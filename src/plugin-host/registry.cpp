// SPDX-License-Identifier: Apache-2.0

#include "souxmar/plugin/registry.h"

#include <fmt/core.h>

#include <algorithm>
#include <utility>

#include "souxmar-c/registry.h"

namespace souxmar::plugin {

Registry::Registry()  = default;
Registry::~Registry() = default;
Registry::Registry(Registry&&) noexcept = default;
Registry& Registry::operator=(Registry&&) noexcept = default;

std::variant<std::monostate, RegistryError>
Registry::add_mesher(std::string                     capability_id,
                     std::string                     plugin_id,
                     const souxmar_mesher_vtable_t*  vtable,
                     void*                           user_data) {
  if (capability_id.empty()) {
    return RegistryError{"capability_id must not be empty"};
  }
  if (vtable == nullptr) {
    return RegistryError{fmt::format("'{}': vtable pointer is null", capability_id)};
  }
  if (vtable->abi_version != SOUXMAR_ABI_VERSION_MAJOR) {
    return RegistryError{fmt::format(
        "'{}': vtable.abi_version = {}, host expects {}",
        capability_id, vtable->abi_version, SOUXMAR_ABI_VERSION_MAJOR)};
  }
  if (vtable->mesh_fn == nullptr) {
    return RegistryError{fmt::format("'{}': vtable.mesh_fn is null", capability_id)};
  }

  std::unique_lock lock(mu_);
  if (entries_.contains(capability_id)) {
    return RegistryError{fmt::format(
        "'{}': capability already registered", capability_id)};
  }

  CapabilityEntry entry{
      capability_id,
      std::move(plugin_id),
      CapabilityKind::Mesher,
      vtable->abi_version,
      MesherEntry{vtable, user_data},
  };
  entries_.emplace(std::move(capability_id), std::move(entry));
  return std::monostate{};
}

std::size_t Registry::size() const noexcept {
  std::shared_lock lock(mu_);
  return entries_.size();
}

std::vector<std::string> Registry::list_capabilities() const {
  std::shared_lock lock(mu_);
  std::vector<std::string> out;
  out.reserve(entries_.size());
  for (const auto& [id, _] : entries_) out.push_back(id);
  std::sort(out.begin(), out.end());
  return out;
}

std::vector<std::string>
Registry::list_capabilities_in_namespace(std::string_view ns) const {
  std::shared_lock lock(mu_);
  std::vector<std::string> out;
  const std::string prefix = std::string(ns) + ".";
  for (const auto& [id, _] : entries_) {
    if (id.size() > prefix.size() && id.compare(0, prefix.size(), prefix) == 0) {
      out.push_back(id);
    }
  }
  std::sort(out.begin(), out.end());
  return out;
}

const CapabilityEntry* Registry::find(std::string_view capability_id) const {
  std::shared_lock lock(mu_);
  if (auto it = entries_.find(std::string(capability_id)); it != entries_.end()) {
    return &it->second;
  }
  return nullptr;
}

const MesherEntry* Registry::find_mesher(std::string_view capability_id) const {
  const auto* entry = find(capability_id);
  if (!entry || entry->kind != CapabilityKind::Mesher) return nullptr;
  return std::get_if<MesherEntry>(&entry->payload);
}

void Registry::remove_plugin(std::string_view plugin_id) {
  std::unique_lock lock(mu_);
  for (auto it = entries_.begin(); it != entries_.end();) {
    if (it->second.plugin_id == plugin_id) {
      it = entries_.erase(it);
    } else {
      ++it;
    }
  }
}

souxmar_status_t Registry::add_mesher_c(std::string_view                plugin_id,
                                        const char*                     capability_id,
                                        const souxmar_mesher_vtable_t*  vtable,
                                        void*                           user_data) noexcept {
  if (capability_id == nullptr) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT,
                                "souxmar_registry_add_mesher: capability_id is NULL");
  }
  // We catch any allocation failure that bubbles out of the C++ implementation
  // and translate it to a status code, since this function is noexcept and
  // crosses the C ABI boundary.
  try {
    auto result = add_mesher(capability_id, std::string(plugin_id), vtable, user_data);
    if (std::holds_alternative<RegistryError>(result)) {
      // Use a static thread-local for the message; the C-side contract says
      // the pointer is valid until the next call into the same plugin/host
      // function from the same thread.
      thread_local std::string last_error;
      last_error = std::get<RegistryError>(result).message;
      return souxmar_status_error(SOUXMAR_E_PLUGIN_REJECTED, last_error.c_str());
    }
    return souxmar_status_ok();
  } catch (const std::bad_alloc&) {
    return souxmar_status_error(SOUXMAR_E_OUT_OF_MEMORY,
                                "souxmar_registry_add_mesher: out of memory");
  } catch (...) {
    return souxmar_status_error(SOUXMAR_E_INTERNAL,
                                "souxmar_registry_add_mesher: unknown C++ exception");
  }
}

}  // namespace souxmar::plugin

// -------- C ABI surface --------
//
// `souxmar_registry_t` is opaque to plugin authors; we treat it as a Registry*
// on the host side. This is the canonical C-ABI bridging pattern: the C type
// is defined in C-only translation units, the C++ class lives behind it, and
// the cast is centralised in this single translation unit.

extern "C" {

souxmar_status_t
souxmar_registry_add_mesher(souxmar_registry_t*             registry,
                            const char*                     capability_id,
                            const souxmar_mesher_vtable_t*  vtable,
                            void*                           user_data) {
  if (registry == nullptr) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT,
                                "souxmar_registry_add_mesher: registry is NULL");
  }
  auto* reg = reinterpret_cast<souxmar::plugin::Registry*>(registry);
  return reg->add_mesher_c(reg->current_plugin_id_, capability_id, vtable, user_data);
}

}  // extern "C"
