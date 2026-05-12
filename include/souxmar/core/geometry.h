// SPDX-License-Identifier: Apache-2.0
//
// Geometry — souxmar's representation of CAD B-rep entities.
//
// At Sprint 1 this is a deliberately minimal façade: counts of each entity
// kind, vertex coordinates, and per-entity tags / names. Edge/face/solid
// geometric data is opaque (typically held in the OpenCASCADE adapter via the
// pimpl). Future sprints expose curve / surface / volume queries through
// stable accessors as the adapters require them.

#pragma once

#include "souxmar/core/tag.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace souxmar::core {

enum class EntityKind : std::uint8_t {
  Vertex = 0,
  Edge = 1,
  Face = 2,
  Solid = 3,
};

struct EntityRef {
  EntityKind kind;
  std::uint32_t index;

  [[nodiscard]] constexpr bool operator==(const EntityRef&) const noexcept = default;
};

class Geometry {
 public:
  Geometry();
  ~Geometry();

  Geometry(Geometry&&) noexcept;
  Geometry& operator=(Geometry&&) noexcept;

  Geometry(const Geometry&) = delete;
  Geometry& operator=(const Geometry&) = delete;

  // -------- Read access --------

  [[nodiscard]] std::size_t count(EntityKind kind) const noexcept;

  // Convenience accessors per kind.
  [[nodiscard]] std::size_t num_vertices() const noexcept;
  [[nodiscard]] std::size_t num_edges() const noexcept;
  [[nodiscard]] std::size_t num_faces() const noexcept;
  [[nodiscard]] std::size_t num_solids() const noexcept;

  // Vertex coordinates are public for every Geometry (raw points have no
  // adapter-specific representation).
  [[nodiscard]] std::array<double, 3> vertex_position(VertexIndex v) const;

  // Tag and name access. Returns -1 / nullopt for entities that have neither.
  [[nodiscard]] EntityTag tag(EntityRef ref) const noexcept;
  [[nodiscard]] std::optional<std::string_view> name(EntityRef ref) const;

  // Bounding box in world coordinates. Returns {0,0,0,0,0,0} if empty.
  // Order: { xmin, ymin, zmin, xmax, ymax, zmax }.
  [[nodiscard]] std::array<double, 6> bounding_box() const;

  [[nodiscard]] bool empty() const noexcept;

  // -------- Build API (used by adapters during import) --------

  VertexIndex add_vertex(std::array<double, 3> position);
  EdgeIndex add_edge();
  FaceIndex add_face();
  SolidIndex add_solid();

  void set_tag(EntityRef ref, EntityTag tag);
  void set_name(EntityRef ref, std::string name);

  // -------- Adapter escape hatch (DO NOT USE FROM USER CODE) --------
  //
  // Adapter implementations need to associate native handles (e.g. OCCT
  // TopoDS_Shape) with the Geometry's logical entities. This is exposed
  // through a void* "adapter slot" per entity kind, set/got opaquely.
  // Callers other than adapter implementations must not use this interface.

  void set_adapter_data(void* data, void (*deleter)(void*)) noexcept;
  [[nodiscard]] void* adapter_data() const noexcept;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace souxmar::core
