// SPDX-License-Identifier: Apache-2.0

#include "souxmar/core/geometry.h"

#include <algorithm>
#include <cassert>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace souxmar::core {

class Geometry::Impl {
 public:
  std::vector<std::array<double, 3>> vertices;

  // For non-vertex entities we currently store only bookkeeping. Future
  // sprints add per-kind geometric payloads behind this same façade.
  std::vector<std::int32_t> edge_count_pad;  // size acts as count
  std::vector<std::int32_t> face_count_pad;
  std::vector<std::int32_t> solid_count_pad;

  // (kind, index) -> tag / name. Sparse — most entities never get a name.
  struct EntityKey {
    EntityKind kind;
    std::uint32_t index;
    bool operator==(const EntityKey& other) const noexcept = default;
  };

  struct EntityKeyHash {
    std::size_t operator()(const EntityKey& k) const noexcept {
      return std::hash<std::uint64_t>{}((static_cast<std::uint64_t>(k.kind) << 32) | k.index);
    }
  };

  std::unordered_map<EntityKey, EntityTag, EntityKeyHash> tags;
  std::unordered_map<EntityKey, std::string, EntityKeyHash> names;

  // Adapter slot for native handles owned by an adapter (e.g. OCCT).
  void* adapter_data = nullptr;
  void (*adapter_deleter)(void*) = nullptr;

  ~Impl() {
    if (adapter_data && adapter_deleter) {
      adapter_deleter(adapter_data);
    }
  }
};

Geometry::Geometry() : impl_(std::make_unique<Impl>()) {}

Geometry::~Geometry() = default;
Geometry::Geometry(Geometry&&) noexcept = default;
Geometry& Geometry::operator=(Geometry&&) noexcept = default;

std::size_t Geometry::count(EntityKind kind) const noexcept {
  switch (kind) {
    case EntityKind::Vertex:
      return impl_->vertices.size();
    case EntityKind::Edge:
      return impl_->edge_count_pad.size();
    case EntityKind::Face:
      return impl_->face_count_pad.size();
    case EntityKind::Solid:
      return impl_->solid_count_pad.size();
  }
  return 0;
}

std::size_t Geometry::num_vertices() const noexcept {
  return count(EntityKind::Vertex);
}

std::size_t Geometry::num_edges() const noexcept {
  return count(EntityKind::Edge);
}

std::size_t Geometry::num_faces() const noexcept {
  return count(EntityKind::Face);
}

std::size_t Geometry::num_solids() const noexcept {
  return count(EntityKind::Solid);
}

std::array<double, 3> Geometry::vertex_position(VertexIndex v) const {
  if (v.value >= impl_->vertices.size()) {
    throw std::out_of_range("Geometry::vertex_position: index out of range");
  }
  return impl_->vertices[v.value];
}

EntityTag Geometry::tag(EntityRef ref) const noexcept {
  const Impl::EntityKey key{ref.kind, ref.index};
  if (auto it = impl_->tags.find(key); it != impl_->tags.end()) {
    return it->second;
  }
  return EntityTag{};
}

std::optional<std::string_view> Geometry::name(EntityRef ref) const {
  const Impl::EntityKey key{ref.kind, ref.index};
  if (auto it = impl_->names.find(key); it != impl_->names.end()) {
    return std::string_view{it->second};
  }
  return std::nullopt;
}

std::array<double, 6> Geometry::bounding_box() const {
  if (impl_->vertices.empty()) {
    return {0, 0, 0, 0, 0, 0};
  }
  constexpr double inf = std::numeric_limits<double>::infinity();
  std::array<double, 6> box{inf, inf, inf, -inf, -inf, -inf};
  for (const auto& v : impl_->vertices) {
    box[0] = std::min(box[0], v[0]);
    box[1] = std::min(box[1], v[1]);
    box[2] = std::min(box[2], v[2]);
    box[3] = std::max(box[3], v[0]);
    box[4] = std::max(box[4], v[1]);
    box[5] = std::max(box[5], v[2]);
  }
  return box;
}

bool Geometry::empty() const noexcept {
  return num_vertices() == 0 && num_edges() == 0 && num_faces() == 0 && num_solids() == 0;
}

VertexIndex Geometry::add_vertex(std::array<double, 3> position) {
  const auto idx = static_cast<std::uint32_t>(impl_->vertices.size());
  impl_->vertices.push_back(position);
  return VertexIndex{idx};
}

EdgeIndex Geometry::add_edge() {
  const auto idx = static_cast<std::uint32_t>(impl_->edge_count_pad.size());
  impl_->edge_count_pad.push_back(0);
  return EdgeIndex{idx};
}

FaceIndex Geometry::add_face() {
  const auto idx = static_cast<std::uint32_t>(impl_->face_count_pad.size());
  impl_->face_count_pad.push_back(0);
  return FaceIndex{idx};
}

SolidIndex Geometry::add_solid() {
  const auto idx = static_cast<std::uint32_t>(impl_->solid_count_pad.size());
  impl_->solid_count_pad.push_back(0);
  return SolidIndex{idx};
}

void Geometry::set_tag(EntityRef ref, EntityTag tag) {
  if (ref.index >= count(ref.kind)) {
    throw std::out_of_range("Geometry::set_tag: ref out of range for kind");
  }
  impl_->tags[Impl::EntityKey{ref.kind, ref.index}] = tag;
}

void Geometry::set_name(EntityRef ref, std::string name) {
  if (ref.index >= count(ref.kind)) {
    throw std::out_of_range("Geometry::set_name: ref out of range for kind");
  }
  impl_->names[Impl::EntityKey{ref.kind, ref.index}] = std::move(name);
}

void Geometry::set_adapter_data(void* data, void (*deleter)(void*)) noexcept {
  if (impl_->adapter_data && impl_->adapter_deleter) {
    impl_->adapter_deleter(impl_->adapter_data);
  }
  impl_->adapter_data = data;
  impl_->adapter_deleter = deleter;
}

void* Geometry::adapter_data() const noexcept {
  return impl_->adapter_data;
}

}  // namespace souxmar::core
