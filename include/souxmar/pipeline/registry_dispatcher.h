// SPDX-License-Identifier: Apache-2.0
//
// RegistryDispatcher — bridges the runner's IDispatcher to real plugins
// loaded into a Registry. Each Stage's capability id is looked up; the
// dispatcher routes by namespace prefix (mesher.* / solver.* / writer.*),
// extracts upstream handles by convention, and calls the plugin vtable
// inside the plugin host's crash-isolation guard.
//
// Convention for input keys (matches docs/ARCHITECTURE.md):
//   mesher.*  : optional `geometry` -> upstream Geometry handle
//   solver.*  : required `mesh`     -> upstream Mesh handle
//   writer.*  : required `mesh`     -> upstream Mesh handle
//               optional `field`    -> upstream Field handle
//
// Stage outputs: meshers produce Mesh; solvers produce Field; writers
// produce a Path (the file they wrote). Wrapped in StageOutput so the
// downstream stage's RegistryDispatcher knows what it received.

#pragma once

#include "souxmar/core/field.h"
#include "souxmar/core/geometry.h"
#include "souxmar/core/mesh.h"
#include "souxmar/pipeline/runner.h"
#include "souxmar/plugin/registry.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace souxmar::pipeline {

// Discriminated wrapper for stage outputs. Universal payload type for
// shared_ptr<void> threading through the runner. Plugins do not see this
// type; it lives between the dispatcher and the runner.
struct StageOutput {
  enum class Kind {
    None = 0,
    Mesh = 1,
    Geometry = 2,
    Field = 3,
    Path = 4,
  };

  Kind kind = Kind::None;
  std::shared_ptr<souxmar::core::Mesh> mesh;
  std::shared_ptr<souxmar::core::Geometry> geometry;
  std::shared_ptr<souxmar::core::Field> field;
  std::string path;
};

class RegistryDispatcher : public IDispatcher {
 public:
  explicit RegistryDispatcher(plugin::Registry& registry);

  DispatchResult dispatch(const DispatchContext& ctx) override;

  // Returned in the cache-key context string. Sprint 3 push 2 returns the
  // capability's ABI-version integer as a stand-in for plugin version;
  // real per-plugin version strings come with the plugin manifest in
  // Sprint 5 hardening.
  std::string plugin_version(std::string_view capability_id) override;

  // Used by the parallel runner to attribute capabilities back to their
  // owning plugin and to honor manifest-declared threading models.
  std::string plugin_id(std::string_view capability_id) override;
  ::souxmar::plugin::ThreadingModel plugin_threading(std::string_view capability_id) override;

 private:
  plugin::Registry& registry_;
};

// ---- StageOutput on-disk serialization -----------------------------------
//
// In Sprint 3 push 3 only Path-kind StageOutputs are persistable — Mesh,
// Geometry, and Field handles need plugin-side serialization (Sprint 5+).
// serialize_stage_output returns std::nullopt for non-persistable kinds;
// the runner treats that as "do not write to disk" and the cache simply
// stays in-memory for that stage.
//
// Wire format (Path):
//   uint8  kind      (4)
//   uint64 length    (path string length, little-endian)
//   bytes  path      (length bytes)
//
// Plug these into RunOptions::disk_backing.{serialize,deserialize}.

[[nodiscard]] std::optional<std::vector<std::uint8_t>> serialize_stage_output(
    const std::shared_ptr<void>& payload);

[[nodiscard]] std::shared_ptr<void> deserialize_stage_output(std::span<const std::uint8_t> blob);

}  // namespace souxmar::pipeline
