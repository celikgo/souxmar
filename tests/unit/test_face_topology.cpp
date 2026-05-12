// SPDX-License-Identifier: Apache-2.0

#include "souxmar/core/element_type.h"
#include "souxmar/core/face_topology.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <set>

using namespace souxmar::core;

TEST(FaceTopology, Tet4HasFourTriangularFaces) {
  const auto t = face_node_table(ElementType::Tet4);
  ASSERT_EQ(t.size(), 4u);
  for (const auto& f : t) {
    EXPECT_EQ(f.vertex_count, 3u);
    for (std::uint8_t i = 0; i < f.vertex_count; ++i) {
      EXPECT_LT(f.cell_local_idx[i], 4u);
    }
  }
}

TEST(Tet4Faces, EveryVertexAppearsInExactlyThreeFaces) {
  // Each Tet4 vertex is on exactly 3 of the 4 faces (the one opposite
  // it is the missing one). Verifies the canonical opposite-vertex
  // table without rewriting it.
  const auto t = face_node_table(ElementType::Tet4);
  std::array<int, 4> count{{0, 0, 0, 0}};
  for (const auto& f : t) {
    for (std::uint8_t i = 0; i < f.vertex_count; ++i) {
      count[f.cell_local_idx[i]]++;
    }
  }
  EXPECT_EQ(count[0], 3);
  EXPECT_EQ(count[1], 3);
  EXPECT_EQ(count[2], 3);
  EXPECT_EQ(count[3], 3);
}

TEST(FaceTopology, Hex8HasSixQuadFaces) {
  const auto t = face_node_table(ElementType::Hex8);
  ASSERT_EQ(t.size(), 6u);
  for (const auto& f : t) {
    EXPECT_EQ(f.vertex_count, 4u);
    for (std::uint8_t i = 0; i < f.vertex_count; ++i) {
      EXPECT_LT(f.cell_local_idx[i], 8u);
    }
  }
}

TEST(Hex8Faces, EveryVertexAppearsInExactlyThreeFaces) {
  // Each Hex8 corner is on three faces (the three coordinate planes
  // meeting at that corner).
  const auto t = face_node_table(ElementType::Hex8);
  std::array<int, 8> count{};
  for (const auto& f : t) {
    for (std::uint8_t i = 0; i < f.vertex_count; ++i)
      count[f.cell_local_idx[i]]++;
  }
  for (int c : count)
    EXPECT_EQ(c, 3);
}

TEST(FaceTopology, Prism6HasTwoTrisAndThreeQuads) {
  const auto t = face_node_table(ElementType::Prism6);
  ASSERT_EQ(t.size(), 5u);
  int tris = 0, quads = 0;
  for (const auto& f : t) {
    if (f.vertex_count == 3)
      ++tris;
    else if (f.vertex_count == 4)
      ++quads;
  }
  EXPECT_EQ(tris, 2);
  EXPECT_EQ(quads, 3);
}

TEST(FaceTopology, Pyramid5HasOneQuadAndFourTris) {
  const auto t = face_node_table(ElementType::Pyramid5);
  ASSERT_EQ(t.size(), 5u);
  int tris = 0, quads = 0;
  for (const auto& f : t) {
    if (f.vertex_count == 3)
      ++tris;
    else if (f.vertex_count == 4)
      ++quads;
  }
  EXPECT_EQ(tris, 4);
  EXPECT_EQ(quads, 1);
}

TEST(FaceTopology, Tri3IsItsOwnSingleFace) {
  const auto t = face_node_table(ElementType::Tri3);
  ASSERT_EQ(t.size(), 1u);
  EXPECT_EQ(t[0].vertex_count, 3u);
  EXPECT_EQ(t[0].cell_local_idx[0], 0u);
  EXPECT_EQ(t[0].cell_local_idx[1], 1u);
  EXPECT_EQ(t[0].cell_local_idx[2], 2u);
}

TEST(FaceTopology, Quad4IsItsOwnSingleFace) {
  const auto t = face_node_table(ElementType::Quad4);
  ASSERT_EQ(t.size(), 1u);
  EXPECT_EQ(t[0].vertex_count, 4u);
}

TEST(FaceTopology, QuadraticVariantsReturnEmptySpan) {
  EXPECT_TRUE(face_node_table(ElementType::Tet10).empty());
  EXPECT_TRUE(face_node_table(ElementType::Hex20).empty());
  EXPECT_TRUE(face_node_table(ElementType::Hex27).empty());
  EXPECT_TRUE(face_node_table(ElementType::Prism15).empty());
  EXPECT_TRUE(face_node_table(ElementType::Pyramid13).empty());
  EXPECT_TRUE(face_node_table(ElementType::Tri6).empty());
  EXPECT_TRUE(face_node_table(ElementType::Quad8).empty());
  EXPECT_TRUE(face_node_table(ElementType::Quad9).empty());
}

TEST(FaceTopology, NonFaceElementsReturnEmptySpan) {
  EXPECT_TRUE(face_node_table(ElementType::Vertex).empty());
  EXPECT_TRUE(face_node_table(ElementType::Edge2).empty());
  EXPECT_TRUE(face_node_table(ElementType::Edge3).empty());
  EXPECT_TRUE(face_node_table(ElementType::Unknown).empty());
}

TEST(FaceTopology, FaceCountMatchesElementInfo) {
  // The face count this header reports for 3D elements must match
  // num_faces() in element_type.h — the two surfaces are independent
  // sources of truth and divergence is a real bug.
  EXPECT_EQ(face_node_table(ElementType::Tet4).size(), num_faces(ElementType::Tet4));
  EXPECT_EQ(face_node_table(ElementType::Hex8).size(), num_faces(ElementType::Hex8));
  EXPECT_EQ(face_node_table(ElementType::Prism6).size(), num_faces(ElementType::Prism6));
  EXPECT_EQ(face_node_table(ElementType::Pyramid5).size(), num_faces(ElementType::Pyramid5));
}
