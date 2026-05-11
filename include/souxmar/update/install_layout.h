// SPDX-License-Identifier: Apache-2.0
//
// Auto-updater — install-root layout and atomic-swap helper.
//
// Sprint 10 push 7 of Platform's auto-updater XL story. Push 4 landed
// the manifest format; push 5 the verifier; push 6 the apply gate +
// `check` CLI; this push lands the file-system half: how the install
// root is structured, how a new version is staged into it, and how
// the swap from "this version is current" to "that version is
// current" happens atomically.
//
// ------------------------------------------------------------------
// On-disk layout
// ------------------------------------------------------------------
//
//   <target_root>/
//     current.txt                       (1-line file: the active version)
//     previous.txt                      (1-line file: the rollback target)
//     versions/
//       0.8.5/                          (a fully-installed prior build)
//         payload                       (the artifact bytes — opaque
//                                       today; will be an unpacked
//                                       archive root once push 9+ adds
//                                       tar/zip extraction)
//       0.9.0/                          (the active build today)
//         payload
//     staging/                          (in-progress payloads; cleaned
//                                       on the next successful apply)
//     rollback.log                      (append-only swap-event log;
//                                       see rollback_log.h)
//
// The "current install" is whatever version `current.txt` names. The
// updater never modifies the running binary's bytes; instead it
// writes a new directory under `versions/<new>/` and atomically
// flips `current.txt`. The launcher (host CLI / desktop app's
// bootstrapper, lands in a future push) reads `current.txt` at
// startup and dispatches to that directory.
//
// Choosing a marker file over a symlink keeps the cross-platform
// story trivial: `rename()` is atomic on POSIX same-filesystem and on
// NTFS (when the target isn't memory-mapped open) — both behaviours
// we rely on. A symlink-based layout would buy nothing here and
// would tangle the Windows code path with junctions / reparse-point
// permissions.
//
// ------------------------------------------------------------------
// Atomicity guarantees this header makes
// ------------------------------------------------------------------
//
//   * `atomic_switch_to(version)` either flips `current.txt` to name
//     `version`, or returns false and leaves the file untouched. No
//     partial write window.
//   * `stage_version(version, payload_bytes)` either materialises
//     `versions/<version>/payload` fully or returns false with the
//     partial dir cleaned. Power-loss between stage and switch
//     leaves an orphan directory under `versions/`; `gc_unreferenced`
//     reaps it on the next apply.
//   * The header does *not* claim anything about subsequent writes
//     to the payload — once `versions/<version>/payload` exists, the
//     updater never modifies it again.

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace souxmar::update {

class InstallLayout {
 public:
  explicit InstallLayout(std::filesystem::path target_root);

  // Read `current.txt`. Returns empty string if the file is missing
  // (fresh install, nothing applied yet) — distinguishable from a
  // present-but-empty file by checking has_current().
  [[nodiscard]] std::string  read_current_version()  const;
  [[nodiscard]] std::string  read_previous_version() const;
  [[nodiscard]] bool         has_current()  const;
  [[nodiscard]] bool         has_previous() const;

  // Enumerate installed versions on disk (the immediate children of
  // versions/). Order is filesystem-defined; callers that care sort
  // afterwards. Excludes incomplete `staging/` entries.
  [[nodiscard]] std::vector<std::string> list_versions() const;

  // True if `versions/<version>/payload` exists and is non-empty.
  // The atomic-switch refuses to flip current.txt to a version that
  // doesn't satisfy this — guards against a half-cleaned-up state
  // where current.txt names a directory that was removed.
  [[nodiscard]] bool         has_version_payload(const std::string& v) const;

  // Materialise versions/<version>/payload with the given bytes. Goes
  // through staging/<version>.<rand>/ + rename to make a power-loss
  // mid-write leave no trace under versions/. The bytes are taken
  // verbatim; the caller (apply_update) is responsible for the
  // sha256 check before calling.
  [[nodiscard]] bool stage_version(const std::string& version,
                                   std::span<const std::uint8_t> payload);

  // Flip current.txt (and shuffle previous.txt to point at the
  // outgoing version) so the running install is now `to_version`.
  // Returns false (and leaves the layout unchanged) if to_version
  // has no payload on disk. `from_version` is the value that *was*
  // in current.txt at call time; passed explicitly so the caller can
  // record it in the rollback log alongside the swap.
  //
  // The implementation writes current.txt.new + previous.txt.new
  // first, then renames both atomically. POSIX rename is atomic
  // same-filesystem; we don't span filesystems here (everything is
  // inside target_root).
  [[nodiscard]] bool atomic_switch_to(const std::string& from_version,
                                      const std::string& to_version);

  // Remove a version directory (and its payload) from disk. Used by
  // gc_unreferenced and by an explicit `souxmar update prune <ver>`
  // path (future push). Refuses to remove the version named by
  // current.txt or previous.txt — both are load-bearing for rollback.
  [[nodiscard]] bool remove_version(const std::string& version);

  // Reap version directories that are neither `current` nor
  // `previous` and have no rollback-log reference. Called at the end
  // of `apply` to keep the install root small. Returns the list of
  // versions reaped; an empty list is a success (nothing to reap),
  // not an error.
  [[nodiscard]] std::vector<std::string>
  gc_unreferenced(std::span<const std::string> protect_versions = {});

  // Path accessors — exposed so the launcher can find the binary to
  // exec and the rollback-log can compute its own filename.
  [[nodiscard]] const std::filesystem::path& root() const noexcept {
    return root_;
  }
  [[nodiscard]] std::filesystem::path payload_path(const std::string& v) const;
  [[nodiscard]] std::filesystem::path rollback_log_path() const;

 private:
  std::filesystem::path root_;

  std::filesystem::path current_marker_path()  const;
  std::filesystem::path previous_marker_path() const;
  std::filesystem::path versions_dir()         const;
  std::filesystem::path staging_dir()          const;
};

// Sha256 helper. Returns a lowercase 64-char hex digest. Used by
// apply_update to verify the artifact bytes match
// manifest.artifact.sha256 before staging. Header-local so the
// manifest module doesn't grow a crypto-related dep; implementation
// uses libsodium's crypto_hash_sha256 (already linked through the
// verifier).
[[nodiscard]] std::string sha256_hex(std::span<const std::uint8_t> bytes);

}  // namespace souxmar::update
