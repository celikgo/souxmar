// SPDX-License-Identifier: Apache-2.0
//
// Strongly-typed identifiers used throughout the data model.
//
// Why strong types: indices, tags, and counts have the same underlying integer
// representation but different semantics. Letting the compiler enforce that
// you cannot pass a CellIndex where a NodeIndex is expected catches whole
// classes of bug before they reach a test, let alone a user.

#pragma once

#include <cstdint>
#include <functional>

namespace souxmar::core {

// EntityTag — the integer identifier inherited from CAD geometry by every
// derived mesh cell, attached to BCs and materials. -1 means "untagged".
struct EntityTag {
  std::int32_t value{-1};

  [[nodiscard]] constexpr bool valid() const noexcept {
    return value >= 0;
  }

  [[nodiscard]] constexpr bool operator==(const EntityTag&) const noexcept = default;
  [[nodiscard]] constexpr auto operator<=>(const EntityTag&) const noexcept = default;
};

// NodeIndex — index of a node in a Mesh's node array.
struct NodeIndex {
  std::uint64_t value{0};

  [[nodiscard]] constexpr bool operator==(const NodeIndex&) const noexcept = default;
  [[nodiscard]] constexpr auto operator<=>(const NodeIndex&) const noexcept = default;
};

// CellIndex — index of a cell in a Mesh's cell array.
struct CellIndex {
  std::uint64_t value{0};

  [[nodiscard]] constexpr bool operator==(const CellIndex&) const noexcept = default;
  [[nodiscard]] constexpr auto operator<=>(const CellIndex&) const noexcept = default;
};

// VertexIndex / EdgeIndex / FaceIndex / SolidIndex — per-kind indices inside
// a Geometry. Kept separate from mesh indices because conflating them is the
// single most common bug in this domain.
struct VertexIndex {
  std::uint32_t value{0};
  [[nodiscard]] constexpr bool operator==(const VertexIndex&) const noexcept = default;
  [[nodiscard]] constexpr auto operator<=>(const VertexIndex&) const noexcept = default;
};

struct EdgeIndex {
  std::uint32_t value{0};
  [[nodiscard]] constexpr bool operator==(const EdgeIndex&) const noexcept = default;
  [[nodiscard]] constexpr auto operator<=>(const EdgeIndex&) const noexcept = default;
};

struct FaceIndex {
  std::uint32_t value{0};
  [[nodiscard]] constexpr bool operator==(const FaceIndex&) const noexcept = default;
  [[nodiscard]] constexpr auto operator<=>(const FaceIndex&) const noexcept = default;
};

struct SolidIndex {
  std::uint32_t value{0};
  [[nodiscard]] constexpr bool operator==(const SolidIndex&) const noexcept = default;
  [[nodiscard]] constexpr auto operator<=>(const SolidIndex&) const noexcept = default;
};

}  // namespace souxmar::core

// std::hash specialisations so these types work as keys in associative
// containers without callers writing the boilerplate.
namespace std {

template <>
struct hash<souxmar::core::EntityTag> {
  size_t operator()(const souxmar::core::EntityTag& t) const noexcept {
    return hash<int32_t>{}(t.value);
  }
};

template <>
struct hash<souxmar::core::NodeIndex> {
  size_t operator()(const souxmar::core::NodeIndex& i) const noexcept {
    return hash<uint64_t>{}(i.value);
  }
};

template <>
struct hash<souxmar::core::CellIndex> {
  size_t operator()(const souxmar::core::CellIndex& i) const noexcept {
    return hash<uint64_t>{}(i.value);
  }
};

}  // namespace std
