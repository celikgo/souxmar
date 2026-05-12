// SPDX-License-Identifier: Apache-2.0

#include "souxmar/core/mesh.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <stdexcept>
#include <vector>

using namespace souxmar::core;

namespace {

// Build a 2-tet "bipyramid": two tetrahedra sharing a face.
// Used by several tests so it is a fixture.
Mesh MakeTwoTets() {
  Mesh m;
  m.add_node({0, 0, 0});  // 0
  m.add_node({1, 0, 0});  // 1
  m.add_node({0, 1, 0});  // 2
  m.add_node({0, 0, 1});  // 3
  m.add_node({1, 1, 1});  // 4 (shared apex above)

  const std::array<NodeIndex, 4> tet0{{NodeIndex{0}, NodeIndex{1}, NodeIndex{2}, NodeIndex{3}}};
  const std::array<NodeIndex, 4> tet1{{NodeIndex{1}, NodeIndex{2}, NodeIndex{3}, NodeIndex{4}}};
  m.add_cell(ElementType::Tet4, tet0, EntityTag{10});
  m.add_cell(ElementType::Tet4, tet1, EntityTag{20});
  return m;
}

TEST(Mesh, FreshIsEmpty) {
  Mesh m;
  EXPECT_TRUE(m.empty());
  EXPECT_EQ(m.num_nodes(), 0u);
  EXPECT_EQ(m.num_cells(), 0u);
}

TEST(Mesh, NodeAddAndReadRoundtrip) {
  Mesh m;
  const auto i = m.add_node({1.5, 2.5, 3.5});
  EXPECT_EQ(i.value, 0u);
  const auto pos = m.node(i);
  EXPECT_DOUBLE_EQ(pos[0], 1.5);
  EXPECT_DOUBLE_EQ(pos[1], 2.5);
  EXPECT_DOUBLE_EQ(pos[2], 3.5);
}

TEST(Mesh, FlatNodeBufferIsContiguous) {
  Mesh m;
  m.add_node({1, 2, 3});
  m.add_node({4, 5, 6});
  const auto flat = m.nodes_flat();
  ASSERT_EQ(flat.size(), 6u);
  EXPECT_DOUBLE_EQ(flat[0], 1);
  EXPECT_DOUBLE_EQ(flat[1], 2);
  EXPECT_DOUBLE_EQ(flat[2], 3);
  EXPECT_DOUBLE_EQ(flat[3], 4);
  EXPECT_DOUBLE_EQ(flat[4], 5);
  EXPECT_DOUBLE_EQ(flat[5], 6);
}

TEST(Mesh, CellTypeNodeCountValidation) {
  Mesh m;
  m.add_node({0, 0, 0});
  m.add_node({1, 0, 0});
  m.add_node({0, 1, 0});
  // Tet4 expects 4 nodes; we only give 3.
  std::vector<NodeIndex> three{NodeIndex{0}, NodeIndex{1}, NodeIndex{2}};
  EXPECT_THROW(m.add_cell(ElementType::Tet4, three), std::invalid_argument);
}

TEST(Mesh, CellNodesReferenceMustExist) {
  Mesh m;
  m.add_node({0, 0, 0});
  std::vector<NodeIndex> bad{NodeIndex{0}, NodeIndex{1}};  // node 1 doesn't exist
  EXPECT_THROW(m.add_cell(ElementType::Edge2, bad), std::out_of_range);
}

TEST(Mesh, TwoTetsAreCorrectlyShaped) {
  const auto m = MakeTwoTets();
  EXPECT_EQ(m.num_nodes(), 5u);
  EXPECT_EQ(m.num_cells(), 2u);
  EXPECT_EQ(m.cell_type(CellIndex{0}), ElementType::Tet4);
  EXPECT_EQ(m.cell_type(CellIndex{1}), ElementType::Tet4);
  EXPECT_EQ(m.cell_tag(CellIndex{0}), EntityTag{10});
  EXPECT_EQ(m.cell_tag(CellIndex{1}), EntityTag{20});
  EXPECT_EQ(m.cell_nodes(CellIndex{0}).size(), 4u);
  EXPECT_EQ(m.cell_nodes(CellIndex{1}).size(), 4u);
}

TEST(Mesh, ElementHistogramCountsTypes) {
  Mesh m;
  m.add_node({0, 0, 0});
  m.add_node({1, 0, 0});
  m.add_node({0, 1, 0});
  m.add_node({0, 0, 1});
  m.add_node({1, 1, 1});

  std::array<NodeIndex, 4> tet{{NodeIndex{0}, NodeIndex{1}, NodeIndex{2}, NodeIndex{3}}};
  std::array<NodeIndex, 3> tri{{NodeIndex{0}, NodeIndex{1}, NodeIndex{4}}};

  m.add_cell(ElementType::Tet4, tet);
  m.add_cell(ElementType::Tet4, tet);
  m.add_cell(ElementType::Tri3, tri);

  const auto hist = m.element_histogram();
  ASSERT_EQ(hist.size(), 2u);
  // Histogram is in canonical numeric ElementType order: Tri3 (4) before Tet4 (9).
  EXPECT_EQ(hist[0].first, ElementType::Tri3);
  EXPECT_EQ(hist[0].second, 1u);
  EXPECT_EQ(hist[1].first, ElementType::Tet4);
  EXPECT_EQ(hist[1].second, 2u);
}

TEST(Mesh, BoundingBoxOfTwoTets) {
  const auto m = MakeTwoTets();
  const auto box = m.bounding_box();
  EXPECT_DOUBLE_EQ(box[0], 0);
  EXPECT_DOUBLE_EQ(box[1], 0);
  EXPECT_DOUBLE_EQ(box[2], 0);
  EXPECT_DOUBLE_EQ(box[3], 1);
  EXPECT_DOUBLE_EQ(box[4], 1);
  EXPECT_DOUBLE_EQ(box[5], 1);
}

TEST(Mesh, MoveTransfersStorage) {
  auto m1 = MakeTwoTets();
  Mesh m2 = std::move(m1);
  EXPECT_EQ(m2.num_nodes(), 5u);
  EXPECT_EQ(m2.num_cells(), 2u);
}

TEST(Mesh, ReserveDoesNotChangeSize) {
  Mesh m;
  m.reserve_nodes(1000);
  m.reserve_cells(500);
  EXPECT_EQ(m.num_nodes(), 0u);
  EXPECT_EQ(m.num_cells(), 0u);
}

TEST(Mesh, MixedElementTypesInOneMesh) {
  Mesh m;
  m.add_node({0, 0, 0});
  m.add_node({1, 0, 0});
  m.add_node({0, 1, 0});
  m.add_node({0, 0, 1});
  m.add_node({1, 1, 0});
  m.add_node({0, 1, 1});
  m.add_node({1, 1, 1});
  m.add_node({1, 0, 1});

  std::array<NodeIndex, 4> tet{{NodeIndex{0}, NodeIndex{1}, NodeIndex{2}, NodeIndex{3}}};
  std::array<NodeIndex, 8> hex{{NodeIndex{0},
                                NodeIndex{1},
                                NodeIndex{4},
                                NodeIndex{2},
                                NodeIndex{3},
                                NodeIndex{7},
                                NodeIndex{6},
                                NodeIndex{5}}};
  m.add_cell(ElementType::Tet4, tet);
  m.add_cell(ElementType::Hex8, hex);

  EXPECT_EQ(m.cell_type(CellIndex{0}), ElementType::Tet4);
  EXPECT_EQ(m.cell_type(CellIndex{1}), ElementType::Hex8);
  EXPECT_EQ(m.cell_nodes(CellIndex{0}).size(), 4u);
  EXPECT_EQ(m.cell_nodes(CellIndex{1}).size(), 8u);
}

// -------- Per-face tags (ADR-0012, ABI v1.3) --------

TEST(MeshFaceTags, FreshMeshAllFacesUntagged) {
  const auto m = MakeTwoTets();
  // Tet4 has 4 faces; both cells should report -1 on every face slot.
  for (std::uint64_t cell = 0; cell < m.num_cells(); ++cell) {
    for (std::uint8_t f = 0; f < 4; ++f) {
      EXPECT_EQ(m.face_tag(CellIndex{cell}, f).value, -1);
    }
  }
  EXPECT_TRUE(m.tagged_faces().empty());
}

TEST(MeshFaceTags, SetAndReadBackRoundtrip) {
  auto m = MakeTwoTets();
  m.set_face_tag(CellIndex{0}, 2, EntityTag{77});
  EXPECT_EQ(m.face_tag(CellIndex{0}, 2).value, 77);
  // Untouched slots stay untagged.
  EXPECT_EQ(m.face_tag(CellIndex{0}, 0).value, -1);
  EXPECT_EQ(m.face_tag(CellIndex{0}, 3).value, -1);
  EXPECT_EQ(m.face_tag(CellIndex{1}, 2).value, -1);
}

TEST(MeshFaceTags, OverwriteReplacesPreviousTag) {
  auto m = MakeTwoTets();
  m.set_face_tag(CellIndex{0}, 1, EntityTag{10});
  m.set_face_tag(CellIndex{0}, 1, EntityTag{20});
  EXPECT_EQ(m.face_tag(CellIndex{0}, 1).value, 20);
}

TEST(MeshFaceTags, ClearWithUntaggedSentinelDropsSlot) {
  auto m = MakeTwoTets();
  m.set_face_tag(CellIndex{0}, 1, EntityTag{5});
  EXPECT_EQ(m.tagged_faces().size(), 1u);
  m.set_face_tag(CellIndex{0}, 1, EntityTag{-1});
  EXPECT_EQ(m.face_tag(CellIndex{0}, 1).value, -1);
  EXPECT_TRUE(m.tagged_faces().empty());
}

TEST(MeshFaceTags, OutOfRangeCellThrows) {
  auto m = MakeTwoTets();
  EXPECT_THROW(m.set_face_tag(CellIndex{99}, 0, EntityTag{1}), std::out_of_range);
  // Getter is noexcept and returns the untagged sentinel.
  EXPECT_EQ(m.face_tag(CellIndex{99}, 0).value, -1);
}

TEST(MeshFaceTags, OutOfRangeLocalFaceIndexThrows) {
  auto m = MakeTwoTets();
  // Tet4 has only 4 faces (indices 0..3).
  EXPECT_THROW(m.set_face_tag(CellIndex{0}, 4, EntityTag{1}), std::invalid_argument);
  // Getter just returns untagged.
  EXPECT_EQ(m.face_tag(CellIndex{0}, 4).value, -1);
}

TEST(MeshFaceTags, TaggedFacesEnumerationCoversEveryTag) {
  auto m = MakeTwoTets();
  m.set_face_tag(CellIndex{0}, 0, EntityTag{100});
  m.set_face_tag(CellIndex{0}, 3, EntityTag{200});
  m.set_face_tag(CellIndex{1}, 1, EntityTag{300});

  auto entries = m.tagged_faces();
  ASSERT_EQ(entries.size(), 3u);
  // Order is unspecified; sort by (cell, local_face) for the comparison.
  std::sort(
      entries.begin(), entries.end(), [](const Mesh::TaggedFace& a, const Mesh::TaggedFace& b) {
        if (a.cell.value != b.cell.value)
          return a.cell.value < b.cell.value;
        return a.local_face < b.local_face;
      });
  EXPECT_EQ(entries[0].cell.value, 0u);
  EXPECT_EQ(entries[0].local_face, 0);
  EXPECT_EQ(entries[0].tag.value, 100);
  EXPECT_EQ(entries[1].cell.value, 0u);
  EXPECT_EQ(entries[1].local_face, 3);
  EXPECT_EQ(entries[1].tag.value, 200);
  EXPECT_EQ(entries[2].cell.value, 1u);
  EXPECT_EQ(entries[2].local_face, 1);
  EXPECT_EQ(entries[2].tag.value, 300);
}

TEST(MeshFaceTags, ElementTypeFaceCountMatchesTaxonomy) {
  // Spot-check num_faces() against the ADR-0012 contract.
  EXPECT_EQ(num_faces(ElementType::Vertex), 0);
  EXPECT_EQ(num_faces(ElementType::Edge2), 0);
  EXPECT_EQ(num_faces(ElementType::Tri3), 3);
  EXPECT_EQ(num_faces(ElementType::Quad4), 4);
  EXPECT_EQ(num_faces(ElementType::Tet4), 4);
  EXPECT_EQ(num_faces(ElementType::Hex8), 6);
  EXPECT_EQ(num_faces(ElementType::Prism6), 5);
  EXPECT_EQ(num_faces(ElementType::Pyramid5), 5);
  // Quadratic variants share their linear sibling's face count.
  EXPECT_EQ(num_faces(ElementType::Tet10), 4);
  EXPECT_EQ(num_faces(ElementType::Hex27), 6);
  EXPECT_EQ(num_faces(ElementType::Prism15), 5);
  EXPECT_EQ(num_faces(ElementType::Pyramid13), 5);
}

}  // namespace
