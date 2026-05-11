// SPDX-License-Identifier: Apache-2.0
//
// Tests for the souxmar_mesh_*, souxmar_geometry_*, souxmar_field_*, and
// souxmar_value_* C ABI accessors. The accessors must be the canonical
// surface plugins use; bugs here propagate to every plugin in the
// ecosystem, so coverage is broad.

#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "souxmar-c/field.h"
#include "souxmar-c/geometry.h"
#include "souxmar-c/mesh.h"
#include "souxmar-c/value.h"
#include "souxmar/pipeline/value.h"

// -------- mesh handle --------

TEST(CAbiMesh, NewFreeRoundtrip) {
  souxmar_mesh_t* m = souxmar_mesh_new();
  ASSERT_NE(m, nullptr);
  EXPECT_EQ(souxmar_mesh_num_nodes(m), 0u);
  EXPECT_EQ(souxmar_mesh_num_cells(m), 0u);
  souxmar_mesh_free(m);
  souxmar_mesh_free(nullptr);  // must not crash
}

TEST(CAbiMesh, AddNodeAndReadBack) {
  souxmar_mesh_t* m = souxmar_mesh_new();
  const double pos[3] = {1.5, 2.5, 3.5};
  const uint64_t i = souxmar_mesh_add_node(m, pos);
  EXPECT_EQ(i, 0u);
  EXPECT_EQ(souxmar_mesh_num_nodes(m), 1u);

  double readback[3] = {0, 0, 0};
  auto s = souxmar_mesh_node(m, i, readback);
  EXPECT_EQ(s.code, SOUXMAR_OK);
  EXPECT_DOUBLE_EQ(readback[0], 1.5);
  EXPECT_DOUBLE_EQ(readback[1], 2.5);
  EXPECT_DOUBLE_EQ(readback[2], 3.5);

  souxmar_mesh_free(m);
}

TEST(CAbiMesh, AddCellTetrahedronRoundtrip) {
  souxmar_mesh_t* m = souxmar_mesh_new();
  const double p0[3] = {0, 0, 0};
  const double p1[3] = {1, 0, 0};
  const double p2[3] = {0, 1, 0};
  const double p3[3] = {0, 0, 1};
  souxmar_mesh_add_node(m, p0);
  souxmar_mesh_add_node(m, p1);
  souxmar_mesh_add_node(m, p2);
  souxmar_mesh_add_node(m, p3);

  const uint64_t nodes[4] = {0, 1, 2, 3};
  uint64_t cell_index = 99;
  auto s = souxmar_mesh_add_cell(m, SOUXMAR_ET_TET4, nodes, 4, /*tag=*/42, &cell_index);
  EXPECT_EQ(s.code, SOUXMAR_OK);
  EXPECT_EQ(cell_index, 0u);
  EXPECT_EQ(souxmar_mesh_num_cells(m), 1u);
  EXPECT_EQ(souxmar_mesh_cell_type(m, 0), SOUXMAR_ET_TET4);
  EXPECT_EQ(souxmar_mesh_cell_tag(m, 0), 42);
  EXPECT_EQ(souxmar_mesh_cell_node_count(m, 0), 4u);

  uint64_t readback[4] = {0};
  s = souxmar_mesh_cell_nodes(m, 0, readback, 4);
  EXPECT_EQ(s.code, SOUXMAR_OK);
  for (int i = 0; i < 4; ++i) EXPECT_EQ(readback[i], static_cast<uint64_t>(i));

  souxmar_mesh_free(m);
}

TEST(CAbiMesh, AddCellWrongNodeCountRejected) {
  souxmar_mesh_t* m = souxmar_mesh_new();
  const double p[3] = {0, 0, 0};
  souxmar_mesh_add_node(m, p);
  const uint64_t nodes[3] = {0, 0, 0};
  auto s = souxmar_mesh_add_cell(m, SOUXMAR_ET_TET4, nodes, 3, -1, nullptr);
  EXPECT_EQ(s.code, SOUXMAR_E_INVALID_ARGUMENT);
  souxmar_mesh_free(m);
}

TEST(CAbiMesh, NodeOutOfRangeReturnsNotFound) {
  souxmar_mesh_t* m = souxmar_mesh_new();
  double pos[3] = {0, 0, 0};
  auto s = souxmar_mesh_node(m, 999, pos);
  EXPECT_EQ(s.code, SOUXMAR_E_NOT_FOUND);
  souxmar_mesh_free(m);
}

TEST(CAbiMesh, CellNodesCapacityCheck) {
  souxmar_mesh_t* m = souxmar_mesh_new();
  const double p[3] = {0, 0, 0};
  souxmar_mesh_add_node(m, p);
  souxmar_mesh_add_node(m, p);
  const uint64_t edge_nodes[2] = {0, 1};
  souxmar_mesh_add_cell(m, SOUXMAR_ET_EDGE2, edge_nodes, 2, -1, nullptr);

  uint64_t too_small[1] = {0};
  auto s = souxmar_mesh_cell_nodes(m, 0, too_small, 1);
  EXPECT_EQ(s.code, SOUXMAR_E_INVALID_ARGUMENT);
  souxmar_mesh_free(m);
}

