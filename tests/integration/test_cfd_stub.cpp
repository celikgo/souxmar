// SPDX-License-Identifier: Apache-2.0
//
// Sprint 8 push 2 — always-on CFD stub integration. The opt-in
// OpenFOAM adapter sibling is exercised on the nightly
// CFD-bearing matrix (Docker image with OpenFOAM v12 pre-staged);
// the default-CI bar lives here.

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <variant>

#include "souxmar-c/geometry.h"
#include "souxmar/core/field.h"
#include "souxmar/core/geometry.h"
#include "souxmar/core/mesh.h"
#include "souxmar/pipeline/registry_dispatcher.h"
#include "souxmar/pipeline/runner.h"
#include "souxmar/plugin/discovery.h"
#include "souxmar/plugin/loader.h"
#include "souxmar/plugin/registry.h"

#include "test_config.h"

namespace fs = std::filesystem;
using namespace souxmar;

namespace {

plugin::LoadedPlugin load_by_id(plugin::PluginLoader&             loader,
                                const plugin::DiscoveryReport&    report,
                                const std::string&                want_id) {
  for (const auto& d : report.loaded) {
    if (d.manifest.id != want_id) continue;
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
      {0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0},
      {0, 0, 1}, {1, 0, 1}, {0, 1, 1}, {1, 1, 1},
  };
  for (const auto& c : corners) {
    souxmar_geometry_add_vertex(g, c);
  }
  return std::shared_ptr<souxmar::core::Geometry>(
      reinterpret_cast<souxmar::core::Geometry*>(g),
      [](souxmar::core::Geometry* p) {
        souxmar_geometry_free(reinterpret_cast<souxmar_geometry_t*>(p));
      });
}

}  // namespace

TEST(CfdStubEndToEnd, ProducesUniformVelocityField) {
  const fs::path plugins_root =
      fs::path(SOUXMAR_TEST_HELLO_MESHER_DIR).parent_path();
  const auto discovery = plugin::discover_plugins({plugins_root});
  ASSERT_FALSE(discovery.loaded.empty()) << plugins_root;

  plugin::Registry      registry;
  plugin::PluginLoader  loader(registry, "test-host/0.0.0");
  auto _grid = load_by_id(loader, discovery, "dev.souxmar.examples.grid-mesher");
  auto _cfd  = load_by_id(loader, discovery, "dev.souxmar.examples.cfd-stub");

  ASSERT_NE(registry.find_mesher("mesher.tetra.grid"),     nullptr);
  ASSERT_NE(registry.find_solver("solver.cfd.simple"),      nullptr);

  auto geom_so      = std::make_shared<pipeline::StageOutput>();
  geom_so->kind     = pipeline::StageOutput::Kind::Geometry;
  geom_so->geometry = build_unit_cube_geometry();
  std::map<std::string, std::shared_ptr<void>> upstream;
  upstream.emplace("__test_geom__", std::static_pointer_cast<void>(geom_so));

  pipeline::RegistryDispatcher dispatcher(registry);

  // mesh
  std::map<std::string, pipeline::Value> mesh_in;
  mesh_in.emplace("geometry",    pipeline::Value::stage_ref("__test_geom__"));
  mesh_in.emplace("target_size", pipeline::Value::number(0.5));
  auto mesh_dr = dispatcher.dispatch(
      pipeline::DispatchContext{"mesher.tetra.grid",
                                pipeline::Value::map(std::move(mesh_in)),
                                upstream});
  ASSERT_TRUE(std::holds_alternative<pipeline::DispatchSuccess>(mesh_dr));
  auto mesh_payload = std::get<pipeline::DispatchSuccess>(mesh_dr);
  upstream.emplace("__mesh__", mesh_payload);
  const auto* mesh_out = static_cast<const pipeline::StageOutput*>(mesh_payload.get());
  ASSERT_EQ(mesh_out->kind, pipeline::StageOutput::Kind::Mesh);

  // cfd-stub
  const double magnitude = 2.5;
  std::map<std::string, pipeline::Value> cfd_in;
  cfd_in.emplace("mesh",               pipeline::Value::stage_ref("__mesh__"));
  cfd_in.emplace("velocity_magnitude", pipeline::Value::number(magnitude));
  // flow_direction defaults to +x
  auto cfd_dr = dispatcher.dispatch(
      pipeline::DispatchContext{"solver.cfd.simple",
                                pipeline::Value::map(std::move(cfd_in)),
                                upstream});
  ASSERT_TRUE(std::holds_alternative<pipeline::DispatchSuccess>(cfd_dr));

  auto cfd_payload = std::get<pipeline::DispatchSuccess>(cfd_dr);
  const auto* cfd_out = static_cast<const pipeline::StageOutput*>(cfd_payload.get());
  ASSERT_EQ(cfd_out->kind, pipeline::StageOutput::Kind::Field);
  ASSERT_NE(cfd_out->field, nullptr);

  const auto& field = *cfd_out->field;
  EXPECT_EQ(field.location(),      souxmar::core::FieldLocation::Nodal);
  EXPECT_EQ(field.kind(),          souxmar::core::FieldKind::Vector);
  EXPECT_EQ(field.components(),    3u);
  EXPECT_EQ(field.num_time_steps(), 1u);

  // Every node carries (magnitude, 0, 0).
  for (std::size_t n = 0; n < field.count(); ++n) {
    const auto u = field.at(n, 0);
    EXPECT_DOUBLE_EQ(u[0], magnitude) << "node " << n;
    EXPECT_DOUBLE_EQ(u[1], 0.0)       << "node " << n;
    EXPECT_DOUBLE_EQ(u[2], 0.0)       << "node " << n;
  }
}

