// SPDX-License-Identifier: Apache-2.0
//
// Plugin index data model.
//
// Sprint 10 push 2 — Plugin-team SPRINT_PLAN.md story for Sprint 10
// ("Plugin index data model; `souxmar plugin search` against the
// static index"). This header defines the schema; src/plugin-host/
// index.cpp parses + serialises + filters; docs/plugin-index.toml is
// the canonical static index the open `souxmar plugin search`
// queries.
//
// The index is a TOML file with a `[[plugin]]` table-array. Each
// entry records the publishable metadata a souxmar user needs to
// discover the plugin: who wrote it, what it does (capabilities),
// where the source lives, which souxmar version range it supports,
// what its conformance + lifecycle status is. We deliberately do not
// embed binaries here — that's the paid-marketplace's job
// (`docs/BUSINESS_MODEL.md` § Plugin marketplace economics, paid
// channel launches at Sprint 16). The open index points at upstream
// source URLs; installation is the user's responsibility.
//
// Schema design notes:
//   * String fields are UTF-8. Empty / absent are treated equivalently
//     by the parser (the in-memory `std::string` is just empty), so
//     authors don't have to repeat boilerplate when a field doesn't
//     apply (e.g. `homepage` for a plugin whose source URL is its
//     own homepage).
//   * `capabilities` is a list of capability ids the plugin registers.
//     Matches the `capability_id` strings the C ABI uses (e.g.
//     `mesher.tetra.gmsh`, `solver.cfd.openfoam.simple`).
//   * `souxmar_versions` is a semver range constraint string — same
//     grammar as the plugin manifest's `[plugin.souxmar_required]`
//     field. Recommended: `>=1.0,<2.0` for any plugin built against
//     the v1 ABI; matches the SemVer convention the ABI freeze
//     promises (see ADR-0008).
//   * `conformance` is one of `passed`, `not_run`, `failed`. A passing
//     conformance attestation is the only quality signal the open
//     index surfaces; we deliberately do not vet code or behaviour
//     (`docs/BUSINESS_MODEL.md` § "We do not gatekeep on quality").
//   * `status` is the lifecycle marker: `active`, `maintained`,
//     `unmaintained`, `archived`. Authors self-report. The skill
//     `publishing-plugin-marketplace` documents the convention.
//   * `paid` is a bool. The open index lists free plugins by default;
//     a `paid = true` entry indicates the plugin is in the paid
//     marketplace (purchase via souxmar.dev/plugins) — flagged in
//     `souxmar plugin search` output so users know which entries
//     require a license.

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace souxmar::plugin {

enum class ConformanceStatus : std::uint8_t {
  Passed   = 0,
  NotRun   = 1,
  Failed   = 2,
};

enum class LifecycleStatus : std::uint8_t {
  Active        = 0,
  Maintained    = 1,   // accepts bug fixes; not adding features
  Unmaintained  = 2,
  Archived      = 3,
};

struct IndexEntry {
  std::string                id;                // e.g. "dev.souxmar.examples.gmsh-mesher"
  std::string                name;              // human-readable, e.g. "Gmsh tetrahedral mesher"
  std::string                description;       // one-paragraph description
  std::vector<std::string>   capabilities;      // capability ids registered by the plugin
  std::string                license;           // SPDX expression, e.g. "Apache-2.0"
  std::string                source;            // URL of source repo
  std::string                homepage;          // optional; defaults to source if empty
  std::string                author;            // e.g. "souxmar project" or "Example Inc. (https://example.com)"
  std::string                souxmar_versions;  // SemVer range, e.g. ">=1.0,<2.0"
  ConformanceStatus          conformance     = ConformanceStatus::NotRun;
  std::string                conformance_date; // ISO-8601, e.g. "2026-05-11"; empty if not_run
  LifecycleStatus            status          = LifecycleStatus::Active;
  bool                       paid            = false;
};

// Parse error returned by load_index_*. Caller can decide whether to
// log + continue (per-entry parse failures shouldn't crash the CLI)
// or abort. The message is a one-liner safe to print.
struct IndexParseError {
  std::string  message;
};

using IndexLoadResult = std::variant<std::vector<IndexEntry>, IndexParseError>;

// Parse a plugin index from a TOML file on disk. The file is expected
// to be a `[[plugin]]` table-array; each table maps to an IndexEntry.
[[nodiscard]] IndexLoadResult
load_index_file(const std::filesystem::path& path);

// Parse the same shape from an in-memory string. Useful for tests and
// for piping a fetched-from-URL index through the same parser.
[[nodiscard]] IndexLoadResult
load_index_string(std::string_view toml);

// Filter the entries against a search query. Empty query returns the
// full list. Non-empty: case-insensitive substring match against
// `id`, `name`, `description`, `capabilities`, `author`. Matches in
// any field count.
//
// Optionally restricted by capability prefix (e.g. "mesher.tetra" to
// list every tetrahedral mesher); pass empty string_view to skip.
//
// The returned vector preserves the input order so an author who
// curates their listing's spot in the canonical index controls the
// position users see.
[[nodiscard]] std::vector<IndexEntry>
search_index(const std::vector<IndexEntry>& entries,
             std::string_view               query,
             std::string_view               capability_prefix = {});

// String renderings — used by both the CLI's text output and the
// audit-log entry the paid marketplace will need (Sprint 16+).
[[nodiscard]] std::string_view to_string(ConformanceStatus) noexcept;
[[nodiscard]] std::string_view to_string(LifecycleStatus)   noexcept;

// -------- Validation (Sprint 10 push 3) ---------------------------------
//
// validate_index() runs structural checks the parser doesn't (and
// shouldn't — `load_index_*` is for "can I read this file"; validation
// is for "is this listing publishable"). The CI workflow
// `.github/workflows/plugin-index.yml` runs it on every PR that
// touches `docs/plugin-index.toml`; the CLI subcommand
// `souxmar plugin validate-index` runs the same checks locally.

enum class IndexIssueSeverity : std::uint8_t {
  Error    = 0,   // PR can't merge; structural problem (duplicate id, bad URL).
  Warning  = 1,   // PR can merge; reviewer judgement (empty license, no version range).
};

struct IndexValidationIssue {
  IndexIssueSeverity  severity;
  std::size_t         entry_index;   // 0-based position in the input vector
  std::string         field;         // the affected field name; "" if cross-entry
  std::string         message;       // human-readable diagnostic
};

[[nodiscard]] std::string_view to_string(IndexIssueSeverity) noexcept;

// Returns the list of issues. Empty means the index is publishable as-is.
// At least one Error means a CI gate should reject the PR. Warnings are
// printed but don't gate.
[[nodiscard]] std::vector<IndexValidationIssue>
validate_index(const std::vector<IndexEntry>& entries);

}  // namespace souxmar::plugin
