// SPDX-License-Identifier: Apache-2.0
//
// Plugin loader — opens a discovered plugin's binary, resolves and calls
// its `souxmar_plugin_register_v1` entry point against a Registry.
//
// Each LoadedPlugin owns its native handle (dlopen / LoadLibrary). On
// destruction the loader removes the plugin's capabilities from the
// registry and closes the handle. Move-only.

#pragma once

#include "souxmar/plugin/discovery.h"
#include "souxmar/plugin/manifest.h"
#include "souxmar/plugin/registry.h"

#include <filesystem>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace souxmar::plugin {

struct LoadError {
  std::filesystem::path binary_path;
  std::string message;
};

class LoadedPlugin {
 public:
  ~LoadedPlugin();

  LoadedPlugin(LoadedPlugin&&) noexcept;
  LoadedPlugin& operator=(LoadedPlugin&&) noexcept;

  LoadedPlugin(const LoadedPlugin&) = delete;
  LoadedPlugin& operator=(const LoadedPlugin&) = delete;

  [[nodiscard]] const Manifest& manifest() const noexcept;
  [[nodiscard]] const std::filesystem::path& binary_path() const noexcept;

  // Non-owning native handle (HMODULE on Windows; void* dlopen handle elsewhere).
  // Exposed for diagnostic purposes only — do not call dlclose / FreeLibrary
  // directly on it; LoadedPlugin owns the lifetime.
  [[nodiscard]] void* native_handle() const noexcept;

 private:
  LoadedPlugin();
  friend class PluginLoader;

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

class PluginLoader {
 public:
  // Borrows the registry; the registry must outlive the loader and every
  // LoadedPlugin produced by it. `host_version` is the souxmar release
  // version (e.g. "1.2.3"); passed to plugins via souxmar_host_info_t.
  PluginLoader(Registry& registry, std::string host_version);

  ~PluginLoader();

  PluginLoader(const PluginLoader&) = delete;
  PluginLoader& operator=(const PluginLoader&) = delete;

  // Load a discovered plugin. On success returns a move-only LoadedPlugin
  // that owns the binary handle and (via the registry) the registered
  // capabilities. On failure returns a LoadError describing what went wrong.
  std::variant<LoadedPlugin, LoadError> load(const DiscoveredPlugin& discovered);

 private:
  Registry& registry_;
  std::string host_version_;
};

}  // namespace souxmar::plugin
