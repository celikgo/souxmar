// SPDX-License-Identifier: Apache-2.0
//
// Plugin discovery — walks the platform-specific search paths looking for
// directories that contain a `souxmar-plugin.toml`, parses each manifest,
// and returns a list of discovered candidates.
//
// Loading the binaries happens in a later step (Sprint 2). This module is
// pure path / filesystem / parse work.

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "souxmar/plugin/manifest.h"

namespace souxmar::plugin {

struct DiscoveredPlugin {
  std::filesystem::path manifest_path;     // path to the .toml file
  std::filesystem::path binary_path;       // resolved absolute path to the .so/.dylib/.dll
  Manifest              manifest;
};

struct DiscoveryRejection {
  std::filesystem::path candidate_path;    // the directory or .toml that was rejected
  std::string           reason;
};

struct DiscoveryReport {
  std::vector<DiscoveredPlugin>   loaded;
  std::vector<DiscoveryRejection> rejected;
};

// Default search paths in priority order:
//   1. $SOUXMAR_PLUGIN_PATH (colon- or semicolon-separated; OS-typical)
//   2. <install_prefix>/lib/souxmar/plugins/  (passed in as install_prefix)
//   3. Per-user platform-specific:
//        ~/.local/share/souxmar/plugins/                (Linux/BSD)
//        ~/Library/Application Support/souxmar/plugins/ (macOS)
//        %APPDATA%\souxmar\plugins\                     (Windows)
//   4. ./plugins/ in current working directory
//
// Each is opt-in via the corresponding bool. Pass install_prefix = empty path
// to skip slot 2.
struct DiscoveryOptions {
  bool include_env_path        = true;
  bool include_install_prefix  = true;
  bool include_user_prefix     = true;
  bool include_cwd             = false;

  std::filesystem::path install_prefix;    // e.g. /usr/local
};

// Compute the ordered list of search paths from options + environment.
[[nodiscard]] std::vector<std::filesystem::path>
default_search_paths(const DiscoveryOptions& opts);

// Discover plugins under `search_paths`. Each entry is a directory; its
// immediate subdirectories are scanned for a `souxmar-plugin.toml`.
[[nodiscard]] DiscoveryReport
discover_plugins(const std::vector<std::filesystem::path>& search_paths);

// Convenience: default_search_paths(...) + discover_plugins(...).
[[nodiscard]] DiscoveryReport
discover_plugins(const DiscoveryOptions& opts);

}  // namespace souxmar::plugin
