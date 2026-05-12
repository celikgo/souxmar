// SPDX-License-Identifier: Apache-2.0
//
// Per-element-type local face tables. See header for the contract.

#include "souxmar/core/face_topology.h"

namespace souxmar::core {

namespace {

// Tet4 — 4 triangular faces (opposite-vertex convention).
constexpr LocalFace kTet4Faces[4] = {
    {3, {{1, 2, 3, 0}}},  // opposite v0
    {3, {{0, 3, 2, 0}}},  // opposite v1
    {3, {{0, 1, 3, 0}}},  // opposite v2
    {3, {{0, 2, 1, 0}}},  // opposite v3
};

// Hex8 — 6 quadrilateral faces; VTK_HEXAHEDRON / souxmar internal
// convention. v[0..3] bottom (z=0, CCW from above), v[4..7] top
// (z=1), v[i+4] stacked above v[i]. CCW from outside the cell.
constexpr LocalFace kHex8Faces[6] = {
    {4, {{0, 3, 2, 1}}},  // -z (bottom)
    {4, {{4, 5, 6, 7}}},  // +z (top)
    {4, {{0, 1, 5, 4}}},  // -y (front)
    {4, {{3, 7, 6, 2}}},  // +y (back)
    {4, {{0, 4, 7, 3}}},  // -x (left)
    {4, {{1, 2, 6, 5}}},  // +x (right)
};

// Prism6 — 2 triangular caps + 3 quadrilateral sides.
// v[0..2] bottom triangle, v[3..5] top, v[i+3] above v[i].
constexpr LocalFace kPrism6Faces[5] = {
    {3, {{0, 2, 1, 0}}},  // -z (bottom triangle)
    {3, {{3, 4, 5, 0}}},  // +z (top triangle)
    {4, {{0, 1, 4, 3}}},  // side 0-1
    {4, {{1, 2, 5, 4}}},  // side 1-2
    {4, {{2, 0, 3, 5}}},  // side 2-0
};

// Pyramid5 — 1 quadrilateral base + 4 triangular sides meeting at
// the apex. v[0..3] base (z=0, CCW from above), v[4] apex.
constexpr LocalFace kPyramid5Faces[5] = {
    {4, {{0, 3, 2, 1}}},  // -z (base quad)
    {3, {{0, 1, 4, 0}}},  // side 0-1
    {3, {{1, 2, 4, 0}}},  // side 1-2
    {3, {{2, 3, 4, 0}}},  // side 2-3
    {3, {{3, 0, 4, 0}}},  // side 3-0
};

// Tri3 — the cell IS its single "face". v[0..2] CCW from above.
constexpr LocalFace kTri3Faces[1] = {
    {3, {{0, 1, 2, 0}}},
};

// Quad4 — the cell IS its single "face". v[0..3] CCW from above.
constexpr LocalFace kQuad4Faces[1] = {
    {4, {{0, 1, 2, 3}}},
};

}  // namespace

std::span<const LocalFace> face_node_table(ElementType type) noexcept {
  switch (type) {
    case ElementType::Tet4:
      return {kTet4Faces, 4};
    case ElementType::Hex8:
      return {kHex8Faces, 6};
    case ElementType::Prism6:
      return {kPrism6Faces, 5};
    case ElementType::Pyramid5:
      return {kPyramid5Faces, 5};
    case ElementType::Tri3:
      return {kTri3Faces, 1};
    case ElementType::Quad4:
      return {kQuad4Faces, 1};
    default:
      return {};
  }
}

}  // namespace souxmar::core
