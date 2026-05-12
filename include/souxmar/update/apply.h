// SPDX-License-Identifier: Apache-2.0
//
// Auto-updater — apply / rollback orchestration.
//
// Sprint 10 push 7. Stitches together every other piece in
// include/souxmar/update/ into the two operations users see:
//
//   apply_update    — given a (signed, validated) manifest, a chosen
//                     artifact, the bytes the artifact resolves to,
//                     an install layout, and per-user state, run the
//                     full transition: verify sha256 + size, stage,
//                     atomic-switch, append rollback-log event, bump
//                     state file. Returns a typed ApplyResult so the
//                     CLI can branch on the outcome.
//
//   rollback        — given an install layout, per-user state, and a
//                     rollback log, find the previous-version target,
//                     atomic-switch to it, append a Rollback event,
//                     update state. Returns a typed RollbackResult.
//
// These are the only two functions in this header. Everything below
// the line is types they return.
//
// The orchestrators do *not* fetch from the network — `apply_update`
// takes the artifact bytes as a span. The CLI either reads them from
// `--artifact <path>` (today's path; the desktop app downloads
// separately) or, once an HTTPS fetcher lands in a future push,
// hands the fetched bytes straight in. The boundary is deliberate:
// crypto-verification (signature + payload sha256) is the load-bearing
// trust act, and the bytes that get hashed must be the *same* bytes
// that get written to disk. Threading a Fetcher abstraction here
// would mean re-verifying after the fetch; passing bytes directly
// makes "verify-then-write" the only path.

#pragma once

#include "souxmar/update/install_layout.h"
#include "souxmar/update/manifest.h"
#include "souxmar/update/rollback_log.h"
#include "souxmar/update/state_machine.h"
#include "souxmar/update/update_state.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

namespace souxmar::update {

enum class ApplyOutcome : std::uint8_t {
  Applied = 0,
  // The gate (apply_gate) returned a refusal. The CLI's caller
  // already saw the RefusalReason via the same machinery `check`
  // uses; `apply_update` carries the reason through so the caller
  // doesn't have to re-run the gate to surface it.
  RefusedByGate = 1,
  // The artifact bytes don't match manifest.artifact.sha256. The
  // caller fetched the wrong file or a mirror served a corrupted
  // download.
  ArtifactHashMismatch = 2,
  // The artifact size doesn't match manifest.artifact.size. Cheaper
  // pre-check than the hash — typically catches truncated downloads
  // before the hash work is wasted.
  ArtifactSizeMismatch = 3,
  // Filesystem error during stage_version. The install layout's
  // staging/ directory or the target versions/ subdirectory could
  // not be written.
  StageFailed = 4,
  // Filesystem error during atomic_switch_to. The marker-file
  // rename failed; the install state is unchanged.
  SwitchFailed = 5,
  // The rollback-log append failed *after* the swap completed. The
  // install is on the new version (which is correct) but the audit
  // trail has a gap. This is the only outcome where the result is
  // "mostly success" — the caller surfaces a warning, not an
  // error.
  AppliedButLogWriteFailed = 6,
};

[[nodiscard]] std::string_view to_string(ApplyOutcome) noexcept;

struct ApplyResult {
  ApplyOutcome outcome = ApplyOutcome::RefusedByGate;
  // Set when outcome == RefusedByGate.
  RefusalReason refusal = RefusalReason::MalformedOfferedVersion;
  // Set when outcome == Applied or AppliedButLogWriteFailed.
  std::string applied_version;
  // Diagnostic; never parse.
  std::string detail;
};

struct ApplyContext {
  // The manifest the caller wants to apply. Assumed signature-verified
  // by the caller; apply_update does not re-verify (that work is the
  // CLI's, sitting at the trust boundary).
  const Manifest* manifest = nullptr;
  // The artifact bytes the manifest's selected artifact resolves to.
  // The caller has either read them from --artifact <path> or fetched
  // them from the network; either way, these are the bytes that get
  // hashed and written to disk.
  std::span<const std::uint8_t> artifact_bytes;
  // Layout + per-user state + clock. Mirrors the apply_gate signature.
  InstallLayout* layout = nullptr;
  UpdateState* state = nullptr;
  const TimeSource* clock = nullptr;
  // Host platform — drives the artifact-picking step inside the
  // gate. The CLI fills this from the same --platform override the
  // `check` subcommand exposes.
  HostPlatform platform{};
  // The current install state's view of itself. Same shape as the
  // `check` subcommand builds; passed in so the gate can be re-run
  // without re-loading the state file. apply_update *will* mutate
  // `state` on success; this struct just frames the inputs.
  std::string current_version;
};

[[nodiscard]] ApplyResult apply_update(const ApplyContext& ctx);

// ---- Rollback ------------------------------------------------------------

enum class RollbackOutcome : std::uint8_t {
  RolledBack = 0,
  // current.txt is empty — nothing to roll back from.
  NoCurrentInstall = 1,
  // No matching Apply event in the rollback log — first install, or
  // log truncated. The CLI surfaces this as "no rollback target;
  // rollback is unavailable for this install."
  NoRollbackTarget = 2,
  // The rollback target version directory is missing from disk
  // (gc'd, or never staged). install state is unchanged.
  TargetPayloadMissing = 3,
  // Filesystem error during atomic_switch_to.
  SwitchFailed = 4,
  // The rollback-log append failed *after* the swap completed.
  RolledBackButLogWriteFailed = 5,
};

[[nodiscard]] std::string_view to_string(RollbackOutcome) noexcept;

struct RollbackResult {
  RollbackOutcome outcome = RollbackOutcome::NoRollbackTarget;
  std::string from_version;
  std::string to_version;
  std::string detail;
};

struct RollbackContext {
  InstallLayout* layout = nullptr;
  UpdateState* state = nullptr;
  const TimeSource* clock = nullptr;
};

[[nodiscard]] RollbackResult rollback(const RollbackContext& ctx);

}  // namespace souxmar::update