// Sprint 13 push 4 — per-patch BC routing carry-over from Sprint 10.
// Drives the cfd-stub with a small tet mesh whose face tags wire a
// wall patch + an inlet patch; asserts the velocity field at the
// matching nodes is the routed value (zero for the wall, the
// patch's inlet velocity for the inlet).
TEST(CfdStubEndToEnd, PerPatchBcRoutingAppliesWallAndInlet) {
  const fs::path plugins_root =
      fs::path(SOUXMAR_TEST_HELLO_MESHER_DIR).parent_path();
  const auto discovery = plugin::discover_plugins({plugins_root});
  plugin::Registry      registry;
  plugin::PluginLoader  loader(registry, "test-host/0.0.0");
  auto _hello = load_by_id(loader, discovery, "dev.souxmar.examples.hello-mesher");
  auto _cfd   = load_by_id(loader, discovery, "dev.souxmar.examples.cfd-stub");

  pipeline::RegistryDispatcher dispatcher(registry);
  std::map<std::string, std::shared_ptr<void>> upstream;

  // hello-mesher → one tet, 4 nodes (0,1,2,3). Tet face conventions
  // (Gmsh):
  //   face 0: nodes (1,2,3)
  //   face 1: nodes (0,3,2)
  //   face 2: nodes (0,1,3)
  //   face 3: nodes (0,2,1)
  auto mesh_dr = dispatcher.dispatch(
      pipeline::DispatchContext{"mesher.tetra.hello",
                                pipeline::Value::map({}),
                                upstream});
  ASSERT_TRUE(std::holds_alternative<pipeline::DispatchSuccess>(mesh_dr));
  auto mesh_payload = std::get<pipeline::DispatchSuccess>(mesh_dr);
  upstream.emplace("__mesh__", mesh_payload);
  auto* mesh_so_mut =
      const_cast<pipeline::StageOutput*>(
        static_cast<const pipeline::StageOutput*>(mesh_payload.get()));
  ASSERT_NE(mesh_so_mut->mesh, nullptr);

  // Stamp face_tags: face 0 → wall (tag 7); face 2 → inlet (tag 9).
  mesh_so_mut->mesh->set_face_tag(souxmar::core::CellIndex{0}, 0,
                                   souxmar::core::EntityTag{7});
  mesh_so_mut->mesh->set_face_tag(souxmar::core::CellIndex{0}, 2,
                                   souxmar::core::EntityTag{9});

  // Build the per-patch BC list. The cfd-stub reads list-of-maps.
  std::vector<pipeline::Value> patches;

  {
    std::map<std::string, pipeline::Value> bc;
    bc.emplace("type", pipeline::Value::string("wall"));
    std::map<std::string, pipeline::Value> entry;
    entry.emplace("name", pipeline::Value::string("the-wall"));
    entry.emplace("tag",  pipeline::Value::number(7));
    entry.emplace("bc",   pipeline::Value::map(std::move(bc)));
    patches.push_back(pipeline::Value::map(std::move(entry)));
  }
  {
    std::vector<pipeline::Value> vel = {
        pipeline::Value::number(4.0),
        pipeline::Value::number(0.0),
        pipeline::Value::number(0.0),
    };
    std::map<std::string, pipeline::Value> bc;
    bc.emplace("type",     pipeline::Value::string("inlet"));
    bc.emplace("velocity", pipeline::Value::list(std::move(vel)));
    std::map<std::string, pipeline::Value> entry;
    entry.emplace("name", pipeline::Value::string("the-inlet"));
    entry.emplace("tag",  pipeline::Value::number(9));
    entry.emplace("bc",   pipeline::Value::map(std::move(bc)));
    patches.push_back(pipeline::Value::map(std::move(entry)));
  }

  // Bulk magnitude is 1.0 (default); inlet velocity is 4.0 in +x
  // (overriding the bulk).
  std::map<std::string, pipeline::Value> cfd_in;
  cfd_in.emplace("mesh",    pipeline::Value::stage_ref("__mesh__"));
  cfd_in.emplace("patches", pipeline::Value::list(std::move(patches)));

  auto cfd_dr = dispatcher.dispatch(
      pipeline::DispatchContext{"solver.cfd.simple",
                                pipeline::Value::map(std::move(cfd_in)),
                                upstream});
  ASSERT_TRUE(std::holds_alternative<pipeline::DispatchSuccess>(cfd_dr));
  auto cfd_payload = std::get<pipeline::DispatchSuccess>(cfd_dr);
  const auto* cfd_out = static_cast<const pipeline::StageOutput*>(cfd_payload.get());
  ASSERT_NE(cfd_out->field, nullptr);
  const auto& field = *cfd_out->field;

  // Routing precedence: wall > inlet > bulk. Nodes 1,2,3 sit on
  // the wall face; node 0 sits on the inlet face. Node ordering
  // from the hello-mesher's one tet is (0,1,2,3); face 0 covers
  // (1,2,3) → wall; face 2 covers (0,1,3) → inlet; the wall wins
  // for nodes 1 and 3 (intersection); node 0 is inlet-only; node
  // 2 is wall-only.
  auto u0 = field.at(0, 0);
  auto u1 = field.at(1, 0);
  auto u2 = field.at(2, 0);
  auto u3 = field.at(3, 0);

  // Node 0 — inlet only.
  EXPECT_DOUBLE_EQ(u0[0], 4.0);
  EXPECT_DOUBLE_EQ(u0[1], 0.0);
  EXPECT_DOUBLE_EQ(u0[2], 0.0);

  // Node 1 — wall ∩ inlet → wall dominates → (0,0,0).
  EXPECT_DOUBLE_EQ(u1[0], 0.0);
  EXPECT_DOUBLE_EQ(u1[1], 0.0);
  EXPECT_DOUBLE_EQ(u1[2], 0.0);

  // Node 2 — wall only.
  EXPECT_DOUBLE_EQ(u2[0], 0.0);
  EXPECT_DOUBLE_EQ(u2[1], 0.0);
  EXPECT_DOUBLE_EQ(u2[2], 0.0);

  // Node 3 — wall ∩ inlet → wall.
  EXPECT_DOUBLE_EQ(u3[0], 0.0);
  EXPECT_DOUBLE_EQ(u3[1], 0.0);
  EXPECT_DOUBLE_EQ(u3[2], 0.0);
}