TEST(CAbiMesh, NullMeshReturnsZeroCounts) {
  EXPECT_EQ(souxmar_mesh_num_nodes(nullptr), 0u);
  EXPECT_EQ(souxmar_mesh_num_cells(nullptr), 0u);
  EXPECT_EQ(souxmar_mesh_cell_type(nullptr, 0), SOUXMAR_ET_UNKNOWN);
}

TEST(CAbiMesh, FlatNodesIsContiguous) {
  souxmar_mesh_t* m = souxmar_mesh_new();
  const double p[3] = {1, 2, 3};
  souxmar_mesh_add_node(m, p);
  size_t size = 0;
  const double* flat = souxmar_mesh_nodes_flat(m, &size);
  ASSERT_NE(flat, nullptr);
  EXPECT_EQ(size, 3u);
  EXPECT_DOUBLE_EQ(flat[0], 1.0);
  EXPECT_DOUBLE_EQ(flat[1], 2.0);
  EXPECT_DOUBLE_EQ(flat[2], 3.0);
  souxmar_mesh_free(m);
}

// -------- geometry handle --------

TEST(CAbiGeometry, NewFreeRoundtrip) {
  souxmar_geometry_t* g = souxmar_geometry_new();
  ASSERT_NE(g, nullptr);
  EXPECT_EQ(souxmar_geometry_num_vertices(g), 0u);
  souxmar_geometry_free(g);
}

TEST(CAbiGeometry, AddAndCount) {
  souxmar_geometry_t* g = souxmar_geometry_new();
  const double pos[3] = {1, 2, 3};
  EXPECT_EQ(souxmar_geometry_add_vertex(g, pos), 0u);
  EXPECT_EQ(souxmar_geometry_add_edge(g),        0u);
  EXPECT_EQ(souxmar_geometry_add_face(g),        0u);
  EXPECT_EQ(souxmar_geometry_add_solid(g),       0u);
  EXPECT_EQ(souxmar_geometry_num_vertices(g), 1u);
  EXPECT_EQ(souxmar_geometry_num_edges(g),    1u);
  EXPECT_EQ(souxmar_geometry_num_faces(g),    1u);
  EXPECT_EQ(souxmar_geometry_num_solids(g),   1u);
  souxmar_geometry_free(g);
}

TEST(CAbiGeometry, TagAndNameRoundtrip) {
  souxmar_geometry_t* g = souxmar_geometry_new();
  souxmar_geometry_add_face(g);

  EXPECT_EQ(souxmar_geometry_tag(g, SOUXMAR_GK_FACE, 0), -1);  // untagged
  EXPECT_EQ(souxmar_geometry_name(g, SOUXMAR_GK_FACE, 0), nullptr);

  auto s1 = souxmar_geometry_set_tag(g, SOUXMAR_GK_FACE, 0, 7);
  EXPECT_EQ(s1.code, SOUXMAR_OK);
  auto s2 = souxmar_geometry_set_name(g, SOUXMAR_GK_FACE, 0, "clamped_face");
  EXPECT_EQ(s2.code, SOUXMAR_OK);

  EXPECT_EQ(souxmar_geometry_tag(g, SOUXMAR_GK_FACE, 0), 7);
  const char* name = souxmar_geometry_name(g, SOUXMAR_GK_FACE, 0);
  ASSERT_NE(name, nullptr);
  EXPECT_STREQ(name, "clamped_face");

  souxmar_geometry_free(g);
}

TEST(CAbiGeometry, OutOfRangeSetReturnsNotFound) {
  souxmar_geometry_t* g = souxmar_geometry_new();
  auto s = souxmar_geometry_set_tag(g, SOUXMAR_GK_SOLID, 0, 1);
  EXPECT_EQ(s.code, SOUXMAR_E_NOT_FOUND);
  souxmar_geometry_free(g);
}

TEST(CAbiGeometry, BoundingBox) {
  souxmar_geometry_t* g = souxmar_geometry_new();
  const double p1[3] = {-1, -2, -3};
  const double p2[3] = { 4,  5,  6};
  souxmar_geometry_add_vertex(g, p1);
  souxmar_geometry_add_vertex(g, p2);
  double box[6] = {};
  auto s = souxmar_geometry_bounding_box(g, box);
  EXPECT_EQ(s.code, SOUXMAR_OK);
  EXPECT_DOUBLE_EQ(box[0], -1);
  EXPECT_DOUBLE_EQ(box[5],  6);
  souxmar_geometry_free(g);
}

// -------- field handle --------

