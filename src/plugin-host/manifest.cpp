// SPDX-License-Identifier: Apache-2.0

#include "souxmar/plugin/manifest.h"

#include <fmt/core.h>
#include <toml++/toml.hpp>

#include <fstream>
#include <sstream>

namespace souxmar::plugin {

namespace {

ParseError make_error(std::string message, std::optional<std::size_t> line = std::nullopt) {
  return ParseError{std::move(message), line};
}

// Pull a required string from a TOML table; on absence/wrong-type, populate
// `err` and return false.
bool get_required_string(const toml::table& tbl,
                         std::string_view   path,
                         std::string&       out,
                         ParseError&        err) {
  auto node = tbl.at_path(path);
  if (!node) {
    err = make_error(fmt::format("missing required field '{}'", path));
    return false;
  }
  auto str = node.value<std::string>();
  if (!str) {
    err = make_error(fmt::format("field '{}' must be a string", path));
    return false;
  }
  out = std::move(*str);
  return true;
}

bool get_optional_string(const toml::table& tbl,
                         std::string_view   path,
                         std::string&       out) {
  auto node = tbl.at_path(path);
  if (!node) return false;
  if (auto str = node.value<std::string>()) {
    out = std::move(*str);
    return true;
  }
  return false;
}

}  // namespace

std::string_view to_string(ThreadingModel m) noexcept {
  switch (m) {
    case ThreadingModel::Reentrant:        return "reentrant";
    case ThreadingModel::SingleThreaded:   return "single-threaded";
    case ThreadingModel::InternalParallel: return "internal-parallel";
  }
  return "single-threaded";
}

std::optional<ThreadingModel> threading_from_string(std::string_view s) noexcept {
  if (s == "reentrant")        return ThreadingModel::Reentrant;
  if (s == "single-threaded")  return ThreadingModel::SingleThreaded;
  if (s == "internal-parallel") return ThreadingModel::InternalParallel;
  return std::nullopt;
}

ParseResult parse_manifest(std::string_view toml_source) {
  toml::table tbl;
  try {
    tbl = toml::parse(toml_source);
  } catch (const toml::parse_error& e) {
    const auto& src = e.source();
    return make_error(fmt::format("TOML parse error: {}", e.description()),
                      static_cast<std::size_t>(src.begin.line));
  }

  Manifest m;
  ParseError err;

  // [plugin] block — required.
  if (!get_required_string(tbl, "plugin.id",      m.id,      err)) return err;
  if (!get_required_string(tbl, "plugin.name",    m.name,    err)) return err;
  if (!get_required_string(tbl, "plugin.version", m.version, err)) return err;
  if (!get_required_string(tbl, "plugin.license", m.license, err)) return err;
  get_optional_string(tbl, "plugin.homepage", m.homepage);

  if (auto abi_node = tbl.at_path("plugin.abi")) {
    if (auto abi = abi_node.value<int64_t>()) {
      m.abi = static_cast<std::int32_t>(*abi);
    } else {
      return make_error("'plugin.abi' must be an integer");
    }
  } else {
    return make_error("missing required field 'plugin.abi'");
  }

  // [plugin.binary] — required.
  if (!get_required_string(tbl, "plugin.binary.file", m.binary_file, err)) return err;

  // [plugin.capabilities] — required, must be non-empty array of strings.
  auto caps_node = tbl.at_path("plugin.capabilities.provides");
  if (!caps_node) {
    return make_error("missing required field 'plugin.capabilities.provides'");
  }
  auto caps_arr = caps_node.as_array();
  if (!caps_arr) {
    return make_error("'plugin.capabilities.provides' must be an array");
  }
  for (const auto& v : *caps_arr) {
    auto s = v.value<std::string>();
    if (!s) {
      return make_error("'plugin.capabilities.provides' must be an array of strings");
    }
    m.capabilities.push_back(std::move(*s));
  }
  if (m.capabilities.empty()) {
    return make_error("'plugin.capabilities.provides' must list at least one capability");
  }

  // [plugin.threading] — optional with sensible default.
  if (auto thr_node = tbl.at_path("plugin.threading.model")) {
    auto thr_str = thr_node.value<std::string>();
    if (!thr_str) {
      return make_error("'plugin.threading.model' must be a string");
    }
    auto parsed = threading_from_string(*thr_str);
    if (!parsed) {
      return make_error(fmt::format(
          "'plugin.threading.model' must be one of: reentrant, "
          "single-threaded, internal-parallel (got '{}')",
          *thr_str));
    }
    m.threading = *parsed;
  }

  // [plugin.dependencies] — optional.
  get_optional_string(tbl, "plugin.dependencies.souxmar", m.souxmar_version_constraint);

  // ABI sanity: only ABI v1 is supported in souxmar 1.x.
  if (m.abi != 1) {
    return make_error(fmt::format(
        "'plugin.abi' = {} not supported by this host (only ABI v1 is recognised)",
        m.abi));
  }

  return m;
}

ParseResult parse_manifest_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return make_error(fmt::format("cannot open manifest '{}'", path.string()));
  }
  std::ostringstream buf;
  buf << in.rdbuf();
  return parse_manifest(buf.str());
}

}  // namespace souxmar::plugin
