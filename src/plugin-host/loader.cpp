// SPDX-License-Identifier: Apache-2.0

#include "souxmar/plugin/loader.h"

#include "souxmar/plugin/guard.h"

#include "souxmar-c/abi.h"
#include "souxmar-c/plugin.h"

#include <fmt/core.h>

#include <utility>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace souxmar::plugin {

namespace {

#if defined(_WIN32)

void* platform_open(const std::filesystem::path& path, std::string& err) {
  const auto str = path.wstring();
  HMODULE h = LoadLibraryExW(str.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
  if (!h) {
    err = fmt::format("LoadLibraryExW failed (error {})", GetLastError());
  }
  return reinterpret_cast<void*>(h);
}

void platform_close(void* handle) noexcept {
  if (handle)
    FreeLibrary(reinterpret_cast<HMODULE>(handle));
}

void* platform_symbol(void* handle, const char* name) noexcept {
  return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(handle), name));
}

#else  // POSIX

void* platform_open(const std::filesystem::path& path, std::string& err) {
  void* h = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!h) {
    const char* dl = dlerror();
    err = fmt::format("dlopen failed: {}", dl ? dl : "unknown");
  }
  return h;
}

void platform_close(void* handle) noexcept {
  if (handle)
    dlclose(handle);
}

void* platform_symbol(void* handle, const char* name) noexcept {
  // Clear any prior dlerror before calling, per dlsym(3).
  dlerror();
  return dlsym(handle, name);
}

#endif

}  // namespace

// -------- LoadedPlugin --------

struct LoadedPlugin::Impl {
  Registry* registry = nullptr;
  Manifest manifest;
  std::filesystem::path binary_path;
  void* handle = nullptr;
};

LoadedPlugin::LoadedPlugin() : impl_(std::make_unique<Impl>()) {}

LoadedPlugin::~LoadedPlugin() {
  if (!impl_)
    return;
  if (impl_->registry) {
    impl_->registry->remove_plugin(impl_->manifest.id);
  }
  platform_close(impl_->handle);
}

LoadedPlugin::LoadedPlugin(LoadedPlugin&&) noexcept = default;
LoadedPlugin& LoadedPlugin::operator=(LoadedPlugin&&) noexcept = default;

const Manifest& LoadedPlugin::manifest() const noexcept {
  return impl_->manifest;
}

const std::filesystem::path& LoadedPlugin::binary_path() const noexcept {
  return impl_->binary_path;
}

void* LoadedPlugin::native_handle() const noexcept {
  return impl_->handle;
}

// -------- PluginLoader --------

PluginLoader::PluginLoader(Registry& registry, std::string host_version)
    : registry_(registry), host_version_(std::move(host_version)) {}

PluginLoader::~PluginLoader() = default;

std::variant<LoadedPlugin, LoadError> PluginLoader::load(const DiscoveredPlugin& discovered) {
  // 1. Sanity-check ABI before doing anything that would touch the binary.
  if (discovered.manifest.abi != SOUXMAR_ABI_VERSION_MAJOR) {
    return LoadError{discovered.binary_path,
                     fmt::format("manifest declares ABI v{}, host implements ABI v{}",
                                 discovered.manifest.abi,
                                 SOUXMAR_ABI_VERSION_MAJOR)};
  }

  // 2. Open the binary.
  std::string open_err;
  void* handle = platform_open(discovered.binary_path, open_err);
  if (!handle) {
    return LoadError{discovered.binary_path, std::move(open_err)};
  }

  // 3. Resolve the registration symbol.
  auto* sym = platform_symbol(handle, "souxmar_plugin_register_v1");
  if (!sym) {
    platform_close(handle);
    return LoadError{discovered.binary_path,
                     "missing exported symbol 'souxmar_plugin_register_v1'"};
  }
  auto register_fn = reinterpret_cast<souxmar_plugin_register_v1_fn>(sym);

  // 4. Build host info struct.
  const souxmar_host_info_t host_info{
      SOUXMAR_ABI_VERSION_MAJOR,
      SOUXMAR_ABI_VERSION_MINOR,
      host_version_.c_str(),
      0,
  };

  // 5. Stash the plugin's id + threading model on the registry so the
  //    C ABI add_*_c bridges can attribute capabilities + carry threading
  //    through without every plugin call having to pass them. Both slots
  //    are cleared after register returns (success or failure).
  registry_.current_plugin_id_ = discovered.manifest.id;
  registry_.current_plugin_threading_ = discovered.manifest.threading;

  // 6. Call register inside the crash-isolation frame.
  int rc = -1;
  std::string detail;
  const auto guard = guard_call([&] {
    auto* registry_handle = reinterpret_cast<souxmar_registry_t*>(&registry_);
    rc = register_fn(registry_handle, &host_info);
  });
  registry_.current_plugin_id_.clear();
  registry_.current_plugin_threading_ = ThreadingModel::SingleThreaded;

  if (guard.outcome != GuardOutcome::Ok) {
    registry_.remove_plugin(discovered.manifest.id);
    platform_close(handle);
    return LoadError{discovered.binary_path,
                     fmt::format("plugin's souxmar_plugin_register_v1 raised: {}", guard.detail)};
  }
  if (rc != 0) {
    registry_.remove_plugin(discovered.manifest.id);
    platform_close(handle);
    return LoadError{discovered.binary_path,
                     fmt::format("plugin's souxmar_plugin_register_v1 returned non-zero ({})", rc)};
  }

  // 7. Build the LoadedPlugin RAII handle.
  LoadedPlugin lp;
  lp.impl_->registry = &registry_;
  lp.impl_->manifest = discovered.manifest;
  lp.impl_->binary_path = discovered.binary_path;
  lp.impl_->handle = handle;
  return lp;
}

}  // namespace souxmar::plugin
