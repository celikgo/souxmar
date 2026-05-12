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
#include <unordered_map>
#include <vector>

namespace souxmar::plugin {

namespace fs = std::filesystem;

namespace {

std::string lower(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s)
    out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  return out;
}

bool contains_case_insensitive(std::string_view haystack, std::string_view needle) {
  if (needle.empty())
    return true;
  const auto hay = lower(haystack);
  const auto need = lower(needle);
  return hay.find(need) != std::string::npos;
}

ConformanceStatus parse_conformance(std::string_view s) noexcept {
  if (s == "passed")
    return ConformanceStatus::Passed;
  if (s == "failed")
    return ConformanceStatus::Failed;
  return ConformanceStatus::NotRun;
}

LifecycleStatus parse_status(std::string_view s) noexcept {
  if (s == "active")
    return LifecycleStatus::Active;
  if (s == "maintained")
    return LifecycleStatus::Maintained;
  if (s == "unmaintained")
    return LifecycleStatus::Unmaintained;
  if (s == "archived")
    return LifecycleStatus::Archived;
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
    if (!v)
      return {};
    auto sv = v->value<std::string>();
    return sv ? *sv : std::string{};
  };
  auto optional_bool = [&](const char* k, bool dv) -> bool {
    const auto* v = tbl.get(k);
    if (!v)
      return dv;
    auto bv = v->value<bool>();
    return bv ? *bv : dv;
  };

  IndexEntry e;
  e.id = required_str("id");
  e.name = required_str("name");
  e.source = required_str("source");
  e.description = optional_str("description");
  e.license = optional_str("license");
  e.homepage = optional_str("homepage");
  e.author = optional_str("author");
  e.souxmar_versions = optional_str("souxmar_versions");
  e.conformance_date = optional_str("conformance_date");
  e.conformance = parse_conformance(optional_str("conformance"));
  e.status = parse_status(optional_str("status"));
  e.paid = optional_bool("paid", false);

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
    if (sv)
      e.capabilities.push_back(*sv);
  }
  if (e.capabilities.empty()) {
    throw std::runtime_error("'capabilities' must contain at least one entry");
  }
  return e;
}

