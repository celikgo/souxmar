// SPDX-License-Identifier: Apache-2.0

#include "souxmar/core/geometry.h"

#include <gtest/gtest.h>

#include <stdexcept>

using namespace souxmar::core;

namespace {

TEST(Geometry, FreshIsEmpty) {
  Geometry g;
  EXPECT_TRUE(g.empty());
  EXPECT_EQ(g.num_vertices(), 0u);
  EXPECT_EQ(g.num_edges(),    0u);
  EXPECT_EQ(g.num_faces(),    0u);
  EXPECT_EQ(g.num_solids(),   0u);
}

TEST(Geometry, AddVerticesReturnsSequentialIndices) {
  Geometry g;
  const auto v0 = g.add_vertex({0, 0, 0});
  const auto v1 = g.add_vertex({1, 0, 0});
  const auto v2 = g.add_vertex({0, 1, 0});
  EXPECT_EQ(v0.value, 0u);
  EXPECT_EQ(v1.value, 1u);
  EXPECT_EQ(v2.value, 2u);
  EXPECT_EQ(g.num_vertices(), 3u);
}

TEST(Geometry, VertexPositionRoundtrip) {
  Geometry g;
  const auto v = g.add_vertex({1.5, 2.5, 3.5});
  const auto pos = g.vertex_position(v);
  EXPECT_DOUBLE_EQ(pos[0], 1.5);
  EXPECT_DOUBLE_EQ(pos[1], 2.5);
  EXPECT_DOUBLE_EQ(pos[2], 3.5);
}

TEST(Geometry, VertexPositionOutOfRangeThrows) {
  Geometry g;
  EXPECT_THROW(g.vertex_position(VertexIndex{0}), std::out_of_range);
}

TEST(Geometry, AddNonVertexEntities) {
  Geometry g;
  const auto e = g.add_edge();
  const auto f = g.add_face();
  const auto s = g.add_solid();
  EXPECT_EQ(e.value, 0u);
  EXPECT_EQ(f.value, 0u);
  EXPECT_EQ(s.value, 0u);
  EXPECT_EQ(g.num_edges(),  1u);
  EXPECT_EQ(g.num_faces(),  1u);
  EXPECT_EQ(g.num_solids(), 1u);
}

TEST(Geometry, TagAndNameRoundtrip) {
  Geometry g;
  g.add_face();  // index 0
  EntityRef face_ref{EntityKind::Face, 0};

  EXPECT_FALSE(g.tag(face_ref).valid());
  EXPECT_FALSE(g.name(face_ref).has_value());

  g.set_tag(face_ref, EntityTag{42});
  g.set_name(face_ref, "clamped_face");

  EXPECT_EQ(g.tag(face_ref), EntityTag{42});
  ASSERT_TRUE(g.name(face_ref).has_value());
  EXPECT_EQ(*g.name(face_ref), "clamped_face");
}

TEST(Geometry, SetTagOnMissingRefThrows) {
  Geometry g;
  EXPECT_THROW(g.set_tag(EntityRef{EntityKind::Face, 0}, EntityTag{1}),
               std::out_of_range);
}

TEST(Geometry, BoundingBoxOfEmptyIsZero) {
  Geometry g;
  const auto box = g.bounding_box();
  for (auto v : box) EXPECT_DOUBLE_EQ(v, 0.0);
}

TEST(Geometry, BoundingBoxFromVertices) {
  Geometry g;
  g.add_vertex({-1, -2, -3});
  g.add_vertex({ 4,  5,  6});
  g.add_vertex({ 0,  0,  0});
  const auto box = g.bounding_box();
  EXPECT_DOUBLE_EQ(box[0], -1);
  EXPECT_DOUBLE_EQ(box[1], -2);
  EXPECT_DOUBLE_EQ(box[2], -3);
  EXPECT_DOUBLE_EQ(box[3],  4);
  EXPECT_DOUBLE_EQ(box[4],  5);
  EXPECT_DOUBLE_EQ(box[5],  6);
}

TEST(Geometry, MoveTransfersImpl) {
  Geometry g1;
  g1.add_vertex({1, 2, 3});
  Geometry g2 = std::move(g1);
  EXPECT_EQ(g2.num_vertices(), 1u);
}

TEST(Geometry, AdapterDataDeleterFiresOnDestruction) {
  static int delete_count = 0;
  delete_count = 0;
  {
    Geometry g;
    g.set_adapter_data(new int{7}, [](void* p) {
      delete static_cast<int*>(p);
      ++delete_count;
    });
    EXPECT_NE(g.adapter_data(), nullptr);
  }
  EXPECT_EQ(delete_count, 1);
}

TEST(Geometry, AdapterDataReplacementFiresOldDeleter) {
  static int delete_count = 0;
  delete_count = 0;
  Geometry g;
  g.set_adapter_data(new int{1}, [](void* p) { delete static_cast<int*>(p); ++delete_count; });
  g.set_adapter_data(new int{2}, [](void* p) { delete static_cast<int*>(p); ++delete_count; });
  EXPECT_EQ(delete_count, 1);  // only the first deleter has fired so far
}

}  // namespace
