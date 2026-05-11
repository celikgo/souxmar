// SPDX-License-Identifier: Apache-2.0
//
// Auto-updater — apply / rollback orchestration. See
// include/souxmar/update/apply.h for the public API.

#include "souxmar/update/apply.h"

#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <variant>

namespace souxmar::update {

namespace {

// On a successful state-write, last_apply_at gets the same timestamp
// as the rollback-log event; centralised here so the two stay in
// sync.
std::string now_string(const TimeSource& clock) {
  return format_rfc3339_utc(clock.now());
}

}  // namespace

std::string_view to_string(ApplyOutcome o) noexcept {
  switch (o) {
    case ApplyOutcome::Applied:                  return "applied";
    case ApplyOutcome::RefusedByGate:            return "refused-by-gate";
    case ApplyOutcome::ArtifactHashMismatch:     return "artifact-hash-mismatch";
    case ApplyOutcome::ArtifactSizeMismatch:     return "artifact-size-mismatch";
    case ApplyOutcome::StageFailed:              return "stage-failed";
    case ApplyOutcome::SwitchFailed:             return "switch-failed";
    case ApplyOutcome::AppliedButLogWriteFailed: return "applied-but-log-write-failed";
  }
  return "unknown";
}

std::string_view to_string(RollbackOutcome o) noexcept {
  switch (o) {
    case RollbackOutcome::RolledBack:                   return "rolled-back";
    case RollbackOutcome::NoCurrentInstall:             return "no-current-install";
    case RollbackOutcome::NoRollbackTarget:             return "no-rollback-target";
    case RollbackOutcome::TargetPayloadMissing:         return "target-payload-missing";
    case RollbackOutcome::SwitchFailed:                 return "switch-failed";
    case RollbackOutcome::RolledBackButLogWriteFailed:  return "rolled-back-but-log-write-failed";
  }
  return "unknown";
}

ApplyResult apply_update(const ApplyContext& ctx) {
  ApplyResult r;
  if (ctx.manifest == nullptr || ctx.layout == nullptr ||
      ctx.state == nullptr    || ctx.clock  == nullptr) {
    r.outcome = ApplyOutcome::StageFailed;
    r.detail  = "apply_update: null context member";
    return r;
  }

  // 1. Re-run the gate. Refusal => surface the reason and stop.
  CurrentInstall install{
      ctx.current_version,
      ctx.state->max_version_ever_seen,
      ctx.platform};
  const auto decision = apply_gate(*ctx.manifest, install, *ctx.clock);
  if (const auto* refusal = std::get_if<UpdateRefusal>(&decision)) {
    r.outcome = ApplyOutcome::RefusedByGate;
    r.refusal = refusal->reason;
    r.detail  = refusal->detail;
    return r;
  }
  const auto& apply = std::get<UpdateApply>(decision);

  // 2. Size + sha256 of the artifact bytes.
  if (ctx.artifact_bytes.size() != apply.artifact.size) {
    r.outcome = ApplyOutcome::ArtifactSizeMismatch;
    r.detail  = "artifact bytes are " +
                std::to_string(ctx.artifact_bytes.size()) +
                " bytes, manifest expects " +
                std::to_string(apply.artifact.size);
    return r;
  }
  const auto actual_sha = sha256_hex(ctx.artifact_bytes);
  if (actual_sha != apply.artifact.sha256) {
    r.outcome = ApplyOutcome::ArtifactHashMismatch;
    r.detail  = "artifact sha256 " + actual_sha +
                " != manifest " + apply.artifact.sha256;
    return r;
  }

  // 3. Stage.
  if (!ctx.layout->stage_version(apply.version, ctx.artifact_bytes)) {
    r.outcome = ApplyOutcome::StageFailed;
    r.detail  = "stage_version(" + apply.version + ") failed; check "
                "that the install root is writable + has free space";
    return r;
  }

  // 4. Atomic switch.
  const std::string from_version = ctx.layout->read_current_version();
  if (!ctx.layout->atomic_switch_to(from_version, apply.version)) {
    r.outcome = ApplyOutcome::SwitchFailed;
    r.detail  = "atomic_switch_to(" + apply.version + ") failed";
    return r;
  }

  // 5. Append rollback-log event. A failed append leaves the install
  //    on the new version (correct) but the audit trail with a gap;
  //    the orchestrator surfaces this distinct outcome so the CLI
  //    can warn-but-not-error.
  const auto event_timestamp = now_string(*ctx.clock);
  RollbackEvent ev;
  ev.timestamp       = event_timestamp;
  ev.type            = RollbackEventType::Apply;
  ev.from_version    = from_version;
  ev.to_version      = apply.version;
  ev.artifact_sha256 = apply.artifact.sha256;
  ev.public_key_id   = ctx.manifest->signing.public_key_id;
  const bool log_ok =
      append_rollback_event(ctx.layout->rollback_log_path(), ev);

  // 6. Bump per-user state. The order is deliberate:
  //    current_installed_version reflects what's on disk (we own this
  //    rename); max_version_ever_seen is bumped to whichever is
  //    greater of (existing max, just-applied version), closing the
  //    replay-defence gap noted in push 6's commit message.
  ctx.state->current_installed_version = apply.version;
  if (ctx.state->max_version_ever_seen.empty()) {
    ctx.state->max_version_ever_seen = apply.version;
  } else {
    const auto cmp = compare_versions(apply.version,
                                      ctx.state->max_version_ever_seen);
    if (cmp && *cmp > 0) {
      ctx.state->max_version_ever_seen = apply.version;
    }
  }
  ctx.state->last_apply_at = event_timestamp;

  // 7. Garbage-collect stale version directories. Best-effort —
  //    failure here is invisible to the caller; gc_unreferenced is
  //    bounded by N versions on disk and only ever removes ones
  //    not referenced by current/previous.
  (void)ctx.layout->gc_unreferenced();

  r.outcome         = log_ok ? ApplyOutcome::Applied
                             : ApplyOutcome::AppliedButLogWriteFailed;
  r.applied_version = apply.version;
  r.detail          = log_ok ? "applied"
                             : "applied; rollback-log append failed (audit gap)";
  return r;
}

RollbackResult rollback(const RollbackContext& ctx) {
  RollbackResult r;
  if (ctx.layout == nullptr || ctx.state == nullptr || ctx.clock == nullptr) {
    r.outcome = RollbackOutcome::SwitchFailed;
    r.detail  = "rollback: null context member";
    return r;
  }
  const auto current = ctx.layout->read_current_version();
  if (current.empty()) {
    r.outcome = RollbackOutcome::NoCurrentInstall;
    r.detail  = "current.txt is empty; nothing to roll back from";
    return r;
  }

  // The rollback target is either previous.txt (cheap path; most
  // common case) or — if previous.txt is also empty — the rollback
  // log's most-recent applicable from_version.
  std::string target = ctx.layout->read_previous_version();
  if (target.empty()) {
    auto loaded = load_rollback_log(ctx.layout->rollback_log_path());
    if (auto* err = std::get_if<RollbackLogLoadError>(&loaded)) {
      r.outcome = RollbackOutcome::NoRollbackTarget;
      r.detail  = "rollback-log unreadable: " + err->message;
      return r;
    }
    const auto& events = std::get<std::vector<RollbackEvent>>(loaded);
    target = find_rollback_target(events, current);
  }
  if (target.empty()) {
    r.outcome = RollbackOutcome::NoRollbackTarget;
    r.detail  = "no Apply event in rollback log names '" + current +
                "' as to_version";
    return r;
  }

  if (!ctx.layout->has_version_payload(target)) {
    r.outcome      = RollbackOutcome::TargetPayloadMissing;
    r.from_version = current;
    r.to_version   = target;
    r.detail       = "rollback target '" + target + "' has no payload "
                     "on disk (was it gc'd, or never staged?)";
    return r;
  }

  if (!ctx.layout->atomic_switch_to(current, target)) {
    r.outcome      = RollbackOutcome::SwitchFailed;
    r.from_version = current;
    r.to_version   = target;
    r.detail       = "atomic_switch_to(" + target + ") failed";
    return r;
  }

  const auto event_timestamp = now_string(*ctx.clock);
  RollbackEvent ev;
  ev.timestamp    = event_timestamp;
  ev.type         = RollbackEventType::Rollback;
  ev.from_version = current;
  ev.to_version   = target;
  const bool log_ok =
      append_rollback_event(ctx.layout->rollback_log_path(), ev);

  ctx.state->current_installed_version = target;
  // Deliberately *do not* touch max_version_ever_seen — the replay-
  // defence floor stays put, so we can't be tricked into re-applying
  // the version we just rolled back from via a replayed manifest.
  ctx.state->last_apply_at = event_timestamp;

  r.outcome      = log_ok ? RollbackOutcome::RolledBack
                          : RollbackOutcome::RolledBackButLogWriteFailed;
  r.from_version = current;
  r.to_version   = target;
  r.detail       = log_ok ? "rolled back"
                          : "rolled back; rollback-log append failed";
  return r;
}

}  // namespace souxmar::update