TEST(CAbiField, NewFreeRoundtrip) {
  souxmar_field_t* f = souxmar_field_new("u", SOUXMAR_FL_NODAL, SOUXMAR_FK_VECTOR, 4, 1);
  ASSERT_NE(f, nullptr);
  EXPECT_STREQ(souxmar_field_name(f), "u");
  EXPECT_EQ(souxmar_field_location(f),       SOUXMAR_FL_NODAL);
  EXPECT_EQ(souxmar_field_kind(f),           SOUXMAR_FK_VECTOR);
  EXPECT_EQ(souxmar_field_components(f),     3u);
  EXPECT_EQ(souxmar_field_count(f),          4u);
  EXPECT_EQ(souxmar_field_num_time_steps(f), 1u);
  EXPECT_EQ(souxmar_field_data_size(f),      12u);
  souxmar_field_free(f);
}

TEST(CAbiField, ZeroTimeStepsReturnsNull) {
  souxmar_field_t* f = souxmar_field_new("x", SOUXMAR_FL_CELL, SOUXMAR_FK_SCALAR, 3, 0);
  EXPECT_EQ(f, nullptr);
}

TEST(CAbiField, DataReadWrite) {
  souxmar_field_t* f = souxmar_field_new("u", SOUXMAR_FL_NODAL, SOUXMAR_FK_SCALAR, 3, 1);
  double* d = souxmar_field_data(f);
  ASSERT_NE(d, nullptr);
  d[0] = 1.5; d[1] = 2.5; d[2] = 3.5;
  const double* cd = souxmar_field_data_const(f);
  EXPECT_DOUBLE_EQ(cd[1], 2.5);
  souxmar_field_free(f);
}

// -------- value handle --------

TEST(CAbiValue, KindAndScalars) {
  using souxmar::pipeline::Value;
  auto null_v   = Value::null_value();
  auto bool_v   = Value::boolean(true);
  auto num_v    = Value::number(2.5);
  auto str_v    = Value::string("hi");
  auto stage_v  = Value::stage_ref("import");

  EXPECT_EQ(souxmar_value_kind(reinterpret_cast<const souxmar_value_t*>(&null_v)),
            SOUXMAR_VK_NULL);
  EXPECT_EQ(souxmar_value_kind(reinterpret_cast<const souxmar_value_t*>(&bool_v)),
            SOUXMAR_VK_BOOL);
  EXPECT_EQ(souxmar_value_kind(reinterpret_cast<const souxmar_value_t*>(&num_v)),
            SOUXMAR_VK_NUMBER);
  EXPECT_EQ(souxmar_value_kind(reinterpret_cast<const souxmar_value_t*>(&str_v)),
            SOUXMAR_VK_STRING);
  EXPECT_EQ(souxmar_value_kind(reinterpret_cast<const souxmar_value_t*>(&stage_v)),
            SOUXMAR_VK_STAGE);

  EXPECT_EQ(souxmar_value_as_bool(reinterpret_cast<const souxmar_value_t*>(&bool_v)),   1);
  EXPECT_DOUBLE_EQ(souxmar_value_as_number(reinterpret_cast<const souxmar_value_t*>(&num_v)),
                   2.5);
  EXPECT_STREQ(souxmar_value_as_string(reinterpret_cast<const souxmar_value_t*>(&str_v)), "hi");
  EXPECT_STREQ(souxmar_value_as_stage(reinterpret_cast<const souxmar_value_t*>(&stage_v)),
               "import");
}

TEST(CAbiValue, MapAccess) {
  using souxmar::pipeline::Value;
  auto m = Value::map({
      {"path",    Value::string("/tmp/out.txt")},
      {"verbose", Value::boolean(false)},
  });
  const auto* h = reinterpret_cast<const souxmar_value_t*>(&m);
  EXPECT_EQ(souxmar_value_kind(h), SOUXMAR_VK_MAP);
  EXPECT_EQ(souxmar_value_map_size(h), 2u);

  const souxmar_value_t* p = souxmar_value_map_get(h, "path");
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(souxmar_value_kind(p), SOUXMAR_VK_STRING);
  EXPECT_STREQ(souxmar_value_as_string(p), "/tmp/out.txt");

  EXPECT_EQ(souxmar_value_map_get(h, "missing"), nullptr);
}

TEST(CAbiValue, ListAccess) {
  using souxmar::pipeline::Value;
  auto l = Value::list({Value::number(1.0), Value::number(2.0), Value::number(3.0)});
  const auto* h = reinterpret_cast<const souxmar_value_t*>(&l);
  EXPECT_EQ(souxmar_value_kind(h), SOUXMAR_VK_LIST);
  EXPECT_EQ(souxmar_value_list_size(h), 3u);
  const souxmar_value_t* item = souxmar_value_list_at(h, 1);
  ASSERT_NE(item, nullptr);
  EXPECT_DOUBLE_EQ(souxmar_value_as_number(item), 2.0);

  EXPECT_EQ(souxmar_value_list_at(h, 99), nullptr);
}
