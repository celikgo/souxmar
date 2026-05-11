// SPDX-License-Identifier: Apache-2.0
//
// Capability registry — the host-side catalogue of every plugin function the
// orchestrator can dispatch to, keyed by capability id ("mesher.tetra.netgen",
// "solver.elasticity.linear", ...).
//
// The C plugin ABI bridges into this class through extern "C" wrappers in
// registry.cpp; plugin authors do not see this header.

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include "souxmar-c/mesher.h"
#include "souxmar-c/status.h"

namespace souxmar::plugin {

enum class CapabilityKind : std::uint8_t {
  Mesher = 0,
  // Sprint 3+: Solver, Element, Reader, Writer, Postproc.
};

struct MesherEntry {
  const souxmar_mesher_vtable_t* vtable;
  void*                          user_data;
};

struct CapabilityEntry {
  std::string     id;          // "mesher.tetra.netgen"
  std::string     plugin_id;   // owning plugin's manifest id
  CapabilityKind  kind;
  std::int32_t    abi_version; // taken from the vtable

  // Capability-specific payload, discriminated by `kind`.
  std::variant<MesherEntry> payload;
};

struct RegistryError {
  std::string message;
};

class Registry {
 public:
  Registry();
  ~Registry();

  Registry(Registry&&) noexcept;
  Registry& operator=(Registry&&) noexcept;

  Registry(const Registry&)            = delete;
  Registry& operator=(const Registry&) = delete;

  // -------- C++ API used by the loader and by tests --------

  // Register a mesher. Returns std::monostate on success or a RegistryError
  // describing why registration was refused (duplicate id, bad ABI, etc.).
  std::variant<std::monostate, RegistryError>
  add_mesher(std::string                     capability_id,
             std::string                     plugin_id,
             const souxmar_mesher_vtable_t*  vtable,
             void*                           user_data);

  // -------- Read access --------

  [[nodiscard]] std::size_t size() const noexcept;

  [[nodiscard]] std::vector<std::string> list_capabilities() const;
  [[nodiscard]] std::vector<std::string> list_capabilities_in_namespace(std::string_view ns) const;

  [[nodiscard]] const CapabilityEntry* find(std::string_view capability_id) const;

  // Convenience: typed lookup. Returns nullptr if not found OR not a mesher.
  [[nodiscard]] const MesherEntry* find_mesher(std::string_view capability_id) const;

  // Drop every capability owned by `plugin_id`. Used at plugin unload.
  void remove_plugin(std::string_view plugin_id);

  // -------- C ABI bridge --------
  //
  // Called by the extern "C" wrappers in registry.cpp. Not part of the
  // public C++ surface — kept here so the wrapper can avoid friend tricks.

  souxmar_status_t add_mesher_c(std::string_view                plugin_id,
                                const char*                     capability_id,
                                const souxmar_mesher_vtable_t*  vtable,
                                void*                           user_data) noexcept;

 private:
  // shared_mutex: read-mostly (the orchestrator queries; registration is rare).
  mutable std::shared_mutex                              mu_;
  std::unordered_map<std::string, CapabilityEntry>       entries_;

  // The plugin id currently being registered. Set by the loader before
  // calling souxmar_plugin_register_v1; consumed by add_mesher_c so the
  // C ABI surface does not need to thread plugin id through every call.
  std::string                                            current_plugin_id_;

  friend class PluginLoader;
};

}  // namespace souxmar::plugin