IndexLoadResult parse_toml_table(const toml::table& root, std::string_view ctx_label) {
  const auto* arr = root.get_as<toml::array>("plugin");
  if (!arr) {
    IndexParseError err;
    err.message = std::string(ctx_label) + ": missing top-level [[plugin]] table-array";
    return err;
  }
  std::vector<IndexEntry> out;
  out.reserve(arr->size());
  std::size_t idx = 0;
  for (const auto& node : *arr) {
    const auto* tbl = node.as_table();
    if (!tbl) {
      IndexParseError err;
      err.message =
          std::string(ctx_label) + ": [[plugin]] entry #" + std::to_string(idx) + " is not a table";
      return err;
    }
    try {
      out.push_back(parse_one(*tbl));
    } catch (const std::exception& e) {
      IndexParseError err;
      err.message =
          std::string(ctx_label) + ": [[plugin]] entry #" + std::to_string(idx) + ": " + e.what();
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
    err.message = path.string() + ": " + e.what();
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
                                     std::string_view query,
                                     std::string_view capability_prefix) {
  std::vector<IndexEntry> out;
  out.reserve(entries.size());
  for (const auto& e : entries) {
    if (!capability_prefix.empty()) {
      bool any_match = false;
      for (const auto& c : e.capabilities) {
        if (c.size() >= capability_prefix.size()
            && std::equal(capability_prefix.begin(), capability_prefix.end(), c.begin())) {
          any_match = true;
          break;
        }
      }
      if (!any_match)
        continue;
    }
    if (query.empty()) {
      out.push_back(e);
      continue;
    }
    if (contains_case_insensitive(e.id, query) || contains_case_insensitive(e.name, query)
        || contains_case_insensitive(e.description, query)
        || contains_case_insensitive(e.author, query)) {
      out.push_back(e);
      continue;
    }
    bool cap_hit = false;
    for (const auto& c : e.capabilities) {
      if (contains_case_insensitive(c, query)) {
        cap_hit = true;
        break;
      }
    }
    if (cap_hit)
      out.push_back(e);
  }
  return out;
}

std::string_view to_string(ConformanceStatus s) noexcept {
  switch (s) {
    case ConformanceStatus::Passed:
      return "passed";
    case ConformanceStatus::NotRun:
      return "not_run";
    case ConformanceStatus::Failed:
      return "failed";
  }
  return "unknown";
}

std::string_view to_string(LifecycleStatus s) noexcept {
  switch (s) {
    case LifecycleStatus::Active:
      return "active";
    case LifecycleStatus::Maintained:
      return "maintained";
    case LifecycleStatus::Unmaintained:
      return "unmaintained";
    case LifecycleStatus::Archived:
      return "archived";
  }
  return "unknown";
}

std::string_view to_string(IndexIssueSeverity s) noexcept {
  switch (s) {
    case IndexIssueSeverity::Error:
      return "error";
    case IndexIssueSeverity::Warning:
      return "warning";
  }
  return "unknown";
}

// ============================================================================
// Validation (Sprint 10 push 3)
// ============================================================================

namespace {

// http(s):// + at least one host char. Deliberately loose — we want to
// catch obviously-wrong values (empty, "foo", a local path), not police
// URL grammar in depth. RFC 3986 compliance is the user's browser's
// problem when they click the link.
bool looks_like_http_url(std::string_view s) noexcept {
  if (s.size() < 8)
    return false;  // "http://x" minimum
  const bool https = s.starts_with("https://");
  const bool http = s.starts_with("http://");
  if (!https && !http)
    return false;
  const std::size_t prefix_len = https ? 8 : 7;
  if (s.size() <= prefix_len)
    return false;
  // First char after the scheme must be a host character (not '/').
  return s[prefix_len] != '/';
}

// Capability ids: dotted alphanumerics + underscores + hyphens.
// E.g. "mesher.tetra.gmsh", "solver.cfd.openfoam.simple". Strictly
// non-empty; no spaces. Same shape souxmar_registry_add_* enforces
// at registration time.
bool looks_like_capability_id(std::string_view s) noexcept {
  if (s.empty())
    return false;
  for (char c : s) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
                    || c == '.' || c == '_' || c == '-';
    if (!ok)
      return false;
  }
  // Must contain at least one '.' — a capability id without a namespace
  // ("mesh", "solve") would collide with the existing souxmar capability
  // taxonomy. The publishing-plugin-marketplace skill recommends the
  // reverse-DNS convention; we enforce only the dot-namespace shape.
  return s.find('.') != std::string_view::npos;
}

}  // namespace

std::vector<IndexValidationIssue> validate_index(const std::vector<IndexEntry>& entries) {
  std::vector<IndexValidationIssue> out;

  // Cross-entry: duplicate ids. Maintain a first-occurrence map so the
  // second + later occurrences get flagged against the *later* index,
  // matching the position a reviewer would see in a diff.
  std::unordered_map<std::string, std::size_t> seen_ids;
  for (std::size_t i = 0; i < entries.size(); ++i) {
    auto it = seen_ids.find(entries[i].id);
    if (it != seen_ids.end()) {
      IndexValidationIssue iss;
      iss.severity = IndexIssueSeverity::Error;
      iss.entry_index = i;
      iss.field = "id";
      iss.message = "duplicate id '" + entries[i].id + "' — first seen at entry #"
                    + std::to_string(it->second);
      out.push_back(std::move(iss));
    } else {
      seen_ids.emplace(entries[i].id, i);
    }
  }

  // Per-entry checks.
  for (std::size_t i = 0; i < entries.size(); ++i) {
    const auto& e = entries[i];

    // source URL: required by the parser to be non-empty; we additionally
    // require it to look like an http(s) URL. This catches the common
    // mistake of pasting a local path or a `git@...` SSH URL.
    if (!looks_like_http_url(e.source)) {
      IndexValidationIssue iss;
      iss.severity = IndexIssueSeverity::Error;
      iss.entry_index = i;
      iss.field = "source";
      iss.message =
          "source URL must start with http:// or https:// "
          "(got: '"
          + e.source + "')";
      out.push_back(std::move(iss));
    }
    // homepage is optional; when present it must also look URL-shaped.
    if (!e.homepage.empty() && !looks_like_http_url(e.homepage)) {
      IndexValidationIssue iss;
      iss.severity = IndexIssueSeverity::Error;
      iss.entry_index = i;
      iss.field = "homepage";
      iss.message =
          "homepage URL must start with http:// or https:// "
          "(got: '"
          + e.homepage + "')";
      out.push_back(std::move(iss));
    }
    // Capabilities: each must look like a capability id.
    for (std::size_t c = 0; c < e.capabilities.size(); ++c) {
      if (!looks_like_capability_id(e.capabilities[c])) {
        IndexValidationIssue iss;
        iss.severity = IndexIssueSeverity::Error;
        iss.entry_index = i;
        iss.field = "capabilities[" + std::to_string(c) + "]";
        iss.message     = "capability id '" + e.capabilities[c] +
                          "' is not a valid dotted reverse-DNS name "
                          "(alphanumeric + . + _ + -)";
        out.push_back(std::move(iss));
      }
    }
    // License: warn if empty. Open index policy
    // (docs/BUSINESS_MODEL.md § Plugin marketplace economics) says
    // plugins must be OSI-licensed, but the field could be intentionally
    // omitted for a paid listing's free trial. Reviewer judgement.
    if (e.license.empty() && !e.paid) {
      IndexValidationIssue iss;
      iss.severity = IndexIssueSeverity::Warning;
      iss.entry_index = i;
      iss.field = "license";
      iss.message =
          "open-index entry has no license field — "
          "BUSINESS_MODEL.md requires OSI-licensed source "
          "for the open channel; reviewer should confirm";
      out.push_back(std::move(iss));
    }
    // souxmar_versions: warn if empty. ABI v1 plugins should declare
    // ">=1.0,<2.0"; absence is technically valid but indicates the
    // author hasn't thought about forward compatibility.
    if (e.souxmar_versions.empty()) {
      IndexValidationIssue iss;
      iss.severity = IndexIssueSeverity::Warning;
      iss.entry_index = i;
      iss.field = "souxmar_versions";
      iss.message =
          "missing version range — recommended: "
          "'>=1.0,<2.0' for any plugin built against v1 ABI";
      out.push_back(std::move(iss));
    }
    // Conformance: warn on `failed`. The PR can still merge — sometimes
    // an author needs the listing visible to drive bug reports — but
    // reviewers should call it out.
    if (e.conformance == ConformanceStatus::Failed) {
      IndexValidationIssue iss;
      iss.severity = IndexIssueSeverity::Warning;
      iss.entry_index = i;
      iss.field = "conformance";
      iss.message =
          "conformance failed — listing remains visible but "
          "the badge will read 'failed' until reattested";
      out.push_back(std::move(iss));
    }
  }
  return out;
}

}  // namespace souxmar::plugin
