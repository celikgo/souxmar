// SPDX-License-Identifier: Apache-2.0
//
// Per-element-type local face tables.
//
// Maps an ElementType to the canonical list of its bounding faces, each
// face expressed as the local-cell-vertex indices the face touches.
// Face orderings are CCW from outside the cell, matching the Gmsh / VTK
// / OpenFOAM polyMesh side-set convention. This is the same set of
// tables openfoam-solver carries plugin-side (kTet4Faces et al. in
// examples/plugins/openfoam-solver/openfoam_solver.cpp); ADR-0012's
// pre-mortem named centralisation as a follow-up, and this header is
// it. The plugin keeps its own copy because plugins load independently
// of libsouxmar-core; host-side consumers (surface_stream, future
// mesh-viz, future BC-from-face helpers) share this one.
//
// Coverage: linear 3D (Tet4, Hex8, Prism6, Pyramid5) and linear 2D
// (Tri3, Quad4 — both report themselves as their single "face").
// Quadratic variants return an empty span; callers are expected to
// either skip them or lower to the linear corner set themselves.

#pragma once

#include "souxmar/core/element_type.h"

#include <array>
#include <cstdint>
#include <span>

namespace souxmar::core {

struct LocalFace {
  // 3 for triangular faces, 4 for quadrilateral faces.
  std::uint8_t vertex_count = 0;
  // Cell-local vertex indices, listed CCW from outside the cell.
  // Slots [vertex_count..3] are unused for triangular faces.
  std::array<std::uint8_t, 4> cell_local_idx{{0, 0, 0, 0}};
};

// Returns the per-element-type face table. Empty span for element
// types without face decomposition support in v1 (Vertex, Edge*,
// quadratic variants).
[[nodiscard]] std::span<const LocalFace> face_node_table(ElementType type) noexcept;

}  // namespace souxmar::core
