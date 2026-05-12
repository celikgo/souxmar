// SPDX-License-Identifier: Apache-2.0
//
// Auto-updater — apply-gate state machine.
//
// Sprint 10 push 6 of Platform's "Auto-updater across all 3 OSes;
// signed manifest pipeline; rollback protocol" XL story. Push 4 landed
// the manifest data model; push 5 the verifier; this push lands the
// pre-flight decision: "given a (signed and validated) manifest, the
// host's current install state, the host's (os, arch), and a clock —
// would we apply this update right now, and which artifact would we
// pick?"
//
// Scope boundary: this module is *pure logic*. No network, no
// filesystem writes, no time-of-day reads (every clock access goes
// through TimeSource so unit tests can pin the clock and CI runs are
// deterministic regardless of when they execute). The download +
// stage + swap mover lands in push 7 alongside the rollback log; the
// notarisation automation in push 8. By scoping the gate this way:
//
//   * Every refusal reason is a discrete enum value the rollback log
//     records verbatim — an auditor reading
//     `~/.souxmar/update-audit.log` (push 7) can answer "why did this
//     client refuse the 0.9.0 update on 2026-08-12?" without parsing
//     a free-form string.
//   * Reviewers see the policy code (this push) separately from the
//     filesystem code (push 7), so the threat-model questions
//     (replay defence, expires_at, min-previous floor) and the
//     ops-model questions (atomic swap, partial-download cleanup)
//     don't share PR review attention.

#pragma once

#include "souxmar/update/manifest.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace souxmar::update {

// ---- Host platform detection --------------------------------------------

// The (os, arch) the running binary was built for. The state machine
// uses this to pick the right [[artifact]] entry from the manifest.
// `detect_host_platform()` is compile-time-baked from the platform
// macros; an `as-of`-style override does *not* exist (you can't
// download a linux/aarch64 binary onto a windows/x86_64 machine).
struct HostPlatform {
  Os os;
  Arch arch;
};

[[nodiscard]] HostPlatform detect_host_platform() noexcept;

[[nodiscard]] std::optional<HostPlatform> parse_host_platform(
    std::string_view os_arch);  // "linux/x86_64" form

// ---- TimeSource ---------------------------------------------------------
//
// Every clock read in this module goes through TimeSource. Production
// callers use SystemTimeSource (wall-clock via system_clock::now);
// tests inject a FixedTimeSource constructed from an RFC-3339 string
// so the expires_at gate is exercised against a known instant.

class TimeSource {
 public:
  virtual ~TimeSource() = default;
  [[nodiscard]] virtual std::chrono::system_clock::time_point now() const = 0;
};

class SystemTimeSource final : public TimeSource {
 public:
  [[nodiscard]] std::chrono::system_clock::time_point now() const override;
};

class FixedTimeSource final : public TimeSource {
 public:
  explicit FixedTimeSource(std::chrono::system_clock::time_point t) noexcept : t_(t) {}

  [[nodiscard]] std::chrono::system_clock::time_point now() const override {
    return t_;
  }

 private:
  std::chrono::system_clock::time_point t_;
};

// ---- RFC-3339 parser ----------------------------------------------------
//
// Accepts the subset the release pipeline + manifest already emit:
// "YYYY-MM-DDTHH:MM:SSZ" (UTC, second precision, capital Z). Returns
// nullopt on anything else — we deliberately do not implement the full
// RFC-3339 grammar (offset suffixes, fractional seconds, lowercase z)
// because the manifest format ADR pins this exact shape and accepting
// looser forms would mask a release-pipeline bug.

[[nodiscard]] std::optional<std::chrono::system_clock::time_point> parse_rfc3339_utc(
    std::string_view s);

// Format a time_point back to the canonical "YYYY-MM-DDTHH:MM:SSZ"
// shape. Used by the audit-log writer (push 7) and by `--json`
// output of `souxmar update check`.
[[nodiscard]] std::string format_rfc3339_utc(std::chrono::system_clock::time_point t);

// ---- Version compare ----------------------------------------------------
//
// Strict MAJOR.MINOR.PATCH numeric, mirrors the manifest validator's
// looks_semverish in src/updater/manifest.cpp. Returns -1, 0, or +1
// when both inputs are well-formed; nullopt if either is malformed.
// Pre-release suffixes ("0.9.0-beta3") return nullopt — the state
// machine's caller is expected to map "host is on a pre-release" to a
// numeric base version before invoking the gate. Keeping the
// comparison strict here means a malformed manifest version is
// rejected by the state machine (BelowMinPrevious / similar) rather
// than silently treated as zero.

[[nodiscard]] std::optional<int> compare_versions(std::string_view a, std::string_view b);

