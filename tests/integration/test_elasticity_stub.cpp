// SPDX-License-Identifier: Apache-2.0
//
// Sprint 7 push 2 integration: the always-on elasticity-stub solver.
// Mirrors the heat-solver pattern from Sprint 5 push 3 — a real
// pipeline that exercises a solver.* C ABI capability through the
// dispatcher and asserts the produced Field carries the closed-form
// answer.
//
// We use grid-mesher (Sprint 6 push 5) so the input mesh has a
// non-trivial extent and the displacement field has values to inspect
// at every node. The closed form is u_x = (load/E) * x, u_y / u_z =
// -nu * (load/E) * y / z — pinning these at a handful of corner nodes
// catches sign / scaling regressions immediately.

#include "souxmar/core/field.h"
#include "souxmar/core/geometry.h"
#include "souxmar/core/mesh.h"
#include "souxmar/pipeline/registry_dispatcher.h"
#include "souxmar/pipeline/runner.h"
#include "souxmar/plugin/discovery.h"
#include "souxmar/plugin/loader.h"
#include "souxmar/plugin/registry.h"

#include "souxmar-c/geometry.h"

#include "test_config.h"
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <variant>

namespace fs = std::filesystem;
using namespace souxmar;

namespace {

plugin::LoadedPlugin load_by_id(plugin::PluginLoader& loader,
                                const plugin::DiscoveryReport& report,
                                const std::string& want_id) {
  for (const auto& d : report.loaded) {
    if (d.manifest.id != want_id)
      continue;
    auto r = loader.load(d);
    if (auto* e = std::get_if<plugin::LoadError>(&r)) {
      throw std::runtime_error("load failed for " + want_id + ": " + e->message);
    }
    return std::move(std::get<plugin::LoadedPlugin>(r));
  }
  throw std::runtime_error("plugin not discovered: " + want_id);
}

std::shared_ptr<souxmar::core::Geometry> build_unit_cube_geometry() {
  auto* g = souxmar_geometry_new();
  const double corners[8][3] = {
      {0, 0, 0},
      {1, 0, 0},
      {0, 1, 0},
      {1, 1, 0},
      {0, 0, 1},
      {1, 0, 1},
      {0, 1, 1},
      {1, 1, 1},
  };
  for (const auto& c : corners) {
    souxmar_geometry_add_vertex(g, c);
  }
  return std::shared_ptr<souxmar::core::Geometry>(
      reinterpret_cast<souxmar::core::Geometry*>(g), [](souxmar::core::Geometry* p) {
        souxmar_geometry_free(reinterpret_cast<souxmar_geometry_t*>(p));
      });
}

}  // namespace

