// SPDX-License-Identifier: Apache-2.0

#include "souxmar/plugin/discovery.h"

#include <fmt/core.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <string>
#include <string_view>
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

// The three canonical shared-library extensions, host-native first.
//
// Why this list exists: a `souxmar-plugin.toml` is a *source* artefact that
// ships in the plugin's repository, so it can only ever name one filename —
// and by convention (see docs/PLUGIN_SDK.md) that name is the ELF one,
// `lib<target>.so`. The artefact CMake actually produces is
// `lib<target>.dylib` on macOS and `<target>.dll` on Windows. Taking the
// manifest literally therefore rejected every plugin on two of the three
// supported platforms with `binary_not_found`.
//
// Order matters and is deliberately fixed rather than
// filesystem-iteration-derived: if a plugin directory somehow holds two
// artefacts with the same stem (a stale cross-build, a fat release bundle),
// discovery must pick the same one on every run on every machine, or the
// determinism gate in docs/ENGINEERING_PRACTICES.md is unenforceable.
#if defined(_WIN32)
constexpr std::array<std::string_view, 3> kBinaryExtensions{".dll", ".so", ".dylib"};
#elif defined(__APPLE__)
constexpr std::array<std::string_view, 3> kBinaryExtensions{".dylib", ".so", ".dll"};
#else
constexpr std::array<std::string_view, 3> kBinaryExtensions{".so", ".dylib", ".dll"};
#endif

// Resolve `declared` (the manifest's `[plugin.binary] file` value) against
// what is actually on disk in `dir`.
//
//   1. The declared name wins whenever it exists. The manifest stays
//      authoritative, so a plugin that genuinely ships `foo.dylib` on macOS
//      and says so is loaded from exactly that file, and a plugin shipping
//      several artefacts side by side gets the one it asked for.
//   2. Otherwise the declared extension (if it is one of the three canonical
//      ones) is stripped and the stem is retried with each canonical
//      extension in kBinaryExtensions order — host-native first.
//
// Returns nullopt when nothing resolves; the caller then emits the same
// `BinaryNotFound` rejection (naming the declared path) it always has, so
// the diagnostic a plugin author sees for a genuinely missing binary is
// unchanged.
std::optional<fs::path> resolve_plugin_binary(const fs::path& dir, std::string_view declared) {
  std::error_code ec;

  auto declared_path = dir / fs::path(declared);
  if (fs::exists(declared_path, ec)) {
    return declared_path;
  }

  std::string_view stem = declared;
  for (const auto ext : kBinaryExtensions) {
    if (ends_with(declared, ext)) {
      stem = declared.substr(0, declared.size() - ext.size());
      break;
    }
  }
  if (stem.empty()) {
    return std::nullopt;
  }

  for (const auto ext : kBinaryExtensions) {
    auto candidate = dir / fs::path(std::string(stem) + std::string(ext));
    ec.clear();
    if (fs::exists(candidate, ec)) {
      return candidate;
    }
  }
  return std::nullopt;
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

      // Resolve binary path relative to the manifest. The declared name wins
      // when it exists; otherwise the same stem is retried with the host's
      // canonical shared-library extension. See resolve_plugin_binary().
      const auto declared_path = manifest_path.parent_path() / manifest.binary_file;
      auto resolved = resolve_plugin_binary(manifest_path.parent_path(), manifest.binary_file);
      if (!resolved) {
        DiscoveryRejection r{};
        r.candidate_path = manifest_path;
        r.reason = fmt::format("declared binary '{}' does not exist at '{}'",
                               manifest.binary_file,
                               declared_path.string());
        r.code = DiscoveryRejectionCode::BinaryNotFound;
        report.rejected.push_back(std::move(r));
        continue;
      }
      auto binary_path = std::move(*resolved);
      // Soft-validate the extension of the file we actually resolved. When the
      // fallback fired the resolved name is canonical by construction, so this
      // only ever rejects a *declared* name with a bogus extension — the same
      // behaviour, and the same message, as before the fallback existed.
      if (!plausible_plugin_binary_name(binary_path.filename().string())) {
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
