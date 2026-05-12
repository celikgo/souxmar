// SPDX-License-Identifier: Apache-2.0

#include "souxmar/core/element_type.h"
#include "souxmar/core/mesh.h"
#include "souxmar/core/surface_stream.h"
#include "souxmar/core/tag.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <unordered_set>

using namespace souxmar::core;

namespace {

Mesh MakeSingleTet() {
  Mesh m;
  m.add_node({0, 0, 0});  // 0
  m.add_node({1, 0, 0});  // 1
  m.add_node({0, 1, 0});  // 2
  m.add_node({0, 0, 1});  // 3
  const std::array<NodeIndex, 4> nodes{{NodeIndex{0}, NodeIndex{1}, NodeIndex{2}, NodeIndex{3}}};
  m.add_cell(ElementType::Tet4, nodes);
  return m;
}

// Two tets sharing one face (the triangle on nodes 1,2,3). Should expose
// 6 boundary triangles (8 total faces − 2 shared).
Mesh MakeTwoTets() {
  Mesh m;
  m.add_node({0, 0, 0});  // 0
  m.add_node({1, 0, 0});  // 1
  m.add_node({0, 1, 0});  // 2
  m.add_node({0, 0, 1});  // 3
  m.add_node({1, 1, 1});  // 4
  const std::array<NodeIndex, 4> tet0{{NodeIndex{0}, NodeIndex{1}, NodeIndex{2}, NodeIndex{3}}};
  const std::array<NodeIndex, 4> tet1{{NodeIndex{1}, NodeIndex{2}, NodeIndex{3}, NodeIndex{4}}};
  m.add_cell(ElementType::Tet4, tet0);
  m.add_cell(ElementType::Tet4, tet1);
  return m;
}

Mesh MakeSingleHex() {
  Mesh m;
  m.add_node({0, 0, 0});  // 0
  m.add_node({1, 0, 0});  // 1
  m.add_node({1, 1, 0});  // 2
  m.add_node({0, 1, 0});  // 3
  m.add_node({0, 0, 1});  // 4
  m.add_node({1, 0, 1});  // 5
  m.add_node({1, 1, 1});  // 6
  m.add_node({0, 1, 1});  // 7
  std::array<NodeIndex, 8> n;
  for (std::uint64_t i = 0; i < 8; ++i)
    n[i] = NodeIndex{i};
  m.add_cell(ElementType::Hex8, n);
  return m;
}

Mesh MakeSingleTri() {
  Mesh m;
  m.add_node({0, 0, 0});
  m.add_node({1, 0, 0});
  m.add_node({0, 1, 0});
  const std::array<NodeIndex, 3> n{{NodeIndex{0}, NodeIndex{1}, NodeIndex{2}}};
  m.add_cell(ElementType::Tri3, n);
  return m;
}

}  // namespace

TEST(SurfaceStream, EmptyMesh) {
  Mesh m;
  SurfaceStream s(m);
  EXPECT_EQ(s.vertex_count(), 0u);
  EXPECT_EQ(s.triangle_count(), 0u);
}

TEST(SurfaceStream, SingleTet_FourFacesFourVertices) {
  const Mesh m = MakeSingleTet();
  SurfaceStream s(m);
  EXPECT_EQ(s.vertex_count(), 4u);
  EXPECT_EQ(s.triangle_count(), 4u);

  // All four mesh nodes should appear exactly once in vertex_ids.
  const auto vids = s.vertex_ids();
  std::unordered_set<std::uint64_t> seen(vids.begin(), vids.end());
  EXPECT_EQ(seen.size(), 4u);
  for (std::uint64_t i = 0; i < 4; ++i)
    EXPECT_TRUE(seen.count(i));

  // Indices buffer is 3 entries per triangle.
  EXPECT_EQ(s.indices().size(), 12u);

  // Face IDs are 1-based and there are four distinct ones (one per face).
  std::unordered_set<std::uint32_t> fids(s.face_ids().begin(), s.face_ids().end());
  EXPECT_EQ(fids.size(), 4u);
  EXPECT_FALSE(fids.count(0));  // never the sentinel
}

TEST(SurfaceStream, TwoTets_SharedFaceIsInterior) {
  const Mesh m = MakeTwoTets();
  SurfaceStream s(m);
  // 8 total faces − 2 (the shared triangle counted from each tet) = 6 boundary.
  EXPECT_EQ(s.triangle_count(), 6u);
  // All five mesh nodes appear on the surface.
  EXPECT_EQ(s.vertex_count(), 5u);
}

TEST(SurfaceStream, SingleHex_TwelveTrianglesEightVertices) {
  const Mesh m = MakeSingleHex();
  SurfaceStream s(m);
  EXPECT_EQ(s.vertex_count(), 8u);
  // 6 quad faces × 2 triangles per quad = 12 triangles.
  EXPECT_EQ(s.triangle_count(), 12u);
  // Each pair of triangles from the same quad shares the same face_id, so
  // we should see exactly 6 distinct face_ids.
  std::unordered_set<std::uint32_t> fids(s.face_ids().begin(), s.face_ids().end());
  EXPECT_EQ(fids.size(), 6u);
}

TEST(SurfaceStream, SingleTri_DirectEmission) {
  const Mesh m = MakeSingleTri();
  SurfaceStream s(m);
  EXPECT_EQ(s.vertex_count(), 3u);
  EXPECT_EQ(s.triangle_count(), 1u);
}

TEST(SurfaceStream, BoundsMatchMeshBoundingBox) {
  const Mesh m = MakeSingleHex();
  SurfaceStream s(m);
  const auto bmin = s.bounds_min();
  const auto bmax = s.bounds_max();
  EXPECT_DOUBLE_EQ(bmin[0], 0.0);
  EXPECT_DOUBLE_EQ(bmin[1], 0.0);
  EXPECT_DOUBLE_EQ(bmin[2], 0.0);
  EXPECT_DOUBLE_EQ(bmax[0], 1.0);
  EXPECT_DOUBLE_EQ(bmax[1], 1.0);
  EXPECT_DOUBLE_EQ(bmax[2], 1.0);
}

TEST(SurfaceStream, NormalsAreUnitLength) {
  const Mesh m = MakeSingleHex();
  SurfaceStream s(m);
  const auto normals = s.normals();
  ASSERT_EQ(normals.size(), s.vertex_count() * 3);
  for (std::size_t i = 0; i < s.vertex_count(); ++i) {
    const float nx = normals[3 * i + 0];
    const float ny = normals[3 * i + 1];
    const float nz = normals[3 * i + 2];
    const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
    EXPECT_NEAR(len, 1.0f, 1e-5f);
  }
}

TEST(SurfaceStream, SoABuffersHaveExpectedShape) {
  const Mesh m = MakeSingleTet();
  SurfaceStream s(m);
  EXPECT_EQ(s.positions().size(), s.vertex_count() * 3);
  EXPECT_EQ(s.normals().size(), s.vertex_count() * 3);
  EXPECT_EQ(s.indices().size(), s.triangle_count() * 3);
  EXPECT_EQ(s.face_ids().size(), s.triangle_count());
  EXPECT_EQ(s.vertex_ids().size(), s.vertex_count());
}

TEST(SurfaceStream, IndicesPointWithinVertexRange) {
  const Mesh m = MakeTwoTets();
  SurfaceStream s(m);
  for (const std::uint32_t idx : s.indices()) {
    EXPECT_LT(idx, s.vertex_count());
  }
}