// ---- Apply-gate I/O types ------------------------------------------------

// Everything the gate needs to know about the running install.
// `current_version` empty => fresh install (no floor); the gate
// proceeds as if current is less than any well-formed manifest
// version. `max_version_ever_seen` empty => no replay floor (also
// the fresh-install state); a non-empty value causes the gate to
// refuse any manifest offering a strictly lower version. See
// ADR-0013 § "Replay-after-rollback attack" for the threat model.
struct CurrentInstall {
  std::string current_version;
  std::string max_version_ever_seen;
  HostPlatform platform;
};

// Why the gate refused. Strings are diagnostics-only; rollback-log
// readers branch on the enum. Order is the source-of-truth for the
// audit log's wire-format integer mapping.
enum class RefusalReason : std::uint8_t {
  // The current install is already at or ahead of the offered
  // version. Not really an "error" — `souxmar update check` exits
  // zero here. Distinguished from Apply so the UI can show
  // "you're up to date" rather than "an update is available".
  AlreadyOnOrAheadOfOffered = 0,
  // channel.expires_at parses cleanly and is in the past.
  Expired = 1,
  // channel.expires_at is non-empty but does not match the canonical
  // RFC-3339 UTC shape. Treated as a refusal (suspicious — looks
  // like an attempted freshness window that the release pipeline
  // mangled) rather than as "no expiry set".
  ExpiredInvalidTime = 2,
  // current_version < release.min_previous_version.
  BelowMinPrevious = 3,
  // release.version < max_version_ever_seen. Defends against a
  // mirror serving a stale-but-still-in-its-window manifest as a
  // downgrade vector.
  ReplayDowngrade = 4,
  // No [[artifact]] entry matches the host's (os, arch). The
  // release pipeline either skipped this platform deliberately
  // (rare; release notes should say so) or has a bug.
  NoArtifactForPlatform = 5,
  // release.version is not MAJOR.MINOR.PATCH numeric. Belt-and-
  // braces — the manifest validator already gates this, but the
  // state machine is layered downstream of a signature-verified
  // manifest and still rejects malformed-version manifests so a
  // would-be exploit path through a hand-crafted-but-validly-signed
  // manifest still trips the gate.
  MalformedOfferedVersion = 6,
  // current_version is non-empty but not MAJOR.MINOR.PATCH numeric.
  // The caller (CLI / desktop app) is expected to normalise — see
  // compare_versions docstring above.
  MalformedCurrentVersion = 7,
  // max_version_ever_seen is non-empty but not MAJOR.MINOR.PATCH
  // numeric. Indicates the state file is corrupted; the CLI
  // surfaces this as "your install is in an inconsistent state,
  // run `souxmar update reset-state` (lands in push 7)".
  MalformedReplayFloor = 8,
};

[[nodiscard]] std::string_view to_string(RefusalReason) noexcept;

// Refusal carries the reason + a short human-readable detail. The
// detail strings are diagnostic — never parse them.
struct UpdateRefusal {
  RefusalReason reason;
  std::string detail;
};

// "Apply this manifest" verdict. The picked artifact is the one
// matching the host's (os, arch). The state machine carries it
// through verbatim from the manifest — push 7's downloader hashes
// against artifact.sha256 byte-for-byte.
struct UpdateApply {
  std::string version;  // = manifest.release.version
  Artifact artifact;    // the one matching host platform
  bool mandatory;       // = manifest.release.mandatory
};

using UpdateDecision = std::variant<UpdateApply, UpdateRefusal>;

// ---- The gate -----------------------------------------------------------
//
// Order of checks, each fail-fast. The order is fixed by ADR-0013's
// threat model and is asserted by unit tests so a refactor cannot
// silently reorder.
//
//   1. release.version  well-formed?              (else MalformedOfferedVersion)
//   2. current_version  well-formed (if non-empty)? (else MalformedCurrentVersion)
//   3. max_seen         well-formed (if non-empty)? (else MalformedReplayFloor)
//   4. release.version <= current_version?         (else fall through; if true,
//   AlreadyOnOrAheadOfOffered)
//   5. channel.expires_at parses?                  (empty is OK; malformed => ExpiredInvalidTime)
//   6. now              < expires_at?              (else Expired)
//   7. min_previous_version <= current_version?    (else BelowMinPrevious; skipped if either side
//   empty)
//   8. max_seen <= release.version?                (else ReplayDowngrade; skipped if max_seen
//   empty)
//   9. host (os, arch) matches an artifact?        (else NoArtifactForPlatform)
//   else Apply.

[[nodiscard]] UpdateDecision apply_gate(const Manifest& m,
                                        const CurrentInstall& install,
                                        const TimeSource& clock);

}  // namespace souxmar::update
