// SPDX-License-Identifier: Apache-2.0
//
// Plugin-index parser + search. See include/souxmar/plugin/index.h
// for the schema.
//
// We use tomlplusplus because it's already a dependency of the plugin
// manifest parser — no new third-party adds. The parser is defensive:
// unknown fields are ignored, missing optional fields default to
// empty/Active/NotRun. The only hard failures are syntactic
// (malformed TOML) or "required field absent" — id, name,
// capabilities, and source URL.

#include "souxmar/plugin/index.h"

#include <toml++/toml.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace souxmar::plugin {

namespace fs = std::filesystem;

namespace {

std::string lower(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  return out;
}

bool contains_case_insensitive(std::string_view haystack, std::string_view needle) {
  if (needle.empty()) return true;
  const auto hay  = lower(haystack);
  const auto need = lower(needle);
  return hay.find(need) != std::string::npos;
}

ConformanceStatus parse_conformance(std::string_view s) noexcept {
  if (s == "passed")  return ConformanceStatus::Passed;
  if (s == "failed")  return ConformanceStatus::Failed;
  return ConformanceStatus::NotRun;
}

LifecycleStatus parse_status(std::string_view s) noexcept {
  if (s == "active")         return LifecycleStatus::Active;
  if (s == "maintained")     return LifecycleStatus::Maintained;
  if (s == "unmaintained")   return LifecycleStatus::Unmaintained;
  if (s == "archived")       return LifecycleStatus::Archived;
  return LifecycleStatus::Active;  // safe default — author can self-report later
}

// Parse one TOML table into an IndexEntry. Returns the entry on
// success; throws std::runtime_error with a human-readable message on
// a missing-required-field failure. The caller wraps that into the
// IndexParseError surface.
IndexEntry parse_one(const toml::table& tbl) {
  auto required_str = [&](const char* k) -> std::string {
    const auto* v = tbl.get(k);
    if (!v) {
      throw std::runtime_error(std::string("missing required field: ") + k);
    }
    auto sv = v->value<std::string>();
    if (!sv) {
      throw std::runtime_error(std::string("field must be a string: ") + k);
    }
    return *sv;
  };
  auto optional_str = [&](const char* k) -> std::string {
    const auto* v = tbl.get(k);
    if (!v) return {};
    auto sv = v->value<std::string>();
    return sv ? *sv : std::string{};
  };
  auto optional_bool = [&](const char* k, bool dv) -> bool {
    const auto* v = tbl.get(k);
    if (!v) return dv;
    auto bv = v->value<bool>();
    return bv ? *bv : dv;
  };

  IndexEntry e;
  e.id          = required_str("id");
  e.name        = required_str("name");
  e.source      = required_str("source");
  e.description = optional_str("description");
  e.license     = optional_str("license");
  e.homepage    = optional_str("homepage");
  e.author      = optional_str("author");
  e.souxmar_versions = optional_str("souxmar_versions");
  e.conformance_date = optional_str("conformance_date");
  e.conformance = parse_conformance(optional_str("conformance"));
  e.status      = parse_status(optional_str("status"));
  e.paid        = optional_bool("paid", false);

  // Capabilities: required, must be a non-empty array of strings.
  const auto* caps_node = tbl.get("capabilities");
  if (!caps_node) {
    throw std::runtime_error("missing required field: capabilities");
  }
  const auto* caps_arr = caps_node->as_array();
  if (!caps_arr) {
    throw std::runtime_error("'capabilities' must be an array");
  }
  for (const auto& item : *caps_arr) {
    auto sv = item.value<std::string>();
    if (sv) e.capabilities.push_back(*sv);
  }
  if (e.capabilities.empty()) {
    throw std::runtime_error("'capabilities' must contain at least one entry");
  }
  return e;
}

IndexLoadResult parse_toml_table(const toml::table& root,
                                 std::string_view  ctx_label) {
  const auto* arr = root.get_as<toml::array>("plugin");
  if (!arr) {
    IndexParseError err;
    err.message = std::string(ctx_label) +
                  ": missing top-level [[plugin]] table-array";
    return err;
  }
  std::vector<IndexEntry> out;
  out.reserve(arr->size());
  std::size_t idx = 0;
  for (const auto& node : *arr) {
    const auto* tbl = node.as_table();
    if (!tbl) {
      IndexParseError err;
      err.message = std::string(ctx_label) + ": [[plugin]] entry #" +
                    std::to_string(idx) + " is not a table";
      return err;
    }
    try {
      out.push_back(parse_one(*tbl));
    } catch (const std::exception& e) {
      IndexParseError err;
      err.message = std::string(ctx_label) + ": [[plugin]] entry #" +
                    std::to_string(idx) + ": " + e.what();
      return err;
    }
    ++idx;
  }
  return out;
}

}  // namespace

IndexLoadResult load_index_file(const fs::path& path) {
  try {
    auto root = toml::parse_file(path.string());
    return parse_toml_table(root, path.string());
  } catch (const toml::parse_error& e) {
    IndexParseError err;
    std::ostringstream oss;
    oss << path.string() << ": " << e.description();
    err.message = oss.str();
    return err;
  } catch (const std::exception& e) {
    IndexParseError err;
    err.message = std::string(path.string()) + ": " + e.what();
    return err;
  }
}

IndexLoadResult load_index_string(std::string_view toml_text) {
  try {
    auto root = toml::parse(toml_text);
    return parse_toml_table(root, "<string>");
  } catch (const toml::parse_error& e) {
    IndexParseError err;
    err.message = std::string("<string>: ") + e.description().data();
    return err;
  } catch (const std::exception& e) {
    IndexParseError err;
    err.message = std::string("<string>: ") + e.what();
    return err;
  }
}

std::vector<IndexEntry> search_index(const std::vector<IndexEntry>& entries,
                                     std::string_view               query,
                                     std::string_view               capability_prefix) {
  std::vector<IndexEntry> out;
  out.reserve(entries.size());
  for (const auto& e : entries) {
    if (!capability_prefix.empty()) {
      bool any_match = false;
      for (const auto& c : e.capabilities) {
        if (c.size() >= capability_prefix.size() &&
            std::equal(capability_prefix.begin(), capability_prefix.end(),
                       c.begin())) {
          any_match = true;
          break;
        }
      }
      if (!any_match) continue;
    }
    if (query.empty()) {
      out.push_back(e);
      continue;
    }
    if (contains_case_insensitive(e.id,          query) ||
        contains_case_insensitive(e.name,        query) ||
        contains_case_insensitive(e.description, query) ||
        contains_case_insensitive(e.author,      query)) {
      out.push_back(e);
      continue;
    }
    bool cap_hit = false;
    for (const auto& c : e.capabilities) {
      if (contains_case_insensitive(c, query)) { cap_hit = true; break; }
    }
    if (cap_hit) out.push_back(e);
  }
  return out;
}

std::string_view to_string(ConformanceStatus s) noexcept {
  switch (s) {
    case ConformanceStatus::Passed:  return "passed";
    case ConformanceStatus::NotRun:  return "not_run";
    case ConformanceStatus::Failed:  return "failed";
  }
  return "unknown";
}

std::string_view to_string(LifecycleStatus s) noexcept {
  switch (s) {
    case LifecycleStatus::Active:       return "active";
    case LifecycleStatus::Maintained:   return "maintained";
    case LifecycleStatus::Unmaintained: return "unmaintained";
    case LifecycleStatus::Archived:     return "archived";
  }
  return "unknown";
}

}  // namespace souxmar::plugin
