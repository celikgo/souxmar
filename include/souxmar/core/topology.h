// SPDX-License-Identifier: Apache-2.0
//
// Topology — entity graph independent of geometry.
//
// Used when a Mesh has no underlying CAD model (e.g. raw STL surface mesh,
// or a procedurally-generated test mesh). Exposes the same EntityKind /
// EntityTag plumbing as Geometry so downstream code (BC application,
// material assignment, postprocessing) can treat both uniformly.

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "souxmar/core/tag.h"

namespace souxmar::core {

enum class TopologyKind : std::uint8_t {
  Vertex = 0,
  Edge   = 1,
  Face   = 2,
  Region = 3,
};

struct TopologyRef {
  TopologyKind   kind;
  std::uint32_t  index;

  [[nodiscard]] constexpr bool operator==(const TopologyRef&) const noexcept = default;
};

class Topology {
 public:
  Topology();
  ~Topology();

  Topology(Topology&&) noexcept;
  Topology& operator=(Topology&&) noexcept;

  Topology(const Topology&)            = delete;
  Topology& operator=(const Topology&) = delete;

  [[nodiscard]] std::size_t count(TopologyKind kind) const noexcept;

  [[nodiscard]] EntityTag tag(TopologyRef ref) const noexcept;
  [[nodiscard]] std::optional<std::string_view> name(TopologyRef ref) const;

  std::uint32_t add_entity(TopologyKind kind);
  void          set_tag(TopologyRef ref, EntityTag tag);
  void          set_name(TopologyRef ref, std::string name);

  [[nodiscard]] bool empty() const noexcept;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace souxmar::core