TEST(ElasticityStubEndToEnd, ProducesClosedFormDisplacement) {
  const fs::path plugins_root = fs::path(SOUXMAR_TEST_HELLO_MESHER_DIR).parent_path();
  const auto discovery = plugin::discover_plugins({plugins_root});
  ASSERT_FALSE(discovery.loaded.empty()) << plugins_root;

  plugin::Registry registry;
  plugin::PluginLoader loader(registry, "test-host/0.0.0");
  auto _grid = load_by_id(loader, discovery, "dev.souxmar.examples.grid-mesher");
  auto _stub = load_by_id(loader, discovery, "dev.souxmar.examples.elasticity-stub");

  ASSERT_NE(registry.find_mesher("mesher.tetra.grid"), nullptr);
  ASSERT_NE(registry.find_solver("solver.elasticity.linear"), nullptr);

  // Stage a unit-cube geometry as a synthetic upstream so the
  // mesher's `geometry: {from: ...}` convention resolves.
  auto geom_so = std::make_shared<pipeline::StageOutput>();
  geom_so->kind = pipeline::StageOutput::Kind::Geometry;
  geom_so->geometry = build_unit_cube_geometry();
  std::map<std::string, std::shared_ptr<void>> upstream;
  upstream.emplace("__test_geom__", std::static_pointer_cast<void>(geom_so));

  pipeline::RegistryDispatcher dispatcher(registry);

  // 1) Dispatch grid-mesher → Mesh.
  std::map<std::string, pipeline::Value> mesh_in;
  mesh_in.emplace("geometry", pipeline::Value::stage_ref("__test_geom__"));
  mesh_in.emplace("target_size", pipeline::Value::number(0.5));
  pipeline::DispatchContext mesh_ctx{
      "mesher.tetra.grid", pipeline::Value::map(std::move(mesh_in)), upstream};
  auto mesh_dr = dispatcher.dispatch(mesh_ctx);
  ASSERT_TRUE(std::holds_alternative<pipeline::DispatchSuccess>(mesh_dr));
  auto mesh_payload = std::get<pipeline::DispatchSuccess>(mesh_dr);
  upstream.emplace("__mesh__", mesh_payload);
  const auto* mesh_out = static_cast<const pipeline::StageOutput*>(mesh_payload.get());
  ASSERT_EQ(mesh_out->kind, pipeline::StageOutput::Kind::Mesh);
  const std::size_t num_nodes = mesh_out->mesh->num_nodes();
  ASSERT_EQ(num_nodes, 27u);  // 3×3×3 grid

  // 2) Dispatch elasticity-stub → vector Field.
  const double load = 2.1e6;  // arbitrary scale
  const double E = 210e9;
  const double nu = 0.3;
  const double eps = load / E;

  std::map<std::string, pipeline::Value> elast_in;
  elast_in.emplace("mesh", pipeline::Value::stage_ref("__mesh__"));
  elast_in.emplace("load_magnitude", pipeline::Value::number(load));
  elast_in.emplace("youngs_modulus", pipeline::Value::number(E));
  elast_in.emplace("poisson_ratio", pipeline::Value::number(nu));
  pipeline::DispatchContext elast_ctx{
      "solver.elasticity.linear", pipeline::Value::map(std::move(elast_in)), upstream};
  auto elast_dr = dispatcher.dispatch(elast_ctx);
  ASSERT_TRUE(std::holds_alternative<pipeline::DispatchSuccess>(elast_dr));

  auto elast_payload = std::get<pipeline::DispatchSuccess>(elast_dr);
  const auto* elast_out = static_cast<const pipeline::StageOutput*>(elast_payload.get());
  ASSERT_NE(elast_out, nullptr);
  ASSERT_EQ(elast_out->kind, pipeline::StageOutput::Kind::Field);
  ASSERT_NE(elast_out->field, nullptr);

  const auto& field = *elast_out->field;
  EXPECT_EQ(field.location(), souxmar::core::FieldLocation::Nodal);
  EXPECT_EQ(field.kind(), souxmar::core::FieldKind::Vector);
  EXPECT_EQ(field.count(), num_nodes);
  EXPECT_EQ(field.components(), 3u);
  EXPECT_EQ(field.num_time_steps(), 1u);

  // Closed-form check at the (1, 1, 1) corner. The grid mesher places
  // it as the last node in the lexicographic walk; index 26 in the
  // 3×3×3 grid (i=2, j=2, k=2 → 2*9 + 2*3 + 2).
  const auto u = field.at(/*location_index=*/26, /*time_index=*/0);
  EXPECT_NEAR(u[0], eps * 1.0, 1e-12);
  EXPECT_NEAR(u[1], -nu * eps * 1.0, 1e-12);
  EXPECT_NEAR(u[2], -nu * eps * 1.0, 1e-12);

  // Origin (i=j=k=0 → index 0) is undeformed under uniaxial pull
  // along +x.
  const auto u0 = field.at(0, 0);
  EXPECT_DOUBLE_EQ(u0[0], 0.0);
  EXPECT_DOUBLE_EQ(u0[1], 0.0);
  EXPECT_DOUBLE_EQ(u0[2], 0.0);
}

TEST(ElasticityStubEndToEnd, RejectsMissingMesh) {
  const fs::path plugins_root = fs::path(SOUXMAR_TEST_HELLO_MESHER_DIR).parent_path();
  const auto discovery = plugin::discover_plugins({plugins_root});
  plugin::Registry registry;
  plugin::PluginLoader loader(registry, "test-host/0.0.0");
  auto _stub = load_by_id(loader, discovery, "dev.souxmar.examples.elasticity-stub");

  pipeline::RegistryDispatcher dispatcher(registry);
  std::map<std::string, std::shared_ptr<void>> upstream;
  pipeline::DispatchContext dctx{"solver.elasticity.linear", pipeline::Value::map({}), upstream};
  auto dr = dispatcher.dispatch(dctx);
  ASSERT_TRUE(std::holds_alternative<pipeline::DispatchError>(dr));
  const auto& err = std::get<pipeline::DispatchError>(dr);
  EXPECT_NE(err.message.find("mesh"), std::string::npos) << err.message;
}