TEST(CfdStubEndToEnd, FlowDirectionInputAccepted) {
  const fs::path plugins_root =
      fs::path(SOUXMAR_TEST_HELLO_MESHER_DIR).parent_path();
  const auto discovery = plugin::discover_plugins({plugins_root});
  plugin::Registry      registry;
  plugin::PluginLoader  loader(registry, "test-host/0.0.0");
  auto _hello = load_by_id(loader, discovery, "dev.souxmar.examples.hello-mesher");
  auto _cfd   = load_by_id(loader, discovery, "dev.souxmar.examples.cfd-stub");

  // hello-mesher → cfd-stub with a vertical flow direction.
  pipeline::RegistryDispatcher dispatcher(registry);
  std::map<std::string, std::shared_ptr<void>> upstream;

  auto mesh_dr = dispatcher.dispatch(
      pipeline::DispatchContext{"mesher.tetra.hello",
                                pipeline::Value::map({}),
                                upstream});
  ASSERT_TRUE(std::holds_alternative<pipeline::DispatchSuccess>(mesh_dr));
  upstream.emplace("__mesh__", std::get<pipeline::DispatchSuccess>(mesh_dr));

  std::vector<pipeline::Value> dir = {
      pipeline::Value::number(0.0),
      pipeline::Value::number(0.0),
      pipeline::Value::number(1.0),
  };
  std::map<std::string, pipeline::Value> cfd_in;
  cfd_in.emplace("mesh",               pipeline::Value::stage_ref("__mesh__"));
  cfd_in.emplace("velocity_magnitude", pipeline::Value::number(3.0));
  cfd_in.emplace("flow_direction",     pipeline::Value::list(std::move(dir)));
  auto cfd_dr = dispatcher.dispatch(
      pipeline::DispatchContext{"solver.cfd.simple",
                                pipeline::Value::map(std::move(cfd_in)),
                                upstream});
  ASSERT_TRUE(std::holds_alternative<pipeline::DispatchSuccess>(cfd_dr));

  auto cfd_payload = std::get<pipeline::DispatchSuccess>(cfd_dr);
  const auto* cfd_out = static_cast<const pipeline::StageOutput*>(cfd_payload.get());
  const auto u = cfd_out->field->at(0, 0);
  EXPECT_DOUBLE_EQ(u[0], 0.0);
  EXPECT_DOUBLE_EQ(u[1], 0.0);
  EXPECT_DOUBLE_EQ(u[2], 3.0);
}
