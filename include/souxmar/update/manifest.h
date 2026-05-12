// SPDX-License-Identifier: Apache-2.0
//
// Auto-updater — signed update manifest data model.
//
// Sprint 10 push 4 of Platform's "Auto-updater across all 3 OSes;
// signed manifest pipeline; rollback protocol" XL story
// (docs/SPRINT_PLAN.md § Sprint 10). The whole-story design is locked
// in by ADR-0013; this header is the parser-and-validator's view of
// the manifest.
//
// The manifest is a TOML document with a fixed top-level schema
// (`schema = 1`). The release pipeline emits one manifest per channel
// per release; the desktop app and `souxmar update` CLI fetch it,
// verify the detached signature against a pinned key, and use the
// per-(os, arch) artifact metadata to download + apply the new build.
// See docs/adr/0013-signed-update-manifest.md for the full design.
//
// This header intentionally exposes only the *data model* and
// *structural validator*. The signature verifier (libsodium ed25519)
// lands in push 5; the state machine + CLI subcommand land in push 6.
// Splitting those surfaces lets the parser be exercised in CI without
// key material — every parser/validator rule below has unit-test
// coverage in tests/unit/test_update_manifest.cpp.

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace souxmar::update {

// The only schema version this header understands. Older or unknown
// values are a parse error — clients must upgrade their parser before
// they can read a future schema. See ADR-0013 § "Schema bumps".
inline constexpr std::uint32_t kManifestSchemaV1 = 1;

enum class Channel : std::uint8_t {
  Stable = 0,
  Beta = 1,
  Nightly = 2,
};

enum class Os : std::uint8_t {
  Linux = 0,
  Macos = 1,
  Windows = 2,
};

enum class Arch : std::uint8_t {
  X86_64 = 0,
  Aarch64 = 1,
};

// One downloadable artifact: a (channel × release × os × arch) tuple.
// `sha256` binds the manifest to the byte-stream of the artifact; the
// verifier checks the manifest signature first, then hashes the
// downloaded artifact and compares against this field. `size` is for
// the disk-space pre-flight + early-abort-on-wrong-length checks. See
// ADR-0013 § "Why sha256 + size per artifact" for the threat model.
struct Artifact {
  Os os;
  Arch arch;
  std::string url;         // http:// or https://; required, non-empty
  std::string sha256;      // exactly 64 hex chars (lowercase canonical)
  std::uint64_t size = 0;  // bytes; must be > 0
};

// Cryptographic identity of the manifest. The actual signature lives
// in a detached file (`<manifest_url>.sig`); this struct only records
// *which* algorithm + key the signer used. The verifier (push 5) maps
// `public_key_id` to a public key via the embedded trust store.
struct SigningBlock {
  std::string algorithm;      // ADR-0013 v1: must be "ed25519"
  std::string public_key_id;  // pinned id; rotation = separate event
};

// The release this manifest offers. `version` is the SemVer the client
// will move *to*. `min_previous_version` is the floor — the client
// refuses to apply unless its current version is >= this. See
// ADR-0013 § "Why these per-release fields".
struct ReleaseBlock {
  std::string version;               // SemVer of the offered build
  std::string released_at;           // RFC 3339 UTC; informational
  std::string min_previous_version;  // SemVer floor; empty means no floor
  std::string rollback_target;       // SemVer; empty disables rollback
  std::string notes_url;             // release-notes URL; informational
  bool mandatory = false;            // UI nag hint; never auto-applies
};

// Channel scoping + freshness. `expires_at` is the hard kill date —
// the *apply gate* (not the parser) refuses to act on a manifest past
// it. See ADR-0013 § "Why this channel structure" for the freshness
// rationale (cribbed from TUF).
struct ChannelBlock {
  Channel name = Channel::Stable;
  std::string expires_at;  // RFC 3339 UTC; empty => warning
};

// The whole document. A successfully-parsed Manifest has been verified
// to be *structurally* shaped like a manifest; whether it's
// publishable (no duplicate (os, arch) pairs, every sha256 has 64 hex
// chars, etc.) is the validator's job.
struct Manifest {
  std::uint32_t schema = kManifestSchemaV1;
  std::string generated_at;  // RFC 3339 UTC; informational
  ChannelBlock channel;
  ReleaseBlock release;
  std::vector<Artifact> artifacts;
  SigningBlock signing;
};

// Parse error returned by parse_manifest_*. The message is a one-liner
// suitable for printing to stderr or surfacing in a UI; it always
// names the offending field so the release pipeline's emitter can be
// fixed without staring at the raw TOML.
struct ManifestParseError {
  std::string message;
};

using ManifestLoadResult = std::variant<Manifest, ManifestParseError>;

// Parse a manifest from a TOML file on disk.
[[nodiscard]] ManifestLoadResult parse_manifest_file(const std::filesystem::path& path);

// Parse the same shape from an in-memory string (tests; future
// fetched-from-URL path that hands the bytes off without writing them
// to disk first).
[[nodiscard]] ManifestLoadResult parse_manifest_string(std::string_view toml);

// String renderings — used by the CLI's text output and the
// audit-log entries the rollback log will need (push 7).
[[nodiscard]] std::string_view to_string(Channel) noexcept;
[[nodiscard]] std::string_view to_string(Os) noexcept;
[[nodiscard]] std::string_view to_string(Arch) noexcept;

// -------- Validation ---------------------------------------------------
//
// validate_manifest() runs the structural-publishability checks the
// parser intentionally doesn't. Splitting parse from validate keeps
// `parse_manifest_*` cheap (and reusable for "fetch + display, don't
// act yet"), while making the publish-time gates explicit:
//
//   * The release-pipeline CI calls validate_manifest() before
//     uploading any manifest; an Error issue blocks the upload.
//   * The desktop-app update check (push 6) calls validate_manifest()
//     after signature verification and before any download; an Error
//     issue aborts the update with a "manifest is malformed" error.
//
// The validator does *not* check time-dependent properties (e.g.,
// "channel.expires_at is in the past"). Those are the apply-gate's
// job; the validator must be deterministic so unit tests don't
// time-bomb.

enum class ManifestIssueSeverity : std::uint8_t {
  Error = 0,    // structurally unpublishable; the apply gate must refuse
  Warning = 1,  // unusual but not blocking; reviewer judgement
};

struct ManifestValidationIssue {
  ManifestIssueSeverity severity;
  std::string field;  // dotted path, e.g. "artifacts[2].sha256"
  std::string message;
};

[[nodiscard]] std::string_view to_string(ManifestIssueSeverity) noexcept;

// Returns the list of issues; empty means the manifest is publishable
// as-is. Errors gate; warnings inform.
[[nodiscard]] std::vector<ManifestValidationIssue> validate_manifest(const Manifest& m);

}  // namespace souxmar::update
