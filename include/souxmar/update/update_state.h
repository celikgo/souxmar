// SPDX-License-Identifier: Apache-2.0
//
// Auto-updater — per-user persisted state.
//
// The state file is a tiny TOML doc the updater reads at the start of
// every check / apply and writes at the end. Three jobs:
//
//   * Carry `current_installed_version` so the apply-gate has a
//     `current_version` to compare against (without it, every check
//     would have to re-introspect the running binary, which is a
//     chicken-and-egg during an in-progress swap).
//   * Carry `max_version_ever_seen` for replay-downgrade defence —
//     this is the load-bearing field for ADR-0013 § "Replay-after-
//     rollback attack". Once the client has seen a manifest offering
//     version X, it refuses to apply any future manifest offering
//     anything less, even if it's signed and unexpired.
//   * Carry timestamps (`last_check_at`, `last_apply_at`) so the
//     desktop app's "Check for updates" banner can say "last checked
//     14 minutes ago" and the audit log (push 7) can correlate
//     events.
//
// The file is written atomically: write to <path>.tmp, fsync, rename.
// Loss-of-power between the rename and the next read leaves the file
// either fully written or in its previous state — never partial.
//
// Schema is the same `schema = 1` discriminator pattern as the
// manifest and the plugin-index files. Unknown future-schema values
// are a load error; we'd rather refuse to act than misinterpret a
// future client's bookkeeping.

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <variant>

namespace souxmar::update {

inline constexpr std::uint32_t kUpdateStateSchemaV1 = 1;

struct UpdateState {
  std::uint32_t schema = kUpdateStateSchemaV1;
  // SemVer of the currently-installed build. Empty on a fresh
  // install (the desktop app hasn't run a successful check yet).
  std::string   current_installed_version;
  // Highest manifest version this client has ever applied OR seen
  // offered. The gate's replay-downgrade check reads this. Empty
  // means "no floor" — the gate treats it as "haven't seen anything
  // yet".
  std::string   max_version_ever_seen;
  // RFC-3339 UTC. Empty on a fresh install. Informational.
  std::string   last_check_at;
  // RFC-3339 UTC. Empty until the first successful apply.
  std::string   last_apply_at;
};

// Load-error diagnostic. Same shape the parser modules elsewhere in
// the repo use.
struct UpdateStateLoadError {
  std::string message;
};

using UpdateStateLoadResult =
    std::variant<UpdateState, UpdateStateLoadError>;

// Load the state file at `path`. If the file does not exist, returns
// a default-constructed UpdateState (fresh install). If it exists
// but is malformed (bad TOML, schema mismatch, unparseable version),
// returns UpdateStateLoadError; the caller decides whether to reset
// or refuse. Never throws.
[[nodiscard]] UpdateStateLoadResult
load_update_state(const std::filesystem::path& path);

// Save atomically: write to <path>.tmp, fsync the file, rename. The
// directory is created if missing. Returns false on any I/O failure;
// the caller logs and proceeds — failure to persist state is not a
// reason to refuse a successful update, but the audit log (push 7)
// will record the discrepancy.
[[nodiscard]] bool
save_update_state(const std::filesystem::path& path,
                  const UpdateState&            s);

// Render the state to its canonical TOML string. Split out so unit
// tests can roundtrip without touching the filesystem.
[[nodiscard]] std::string render_update_state(const UpdateState& s);

// Platform-conventional location. Linux:
// $XDG_STATE_HOME/souxmar/update-state.toml (falls back to
// ~/.local/state/souxmar/...); macOS:
// ~/Library/Application Support/souxmar/update-state.toml;
// Windows: %LOCALAPPDATA%\souxmar\update-state.toml.
[[nodiscard]] std::filesystem::path default_update_state_path();

}  // namespace souxmar::update
