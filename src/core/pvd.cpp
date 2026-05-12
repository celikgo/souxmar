// SPDX-License-Identifier: Apache-2.0
//
// Minimal PVD parser — see header.
//
// Algorithm: walk the input looking for `<DataSet`-opening tags.
// For each, extract the attribute value pair (key="value") for the
// timestep and file keys before the closing `>`. Skip any tag that
// doesn't carry both required attributes.

#include "souxmar/core/pvd.h"

#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

namespace souxmar::core::pvd {

namespace {

// Find the value of attribute `name` inside `tag` (the substring from
// the start of "<DataSet" up to and including the next ">"). Returns
// an empty optional if the attribute isn't present.
struct AttrFind {
  bool             found = false;
  std::string_view value;
};

AttrFind find_attribute(std::string_view tag, std::string_view name) {
  // Search for `<whitespace?><name>="...". We match on the simple
  // pattern <space><name>= because the attribute name appears as a
  // word boundary on the left and "=" on the right.
  std::string needle;
  needle.reserve(name.size() + 1);
  needle.append(name);
  needle.push_back('=');

  std::size_t pos = 0;
  while (pos < tag.size()) {
    const auto hit = tag.find(needle, pos);
    if (hit == std::string_view::npos) return {};
    // Validate that what's immediately before `name` is whitespace or
    // the start of the tag (so timestep doesn't match into something
    // like "footimestep=").
    if (hit > 0) {
      const char prev = tag[hit - 1];
      if (prev != ' ' && prev != '\t' && prev != '\n' && prev != '\r' &&
          prev != '<' && prev != '/') {
        pos = hit + needle.size();
        continue;
      }
    }
    const std::size_t eq_pos = hit + name.size();
    if (eq_pos >= tag.size() || tag[eq_pos] != '=') {
      pos = hit + needle.size();
      continue;
    }
    // Match the opening quote (single or double).
    if (eq_pos + 1 >= tag.size()) return {};
    const char quote = tag[eq_pos + 1];
    if (quote != '"' && quote != '\'') {
      pos = eq_pos + 1;
      continue;
    }
    const std::size_t value_start = eq_pos + 2;
    const std::size_t value_end = tag.find(quote, value_start);
    if (value_end == std::string_view::npos) return {};
    return {true, tag.substr(value_start, value_end - value_start)};
  }
  return {};
}

bool parse_double(std::string_view s, double& out) noexcept {
  // std::from_chars for double is C++17 but support is uneven on some
  // libstdc++ versions; use a copy + strtod for portability since the
  // strings here are small and parsing speed is not on the critical
  // path.
  std::string copy{s};
  char* end = nullptr;
  const double v = std::strtod(copy.c_str(), &end);
  if (end == copy.c_str()) return false;  // no characters consumed
  out = v;
  return true;
}

}  // namespace

ParseResult parse(std::string_view xml) {
  ParseResult result;
  std::size_t pos = 0;

  while (pos < xml.size()) {
    const auto open = xml.find("<DataSet", pos);
    if (open == std::string_view::npos) break;
    // Validate that the character after "<DataSet" is whitespace or
    // a self-closing slash — protects against "<DataSetX" matches.
    const std::size_t after = open + sizeof("<DataSet") - 1;
    if (after >= xml.size()) break;
    const char c = xml[after];
    if (c != ' ' && c != '\t' && c != '\n' && c != '\r' && c != '/' && c != '>') {
      pos = open + 1;
      continue;
    }
    const auto close = xml.find('>', after);
    if (close == std::string_view::npos) {
      result.error = "PVD: unterminated <DataSet> tag";
      return result;
    }
    const std::string_view tag = xml.substr(open, close - open + 1);
    pos = close + 1;

    const auto ts = find_attribute(tag, "timestep");
    const auto fp = find_attribute(tag, "file");
    if (!ts.found || !fp.found) {
      // Tag missing required attributes — skip but don't fail; some
      // tooling emits annotation-only DataSets.
      continue;
    }
    double t = 0.0;
    if (!parse_double(ts.value, t)) {
      result.error = "PVD: timestep value is not a number: '";
      result.error.append(ts.value);
      result.error.append("'");
      return result;
    }
    result.entries.push_back(Entry{t, std::string{fp.value}});
  }

  return result;
}

ParseResult parse_file(const std::filesystem::path& pvd_path) {
  std::ifstream in(pvd_path);
  if (!in) {
    ParseResult r;
    r.error = "PVD: cannot open file: ";
    r.error.append(pvd_path.string());
    return r;
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  auto r = parse(ss.str());
  if (!r.error.empty()) return r;

  // Resolve relative file paths against the PVD's directory.
  const auto base = pvd_path.parent_path();
  for (auto& entry : r.entries) {
    std::filesystem::path p{entry.file};
    if (p.is_relative() && !base.empty()) {
      entry.file = (base / p).lexically_normal().string();
    }
  }
  return r;
}

}  // namespace souxmar::core::pvd
