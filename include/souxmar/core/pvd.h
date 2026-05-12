// SPDX-License-Identifier: Apache-2.0
//
// Minimal PVD (ParaView Data) parser.
//
// PVD is the de-facto on-disk format for time-series VTU output: a thin
// XML wrapper pointing at a sequence of per-timestep VTU files plus
// their timestamps. The format is small enough that we hand-roll a
// substring-based extractor rather than pulling in a full XML library
// (the host process otherwise has no XML dependency).
//
// What this parses:
//   <DataSet timestep="<float>" file="<path>"/>
// Everything else in the document is ignored. The parser is forgiving
// of unrelated whitespace, attribute ordering, and self-closing-tag
// style; it does NOT validate XML well-formedness in general.
//
// Sprint 32 PR 1 (partial) — the parser plus its tests. Wiring PVD
// into TimeSeries::FrameLoader (and through to souxmar_timeseries_open
// on a `.pvd` path) lands in PR 2 alongside the VTU reader plugin
// that produces the actual per-frame Fields.
//
// See docs/rfcs/0006-time-series.md.

#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace souxmar::core::pvd {

struct Entry {
  double timestep;   // value from the timestep attribute
  std::string file;  // value from the file attribute (relative path
                     // as written; caller resolves against pvd dir)
};

struct ParseResult {
  std::vector<Entry> entries;
  // Empty error means success. Diagnostic messages are caller-readable;
  // they intentionally don't carry line numbers because the parser is
  // substring-based.
  std::string error;
};

// Parse PVD XML content. Returns ParseResult{entries, ""} on success;
// a non-empty error string on failure (entries may still be partially
// populated for tools that want best-effort).
[[nodiscard]] ParseResult parse(std::string_view xml);

// Convenience: read the file and parse. Resolves DataSet file paths
// relative to pvd_path.parent_path() if they are not already absolute,
// so callers get back loadable paths in entries[i].file.
[[nodiscard]] ParseResult parse_file(const std::filesystem::path& pvd_path);

}  // namespace souxmar::core::pvd
