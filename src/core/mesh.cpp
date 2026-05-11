// SPDX-License-Identifier: Apache-2.0

#include "souxmar/core/mesh.h"

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace souxmar::core {

namespace {

// Sparse key for per-face tags: (cell index, local face index) packed
// into a single uint64_t so std::unordered_map's default hash works.
// Cell indices are bounded by num_cells (< 2^56 even on absurdly large
// meshes); local face indices are bounded by 6 (hex family). The pack
// reserves the low byte for local_face, leaving 56 bits for cell idx —
// plenty of headroom for the 1.x release series.
constexpr std::uint64_t pack_face_key(std::uint64_t cell,
                                      std::uint8_t  local_face) noexcept {
  return (cell << 8) | static_cast<std::uint64_t>(local_face);
}

}  // namespace

class Mesh::Impl {
 public:
  // Flat 3*N node-coordinate buffer.
  std::vector<double> nodes;

  // Per-cell type and tag.
  std::vector<ElementType> cell_types;
  std::vector<EntityTag>   cell_tags;

  // Cell-node offsets: cell_node_offsets[i] is the start of cell i's
  // node-index run in cell_node_indices; cell_node_offsets has size
  // num_cells + 1 so cell_node_offsets[num_cells] == cell_node_indices.size().
  std::vector<std::uint64_t> cell_node_offsets{0};
  std::vector<NodeIndex>     cell_node_indices;

  // Per-face tags (ADR-0012). Sparse: only explicitly-tagged faces
  // materialise. Empty for the default-untagged-everywhere case that
  // covers nearly every interior face of nearly every mesh.
  std::unordered_map<std::uint64_t, EntityTag> face_tags;
};

Mesh::Mesh() : impl_(std::make_unique<Impl>()) {}
Mesh::~Mesh() = default;
Mesh::Mesh(Mesh&&) noexcept = default;
Mesh& Mesh::operator=(Mesh&&) noexcept = default;

void Mesh::reserve_nodes(std::size_t n) {
  impl_->nodes.reserve(3 * n);
}

void Mesh::reserve_cells(std::size_t n) {
  impl_->cell_types.reserve(n);
  impl_->cell_tags.reserve(n);
  impl_->cell_node_offsets.reserve(n + 1);
  // We don't know average nodes-per-cell up front; reserve a reasonable
  // guess of 8 (covers Hex8, Quad8, the bulk of typical mixed meshes).
  impl_->cell_node_indices.reserve(8 * n);
}

NodeIndex Mesh::add_node(std::array<double, 3> position) {
  const auto idx = impl_->nodes.size() / 3;
  impl_->nodes.push_back(position[0]);
  impl_->nodes.push_back(position[1]);
  impl_->nodes.push_back(position[2]);
  return NodeIndex{idx};
}

CellIndex Mesh::add_cell(ElementType                       type,
                         std::span<const NodeIndex>        node_indices,
                         EntityTag                         tag) {
  const auto expected = num_nodes(type);
  if (node_indices.size() != expected) {
    throw std::invalid_argument(
        "Mesh::add_cell: node count does not match element type");
  }
  const auto num_existing_nodes = impl_->nodes.size() / 3;
  for (const auto idx : node_indices) {
    if (idx.value >= num_existing_nodes) {
      throw std::out_of_range("Mesh::add_cell: node index references missing node");
    }
  }

  const auto cell_idx = impl_->cell_types.size();
  impl_->cell_types.push_back(type);
  impl_->cell_tags.push_back(tag);
  impl_->cell_node_indices.insert(
      impl_->cell_node_indices.end(), node_indices.begin(), node_indices.end());
  impl_->cell_node_offsets.push_back(
      static_cast<std::uint64_t>(impl_->cell_node_indices.size()));
  return CellIndex{cell_idx};
}

std::size_t Mesh::num_nodes() const noexcept {
  return impl_->nodes.size() / 3;
}

std::size_t Mesh::num_cells() const noexcept {
  return impl_->cell_types.size();
}

std::array<double, 3> Mesh::node(NodeIndex index) const {
  if (index.value >= num_nodes()) {
    throw std::out_of_range("Mesh::node: index out of range");
  }
  const auto base = 3 * index.value;
  return {impl_->nodes[base], impl_->nodes[base + 1], impl_->nodes[base + 2]};
}

std::span<const double> Mesh::nodes_flat() const noexcept {
  return {impl_->nodes.data(), impl_->nodes.size()};
}

ElementType Mesh::cell_type(CellIndex index) const noexcept {
  if (index.value >= num_cells()) return ElementType::Unknown;
  return impl_->cell_types[index.value];
}

std::span<const NodeIndex> Mesh::cell_nodes(CellIndex index) const {
  if (index.value >= num_cells()) {
    throw std::out_of_range("Mesh::cell_nodes: cell index out of range");
  }
  const auto begin = impl_->cell_node_offsets[index.value];
  const auto end   = impl_->cell_node_offsets[index.value + 1];
  return {impl_->cell_node_indices.data() + begin,
          static_cast<std::size_t>(end - begin)};
}

EntityTag Mesh::cell_tag(CellIndex index) const noexcept {
  if (index.value >= num_cells()) return EntityTag{};
  return impl_->cell_tags[index.value];
}

EntityTag Mesh::face_tag(CellIndex cell, std::uint8_t local_face) const noexcept {
  if (cell.value >= num_cells()) return EntityTag{-1};
  const auto type = impl_->cell_types[cell.value];
  if (local_face >= num_faces(type)) return EntityTag{-1};
  const auto it = impl_->face_tags.find(pack_face_key(cell.value, local_face));
  if (it == impl_->face_tags.end()) return EntityTag{-1};
  return it->second;
}

void Mesh::set_face_tag(CellIndex cell, std::uint8_t local_face, EntityTag tag) {
  if (cell.value >= num_cells()) {
    throw std::out_of_range("Mesh::set_face_tag: cell index out of range");
  }
  const auto type = impl_->cell_types[cell.value];
  if (local_face >= num_faces(type)) {
    throw std::invalid_argument(
        "Mesh::set_face_tag: local_face index exceeds cell's face count");
  }
  const auto key = pack_face_key(cell.value, local_face);
  if (tag.value == -1) {
    impl_->face_tags.erase(key);
  } else {
    impl_->face_tags[key] = tag;
  }
}

std::vector<Mesh::TaggedFace> Mesh::tagged_faces() const {
  std::vector<TaggedFace> out;
  out.reserve(impl_->face_tags.size());
  for (const auto& [key, tag] : impl_->face_tags) {
    out.push_back(TaggedFace{
        CellIndex{key >> 8},
        static_cast<std::uint8_t>(key & 0xFFu),
        tag,
    });
  }
  return out;
}

std::vector<std::pair<ElementType, std::size_t>> Mesh::element_histogram() const {
  // std::map gives canonical numeric ordering.
  std::map<ElementType, std::size_t> bins;
  for (const auto t : impl_->cell_types) {
    ++bins[t];
  }
  return {bins.begin(), bins.end()};
}

std::array<double, 6> Mesh::bounding_box() const {
  if (impl_->nodes.empty()) return {0, 0, 0, 0, 0, 0};
  constexpr double inf = std::numeric_limits<double>::infinity();
  std::array<double, 6> box{inf, inf, inf, -inf, -inf, -inf};
  for (std::size_t i = 0; i < impl_->nodes.size(); i += 3) {
    box[0] = std::min(box[0], impl_->nodes[i + 0]);
    box[1] = std::min(box[1], impl_->nodes[i + 1]);
    box[2] = std::min(box[2], impl_->nodes[i + 2]);
    box[3] = std::max(box[3], impl_->nodes[i + 0]);
    box[4] = std::max(box[4], impl_->nodes[i + 1]);
    box[5] = std::max(box[5], impl_->nodes[i + 2]);
  }
  return box;
}

bool Mesh::empty() const noexcept {
  return num_nodes() == 0 && num_cells() == 0;
}

}  // namespace souxmar::core
