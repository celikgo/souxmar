// SPDX-License-Identifier: Apache-2.0
//
// Auto-updater — rollback / apply event log.
//
// Sprint 10 push 7. Every `apply` and `rollback` operation appends a
// signed-timestamp entry to this log. The log is the authoritative
// answer to "which version was running on this machine on date X,
// and how did we get there?" — referenced by support tickets, by the
// `souxmar update history` CLI (future push), and by the rollback
// command itself (it reads the log to find the version to flip back
// to).
//
// Format: TOML, schema-discriminated, table-array of [[event]]
// entries. Append is read-modify-write-atomic — write the full file
// to <log>.tmp.<rand>, fsync, rename. Linear in the number of events;
// the log is bounded by the number of upgrades over a release
// lifecycle (typically tens), so the O(N) append is fine.
//
//   schema = 1
//
//   [[event]]
//   timestamp        = "2026-05-12T10:00:00Z"   # RFC-3339 UTC
//   type             = "apply"                  # "apply" | "rollback"
//   from_version     = "0.8.5"                  # may be "" on first install
//   to_version       = "0.9.0"
//   artifact_sha256  = "<64 hex>"               # bound the apply to a payload
//   public_key_id    = "release-2026"           # which key signed the manifest
//
//   [[event]]
//   timestamp        = "2026-05-12T11:30:00Z"
//   type             = "rollback"
//   from_version     = "0.9.0"
//   to_version       = "0.8.5"
//   # artifact_sha256 + public_key_id are intentionally omitted for
//   # rollback events — the bytes are already on disk and weren't
//   # freshly signed.

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <variant>
#include <vector>

namespace souxmar::update {

inline constexpr std::uint32_t kRollbackLogSchemaV1 = 1;

enum class RollbackEventType : std::uint8_t {
  Apply    = 0,
  Rollback = 1,
};

[[nodiscard]] std::string_view to_string(RollbackEventType) noexcept;

struct RollbackEvent {
  std::string         timestamp;          // RFC-3339 UTC
  RollbackEventType   type = RollbackEventType::Apply;
  std::string         from_version;
  std::string         to_version;
  std::string         artifact_sha256;    // empty for rollback events
  std::string         public_key_id;      // empty for rollback events
};

struct RollbackLogLoadError {
  std::string message;
};

using RollbackLogLoadResult =
    std::variant<std::vector<RollbackEvent>, RollbackLogLoadError>;

// Load + parse. Missing file => empty vector (not an error). Schema
// mismatch / malformed TOML => load error.
[[nodiscard]] RollbackLogLoadResult
load_rollback_log(const std::filesystem::path& path);

// Render a vector of events to its canonical TOML string. Field
// order is fixed for diff-friendliness across releases. Exposed for
// unit tests; production callers use append_rollback_event.
[[nodiscard]] std::string
render_rollback_log(const std::vector<RollbackEvent>& events);

// Atomic append: read full file (if exists) + push_back(new_event) +
// write to <path>.tmp.<rand> + rename. Returns false on any I/O
// failure; caller logs and proceeds — a failed append is a
// diagnostic-only loss, the install state itself is fine.
[[nodiscard]] bool
append_rollback_event(const std::filesystem::path& path,
                      const RollbackEvent&          event);

// Find the most-recent rollback target — used by `souxmar update
// rollback` to determine which version to revert to. The algorithm
// walks events in reverse looking for the most recent Apply whose
// `to_version` equals `current_version` (i.e. the event that put us
// here); the `from_version` of that event is what rollback targets.
// Returns empty string if no such event is found (nothing to roll
// back to).
[[nodiscard]] std::string
find_rollback_target(const std::vector<RollbackEvent>& events,
                     const std::string&                current_version);

}  // namespace souxmar::update
