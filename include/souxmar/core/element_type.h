// SPDX-License-Identifier: Apache-2.0
//
// Element type taxonomy and per-type metadata.
//
// Numeric values are STABLE — they appear in serialised mesh files and in the
// C plugin ABI. Adding a new element type APPENDS; renumbering is forbidden.

#pragma once

#include <cstdint>
#include <string_view>

namespace souxmar::core {

enum class ElementType : std::uint16_t {
  Unknown    = 0,
  Vertex     = 1,
  Edge2      = 2,   // 2-node linear edge
  Edge3      = 3,   // 3-node quadratic edge
  Tri3       = 4,   // 3-node linear triangle
  Tri6       = 5,   // 6-node quadratic triangle
  Quad4      = 6,   // 4-node linear quadrilateral
  Quad8      = 7,   // 8-node quadratic quadrilateral (serendipity)
  Quad9      = 8,   // 9-node quadratic quadrilateral (Lagrange)
  Tet4       = 9,   // 4-node linear tetrahedron
  Tet10      = 10,  // 10-node quadratic tetrahedron
  Hex8       = 11,  // 8-node linear hexahedron
  Hex20      = 12,  // 20-node quadratic hexahedron (serendipity)
  Hex27      = 13,  // 27-node quadratic hexahedron (Lagrange)
  Prism6     = 14,  // 6-node linear wedge
  Prism15    = 15,  // 15-node quadratic wedge
  Pyramid5   = 16,  // 5-node linear pyramid
  Pyramid13  = 17,  // 13-node quadratic pyramid
};

// Per-type metadata. constexpr so callers can use it in concept/static_assert.
struct ElementInfo {
  std::uint8_t  dimension;   // 0..3
  std::uint8_t  num_nodes;
  std::uint8_t  order;       // 1 = linear, 2 = quadratic
  std::string_view name;     // canonical short name, e.g. "tet4"
};

[[nodiscard]] constexpr ElementInfo info(ElementType t) noexcept {
  switch (t) {
    case ElementType::Vertex:     return {0,  1, 1, "vertex"};
    case ElementType::Edge2:      return {1,  2, 1, "edge2"};
    case ElementType::Edge3:      return {1,  3, 2, "edge3"};
    case ElementType::Tri3:       return {2,  3, 1, "tri3"};
    case ElementType::Tri6:       return {2,  6, 2, "tri6"};
    case ElementType::Quad4:      return {2,  4, 1, "quad4"};
    case ElementType::Quad8:      return {2,  8, 2, "quad8"};
    case ElementType::Quad9:      return {2,  9, 2, "quad9"};
    case ElementType::Tet4:       return {3,  4, 1, "tet4"};
    case ElementType::Tet10:      return {3, 10, 2, "tet10"};
    case ElementType::Hex8:       return {3,  8, 1, "hex8"};
    case ElementType::Hex20:      return {3, 20, 2, "hex20"};
    case ElementType::Hex27:      return {3, 27, 2, "hex27"};
    case ElementType::Prism6:     return {3,  6, 1, "prism6"};
    case ElementType::Prism15:    return {3, 15, 2, "prism15"};
    case ElementType::Pyramid5:   return {3,  5, 1, "pyramid5"};
    case ElementType::Pyramid13:  return {3, 13, 2, "pyramid13"};
    case ElementType::Unknown:    return {0,  0, 0, "unknown"};
  }
  return {0, 0, 0, "unknown"};
}

[[nodiscard]] constexpr std::uint8_t dimension(ElementType t) noexcept {
  return info(t).dimension;
}

[[nodiscard]] constexpr std::uint8_t num_nodes(ElementType t) noexcept {
  return info(t).num_nodes;
}

[[nodiscard]] constexpr std::uint8_t order(ElementType t) noexcept {
  return info(t).order;
}

[[nodiscard]] constexpr std::string_view name(ElementType t) noexcept {
  return info(t).name;
}

}  // namespace souxmar::core
