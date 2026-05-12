// SPDX-License-Identifier: Apache-2.0

#include "souxmar/plugin/discovery.h"

#include <fmt/core.h>

#include <algorithm>
#include <cstdlib>
#include <system_error>

namespace souxmar::plugin {

namespace fs = std::filesystem;

namespace {

#if defined(_WIN32)
constexpr char kPathSeparator = ';';
#else
constexpr char kPathSeparator = ':';
#endif

std::vector<fs::path> split_path_env(const char* env_value) {
  std::vector<fs::path> out;
  if (!env_value)
    return out;
  std::string s(env_value);
  std::size_t start = 0;
  for (std::size_t i = 0; i <= s.size(); ++i) {
    if (i == s.size() || s[i] == kPathSeparator) {
      if (i > start) {
        out.emplace_back(s.substr(start, i - start));
      }
      start = i + 1;
    }
  }
  return out;
}

std::optional<fs::path> user_plugins_dir() {
#if defined(_WIN32)
  if (auto* appdata = std::getenv("APPDATA")) {
    return fs::path(appdata) / "souxmar" / "plugins";
  }
#elif defined(__APPLE__)
  if (auto* home = std::getenv("HOME")) {
    return fs::path(home) / "Library" / "Application Support" / "souxmar" / "plugins";
  }
#else
  // Linux / BSD: respect XDG_DATA_HOME, fall back to ~/.local/share.
  if (auto* xdg = std::getenv("XDG_DATA_HOME")) {
    return fs::path(xdg) / "souxmar" / "plugins";
  }
  if (auto* home = std::getenv("HOME")) {
    return fs::path(home) / ".local" / "share" / "souxmar" / "plugins";
  }
#endif
  return std::nullopt;
}

bool ends_with(std::string_view s, std::string_view suffix) {
  return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

// On the host platform, what binary extension do plugins typically use?
// Discovery does not enforce it strictly — the manifest's `binary.file`
// value is authoritative — but we do a soft-validate so misnamed binaries
// surface as a discovery rejection rather than a load-time crash.
bool plausible_plugin_binary_name(std::string_view name) {
  return ends_with(name, ".so") || ends_with(name, ".dylib") || ends_with(name, ".dll");
}

}  // namespace

std::vector<fs::path> default_search_paths(const DiscoveryOptions& opts) {
  std::vector<fs::path> paths;

  if (opts.include_env_path) {
    if (auto* env = std::getenv("SOUXMAR_PLUGIN_PATH")) {
      auto from_env = split_path_env(env);
      paths.insert(paths.end(), from_env.begin(), from_env.end());
    }
  }
  if (opts.include_install_prefix && !opts.install_prefix.empty()) {
    paths.push_back(opts.install_prefix / "lib" / "souxmar" / "plugins");
  }
  if (opts.include_user_prefix) {
    if (auto u = user_plugins_dir()) {
      paths.push_back(*u);
    }
  }
  if (opts.include_cwd) {
    paths.push_back(fs::current_path() / "plugins");
  }

  // De-duplicate while preserving order.
  std::vector<fs::path> deduped;
  for (auto& p : paths) {
    auto canonical = p;
    if (std::find(deduped.begin(), deduped.end(), canonical) == deduped.end()) {
      deduped.push_back(std::move(canonical));
    }
  }
  return deduped;
}

DiscoveryReport discover_plugins(const std::vector<fs::path>& search_paths) {
  DiscoveryReport report;

  for (const auto& root : search_paths) {
    std::error_code ec;
    if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) {
      // Missing search path is not an error — it just means no plugins live there.
      continue;
    }

    for (const auto& candidate : fs::directory_iterator(root, ec)) {
      if (ec) {
        DiscoveryRejection r{};
        r.candidate_path = root;
        r.reason = fmt::format("cannot iterate: {}", ec.message());
        r.code = DiscoveryRejectionCode::CannotIterateSearchPath;
        report.rejected.push_back(std::move(r));
        ec.clear();
        break;
      }
      if (!candidate.is_directory(ec))
        continue;

      const auto manifest_path = candidate.path() / "souxmar-plugin.toml";
      if (!fs::exists(manifest_path, ec)) {
        // Quiet skip — a directory without a manifest is just not a plugin.
        continue;
      }

      auto result = parse_manifest_file(manifest_path);
      if (auto* err = std::get_if<ParseError>(&result)) {
        DiscoveryRejection r{};
        r.candidate_path = manifest_path;
        r.reason = err->message;
        r.code = DiscoveryRejectionCode::ManifestParseFailed;
        r.manifest_code = err->code;
        report.rejected.push_back(std::move(r));
        continue;
      }
      auto& manifest = std::get<Manifest>(result);

      // Resolve binary path relative to the manifest.
      auto binary_path = manifest_path.parent_path() / manifest.binary_file;
      if (!fs::exists(binary_path, ec)) {
        DiscoveryRejection r{};
        r.candidate_path = manifest_path;
        r.reason = fmt::format("declared binary '{}' does not exist at '{}'",
                               manifest.binary_file,
                               binary_path.string());
        r.code = DiscoveryRejectionCode::BinaryNotFound;
        report.rejected.push_back(std::move(r));
        continue;
      }
      if (!plausible_plugin_binary_name(manifest.binary_file)) {
        DiscoveryRejection r{};
        r.candidate_path = manifest_path;
        r.reason =
            fmt::format("binary '{}' has unrecognised extension; expected .so / .dylib / .dll",
                        manifest.binary_file);
        r.code = DiscoveryRejectionCode::BinaryUnrecognisedExtension;
        report.rejected.push_back(std::move(r));
        continue;
      }

      report.loaded.push_back(DiscoveredPlugin{
          manifest_path,
          binary_path,
          std::move(manifest),
      });
    }
  }

  return report;
}

std::string_view to_string(DiscoveryRejectionCode r) noexcept {
  switch (r) {
    case DiscoveryRejectionCode::Unknown:
      return "unknown";
    case DiscoveryRejectionCode::CannotIterateSearchPath:
      return "cannot_iterate_search_path";
    case DiscoveryRejectionCode::ManifestParseFailed:
      return "manifest_parse_failed";
    case DiscoveryRejectionCode::BinaryNotFound:
      return "binary_not_found";
    case DiscoveryRejectionCode::BinaryUnrecognisedExtension:
      return "binary_unrecognised_extension";
  }
  return "unknown";
}

DiscoveryReport discover_plugins(const DiscoveryOptions& opts) {
  return discover_plugins(default_search_paths(opts));
}

}  // namespace souxmar::plugin
