// SPDX-License-Identifier: Apache-2.0
//
// Mesh — mixed-element finite-element mesh in souxmar's data model.
//
// Storage is column-style: a flat node-coordinate buffer (3*N doubles), a
// per-cell type vector, a flat node-index buffer (offsets pre-computed), and
// a per-cell tag vector. This layout is contiguous, cache-friendly, and
// directly addressable across the C ABI boundary by mmap.
//
// At Sprint 1 the Mesh is in-memory only. Out-of-core / mmap-backed buffers
// land in Sprint 7 (see ROADMAP).

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "souxmar/core/element_type.h"
#include "souxmar/core/tag.h"

namespace souxmar::core {

class Mesh {
 public:
  Mesh();
  ~Mesh();

  Mesh(Mesh&&) noexcept;
  Mesh& operator=(Mesh&&) noexcept;

  Mesh(const Mesh&)            = delete;
  Mesh& operator=(const Mesh&) = delete;

  // -------- Capacity --------

  void reserve_nodes(std::size_t n);
  void reserve_cells(std::size_t n);

  // -------- Build API --------

  NodeIndex add_node(std::array<double, 3> position);

  // Add a cell of `type`, referencing `node_indices` (must have
  // num_nodes(type) entries). Optional `tag` records which Geometry / Topology
  // entity this cell descends from — preserved for BC and material lookup.
  CellIndex add_cell(ElementType                       type,
                     std::span<const NodeIndex>        node_indices,
                     EntityTag                         tag = {});

  // -------- Read access --------

  [[nodiscard]] std::size_t num_nodes() const noexcept;
  [[nodiscard]] std::size_t num_cells() const noexcept;

  [[nodiscard]] std::array<double, 3> node(NodeIndex index) const;
  [[nodiscard]] std::span<const double> nodes_flat() const noexcept;  // size = 3 * num_nodes

  [[nodiscard]] ElementType                cell_type(CellIndex index) const noexcept;
  [[nodiscard]] std::span<const NodeIndex> cell_nodes(CellIndex index) const;
  [[nodiscard]] EntityTag                  cell_tag(CellIndex index) const noexcept;

  // -------- Per-face tags (ADR-0012, ABI minor v1.3) --------
  //
  // Sparse: only explicitly-set face tags consume storage. An unset
  // face reads back as EntityTag{-1} (the same untagged sentinel
  // cell_tag uses for an uninitialised cell). Local face indices run
  // 0 .. num_faces(cell_type(cell)) - 1. The set_face_tag accessor
  // throws std::out_of_range if the cell index is out of range and
  // std::invalid_argument if the local face index exceeds the cell's
  // face count.

  [[nodiscard]] EntityTag face_tag(CellIndex cell,
                                   std::uint8_t local_face) const noexcept;
  void set_face_tag(CellIndex cell, std::uint8_t local_face, EntityTag tag);

  // Iterate every explicitly-tagged face. Each entry is
  // ((cell, local_face), tag). Order is unspecified — consumers that
  // need a stable order sort the returned vector themselves.
  struct TaggedFace {
    CellIndex     cell;
    std::uint8_t  local_face;
    EntityTag     tag;
  };
  [[nodiscard]] std::vector<TaggedFace> tagged_faces() const;

  // Histogram of element types present, in canonical numeric order.
  [[nodiscard]] std::vector<std::pair<ElementType, std::size_t>> element_histogram() const;

  // World-space bounding box from node positions; { xmin, ymin, zmin, xmax, ymax, zmax }.
  [[nodiscard]] std::array<double, 6> bounding_box() const;

  [[nodiscard]] bool empty() const noexcept;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace souxmar::core
