// SPDX-License-Identifier: Apache-2.0

#include "souxmar/plugin/manifest.h"

#include <fmt/core.h>
#include <toml++/toml.hpp>

#include <array>
#include <cctype>
#include <fstream>
#include <sstream>

namespace souxmar::plugin {

namespace {

// The capability allow-list. Keep order in sync with the table in
// docs/PLUGIN_SDK.md § Plugin types.
constexpr std::array<std::string_view, 6> kAllowedNamespaces = {
    "reader",
    "writer",
    "mesher",
    "element",
    "solver",
    "postproc",
};

ParseError make_error(ManifestRejection code,
                      std::string message,
                      std::optional<std::size_t> line = std::nullopt,
                      std::optional<std::size_t> column = std::nullopt,
                      std::string field = {}) {
  ParseError e;
  e.code = code;
  e.message = std::move(message);
  e.line = line;
  e.column = column;
  e.field = std::move(field);
  return e;
}

bool get_required_string(const toml::table& tbl,
                         std::string_view path,
                         std::string& out,
                         ParseError& err) {
  auto node = tbl.at_path(path);
  if (!node) {
    err = make_error(ManifestRejection::MissingField,
                     fmt::format("missing required field '{}'", path),
                     std::nullopt,
                     std::nullopt,
                     std::string{path});
    return false;
  }
  auto str = node.value<std::string>();
  if (!str) {
    err = make_error(ManifestRejection::WrongType,
                     fmt::format("field '{}' must be a string", path),
                     std::nullopt,
                     std::nullopt,
                     std::string{path});
    return false;
  }
  out = std::move(*str);
  return true;
}

bool get_optional_string(const toml::table& tbl, std::string_view path, std::string& out) {
  auto node = tbl.at_path(path);
  if (!node)
    return false;
  if (auto str = node.value<std::string>()) {
    out = std::move(*str);
    return true;
  }
  return false;
}

// Plugin ids must look like reverse-DNS: at least one dot, ASCII
// printable, no whitespace, no path separators. This is loose on
// purpose — the marketplace tightens further at publish time.
bool plugin_id_looks_valid(std::string_view id) noexcept {
  if (id.empty())
    return false;
  if (id.find('.') == std::string_view::npos)
    return false;
  for (char c : id) {
    const auto uc = static_cast<unsigned char>(c);
    if (uc <= 0x20 || uc == 0x7F)
      return false;
    if (c == '/' || c == '\\')
      return false;
    if (!(std::isalnum(uc) || c == '.' || c == '-' || c == '_'))
      return false;
  }
  if (id.front() == '.' || id.back() == '.')
    return false;
  return true;
}

// Basic SemVer shape: <num>.<num>.<num> with optional -<pre>+<build>
// tail. We don't fully implement RFC 2119; we just refuse manifestly-
// broken values (e.g. "abc", "1", "1.2") so the marketplace upload step
// doesn't have to.
bool version_looks_valid(std::string_view v) noexcept {
  if (v.empty())
    return false;
  std::size_t dots = 0;
  bool seen_digit_in_part = false;
  for (std::size_t i = 0; i < v.size(); ++i) {
    const char c = v[i];
    if (c == '.') {
      if (!seen_digit_in_part)
        return false;
      ++dots;
      seen_digit_in_part = false;
      continue;
    }
    if (c == '-' || c == '+') {
      // Hit pre-release / build metadata — require us to be past the
      // patch segment with a digit.
      if (dots < 2 || !seen_digit_in_part)
        return false;
      // Everything after is free-form; the manifest doesn't gate on it.
      return true;
    }
    const auto uc = static_cast<unsigned char>(c);
    if (!std::isdigit(uc))
      return false;
    seen_digit_in_part = true;
  }
  return dots == 2 && seen_digit_in_part;
}

}  // namespace

std::string_view to_string(ThreadingModel m) noexcept {
  switch (m) {
    case ThreadingModel::Reentrant:
      return "reentrant";
    case ThreadingModel::SingleThreaded:
      return "single-threaded";
    case ThreadingModel::InternalParallel:
      return "internal-parallel";
  }
  return "single-threaded";
}

std::optional<ThreadingModel> threading_from_string(std::string_view s) noexcept {
  if (s == "reentrant")
    return ThreadingModel::Reentrant;
  if (s == "single-threaded")
    return ThreadingModel::SingleThreaded;
  if (s == "internal-parallel")
    return ThreadingModel::InternalParallel;
  return std::nullopt;
}

std::string_view to_string(ManifestRejection r) noexcept {
  switch (r) {
    case ManifestRejection::Ok:
      return "ok";
    case ManifestRejection::TomlSyntax:
      return "toml_syntax";
    case ManifestRejection::MissingField:
      return "missing_field";
    case ManifestRejection::WrongType:
      return "wrong_type";
    case ManifestRejection::AbiUnsupported:
      return "abi_unsupported";
    case ManifestRejection::EmptyCapabilities:
      return "empty_capabilities";
    case ManifestRejection::UnknownThreading:
      return "unknown_threading";
    case ManifestRejection::InvalidCapabilityNamespace:
      return "invalid_capability_namespace";
    case ManifestRejection::InvalidPluginId:
      return "invalid_plugin_id";
    case ManifestRejection::InvalidVersion:
      return "invalid_version";
    case ManifestRejection::FileIo:
      return "file_io";
  }
  return "unknown";
}

std::span<const std::string_view> allowed_capability_namespaces() noexcept {
  return std::span<const std::string_view>(kAllowedNamespaces.data(), kAllowedNamespaces.size());
}

bool is_allowed_capability(std::string_view capability_id) noexcept {
  const auto dot = capability_id.find('.');
  if (dot == std::string_view::npos || dot == 0 || dot + 1 == capability_id.size()) {
    return false;
  }
  const auto ns = capability_id.substr(0, dot);
  for (auto allowed : kAllowedNamespaces) {
    if (ns == allowed)
      return true;
  }
  return false;
}

ParseResult parse_manifest(std::string_view toml_source) {
  toml::table tbl;
  try {
    tbl = toml::parse(toml_source);
  } catch (const toml::parse_error& e) {
    const auto& src = e.source();
    return make_error(ManifestRejection::TomlSyntax,
                      fmt::format("TOML parse error: {}", e.description()),
                      static_cast<std::size_t>(src.begin.line),
                      static_cast<std::size_t>(src.begin.column));
  }

  Manifest m;
  ParseError err;

  // [plugin] block — required.
  if (!get_required_string(tbl, "plugin.id", m.id, err))
    return err;
  if (!get_required_string(tbl, "plugin.name", m.name, err))
    return err;
  if (!get_required_string(tbl, "plugin.version", m.version, err))
    return err;
  if (!get_required_string(tbl, "plugin.license", m.license, err))
    return err;
  get_optional_string(tbl, "plugin.homepage", m.homepage);

  // Plugin id + version shape — checked once the required-string
  // extraction has filled them in.
  if (!plugin_id_looks_valid(m.id)) {
    return make_error(ManifestRejection::InvalidPluginId,
                      fmt::format("'plugin.id' = '{}' is not a valid reverse-DNS identifier "
                                  "(letters/digits/dots/hyphens/underscores, at least one dot, "
                                  "no whitespace)",
                                  m.id),
                      std::nullopt,
                      std::nullopt,
                      "plugin.id");
  }
  if (!version_looks_valid(m.version)) {
    return make_error(ManifestRejection::InvalidVersion,
                      fmt::format("'plugin.version' = '{}' does not look like SemVer "
                                  "(expected major.minor.patch[-pre][+build])",
                                  m.version),
                      std::nullopt,
                      std::nullopt,
                      "plugin.version");
  }

  if (auto abi_node = tbl.at_path("plugin.abi")) {
    if (auto abi = abi_node.value<int64_t>()) {
      m.abi = static_cast<std::int32_t>(*abi);
    } else {
      return make_error(ManifestRejection::WrongType,
                        "'plugin.abi' must be an integer",
                        std::nullopt,
                        std::nullopt,
                        "plugin.abi");
    }
  } else {
    return make_error(ManifestRejection::MissingField,
                      "missing required field 'plugin.abi'",
                      std::nullopt,
                      std::nullopt,
                      "plugin.abi");
  }

  // [plugin.binary] — required.
  if (!get_required_string(tbl, "plugin.binary.file", m.binary_file, err))
    return err;

  // [plugin.capabilities] — required, must be non-empty array of strings.
  auto caps_node = tbl.at_path("plugin.capabilities.provides");
  if (!caps_node) {
    return make_error(ManifestRejection::MissingField,
                      "missing required field 'plugin.capabilities.provides'",
                      std::nullopt,
                      std::nullopt,
                      "plugin.capabilities.provides");
  }
  auto caps_arr = caps_node.as_array();
  if (!caps_arr) {
    return make_error(ManifestRejection::WrongType,
                      "'plugin.capabilities.provides' must be an array",
                      std::nullopt,
                      std::nullopt,
                      "plugin.capabilities.provides");
  }
  for (const auto& v : *caps_arr) {
    auto s = v.value<std::string>();
    if (!s) {
      return make_error(ManifestRejection::WrongType,
                        "'plugin.capabilities.provides' must be an array of strings",
                        std::nullopt,
                        std::nullopt,
                        "plugin.capabilities.provides");
    }
    if (!is_allowed_capability(*s)) {
      return make_error(
          ManifestRejection::InvalidCapabilityNamespace,
          fmt::format("'plugin.capabilities.provides' has '{}' — namespace is not in the "
                      "host allow-list (reader, writer, mesher, element, solver, postproc)",
                      *s),
          std::nullopt,
          std::nullopt,
          "plugin.capabilities.provides");
    }
    m.capabilities.push_back(std::move(*s));
  }
  if (m.capabilities.empty()) {
    return make_error(ManifestRejection::EmptyCapabilities,
                      "'plugin.capabilities.provides' must list at least one capability",
                      std::nullopt,
                      std::nullopt,
                      "plugin.capabilities.provides");
  }

  // [plugin.threading] — optional with sensible default.
  if (auto thr_node = tbl.at_path("plugin.threading.model")) {
    auto thr_str = thr_node.value<std::string>();
    if (!thr_str) {
      return make_error(ManifestRejection::WrongType,
                        "'plugin.threading.model' must be a string",
                        std::nullopt,
                        std::nullopt,
                        "plugin.threading.model");
    }
    auto parsed = threading_from_string(*thr_str);
    if (!parsed) {
      return make_error(ManifestRejection::UnknownThreading,
                        fmt::format("'plugin.threading.model' must be one of: reentrant, "
                                    "single-threaded, internal-parallel (got '{}')",
                                    *thr_str),
                        std::nullopt,
                        std::nullopt,
                        "plugin.threading.model");
    }
    m.threading = *parsed;
  }

  // [plugin.dependencies] — optional.
  get_optional_string(tbl, "plugin.dependencies.souxmar", m.souxmar_version_constraint);

  // ------------------------------------------------------------------
  // Sprint 6 push 2 — additive optional fields.
  // ------------------------------------------------------------------
  get_optional_string(tbl, "plugin.description", m.description);
  get_optional_string(tbl, "plugin.documentation", m.documentation);

  if (auto minor_node = tbl.at_path("plugin.min_souxmar_abi_minor")) {
    if (auto v = minor_node.value<int64_t>()) {
      m.min_souxmar_abi_minor = static_cast<std::int32_t>(*v);
    } else {
      return make_error(ManifestRejection::WrongType,
                        "'plugin.min_souxmar_abi_minor' must be an integer",
                        std::nullopt,
                        std::nullopt,
                        "plugin.min_souxmar_abi_minor");
    }
  }

  if (auto tags_node = tbl.at_path("plugin.tags")) {
    auto tags_arr = tags_node.as_array();
    if (!tags_arr) {
      return make_error(ManifestRejection::WrongType,
                        "'plugin.tags' must be an array of strings",
                        std::nullopt,
                        std::nullopt,
                        "plugin.tags");
    }
    for (const auto& v : *tags_arr) {
      auto s = v.value<std::string>();
      if (!s) {
        return make_error(ManifestRejection::WrongType,
                          "'plugin.tags' must be an array of strings",
                          std::nullopt,
                          std::nullopt,
                          "plugin.tags");
      }
      m.tags.push_back(std::move(*s));
    }
  }

  // ABI sanity: only ABI v1 is supported in souxmar 1.x.
  if (m.abi != 1) {
    return make_error(
        ManifestRejection::AbiUnsupported,
        fmt::format("'plugin.abi' = {} not supported by this host (only ABI v1 is recognised)",
                    m.abi),
        std::nullopt,
        std::nullopt,
        "plugin.abi");
  }

  return m;
}

ParseResult parse_manifest_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return make_error(ManifestRejection::FileIo,
                      fmt::format("cannot open manifest '{}'", path.string()));
  }
  std::ostringstream buf;
  buf << in.rdbuf();
  return parse_manifest(buf.str());
}

}  // namespace souxmar::plugin
