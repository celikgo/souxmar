// SPDX-License-Identifier: Apache-2.0
//
// Sprint 6 push 5 — gate test for the second tetrahedral mesher.
//
// The Sprint 6 plan's exit criterion: "A user can swap
// mesher.tetra.native for mesher.tetra.gmsh in pipeline YAML with no
// other changes; same result format." This file exercises the contract
// from the always-on side — grid-mesher takes a Geometry, produces a
// Mesh through the souxmar-c API, and the result is structurally
// indistinguishable from anything else producing tetrahedral cells
// (same cell type, same node/cell schema, same StageOutput::Kind::Mesh).
//
// The gmsh variant runs nightly with the opt-in build flag; this test
// fences the always-on swap-test bar so the contract can't regress.

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

// Build a 1×1×1 bounding-box Geometry via the C ABI. Mirrors what an
// in-tree `geometry-stub` plugin would emit — eight vertices at the
// cube corners; edges/faces/solids not modelled (the grid mesher only
// reads the bounding box, so vertices are sufficient).
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

TEST(SwapMesher, GridMesherFromProgrammaticGeometry) {
  const fs::path plugins_root =
      fs::path(SOUXMAR_TEST_HELLO_MESHER_DIR).parent_path();
  const auto discovery = plugin::discover_plugins({plugins_root});
  ASSERT_FALSE(discovery.loaded.empty()) << plugins_root;

  plugin::Registry      registry;
  plugin::PluginLoader  loader(registry, "test-host/0.0.0");
  auto _grid = load_by_id(loader, discovery, "dev.souxmar.examples.grid-mesher");
  ASSERT_NE(registry.find_mesher("mesher.tetra.grid"), nullptr);

  // Wrap the programmatic geometry as an upstream StageOutput so the
  // dispatcher's mesher.* path resolves it via the standard
  // `geometry: {from: ...}` convention.
  auto geom_so      = std::make_shared<pipeline::StageOutput>();
  geom_so->kind     = pipeline::StageOutput::Kind::Geometry;
  geom_so->geometry = build_unit_cube_geometry();

  std::map<std::string, std::shared_ptr<void>> upstream;
  upstream.emplace("__test_geom__",
                   std::static_pointer_cast<void>(geom_so));

  std::map<std::string, pipeline::Value> stage_input;
  stage_input.emplace("geometry",
      pipeline::Value::stage_ref("__test_geom__"));
  stage_input.emplace("target_size", pipeline::Value::number(0.5));
  auto input_value = pipeline::Value::map(std::move(stage_input));

  pipeline::RegistryDispatcher dispatcher(registry);
  pipeline::DispatchContext    dctx{"mesher.tetra.grid",
                                    input_value, upstream};
  auto dr = dispatcher.dispatch(dctx);
  ASSERT_TRUE(std::holds_alternative<pipeline::DispatchSuccess>(dr));

  auto payload = std::get<pipeline::DispatchSuccess>(dr);
  const auto* so = static_cast<const pipeline::StageOutput*>(payload.get());
  ASSERT_NE(so, nullptr);
  ASSERT_EQ(so->kind, pipeline::StageOutput::Kind::Mesh);
  ASSERT_NE(so->mesh, nullptr);

  // target_size=0.5 against a 1×1×1 bbox → 3 nodes per axis → 27 nodes
  // and 5×2×2×2 = 40 tets. Pinning the exact count guards against a
  // regression in the bbox-to-N computation (the contract is "same
  // result format as anything else producing tetrahedral cells", and
  // the count is part of the format the swap-test promises).
  EXPECT_EQ(so->mesh->num_nodes(), 27u);
  EXPECT_EQ(so->mesh->num_cells(), 40u);
}

TEST(SwapMesher, GridMesherRejectsMissingGeometry) {
  // Dispatcher requires a geometry upstream for any mesher.* call that
  // declares one; grid-mesher fails gracefully when none is wired.
  const fs::path plugins_root =
      fs::path(SOUXMAR_TEST_HELLO_MESHER_DIR).parent_path();
  const auto discovery = plugin::discover_plugins({plugins_root});
  plugin::Registry      registry;
  plugin::PluginLoader  loader(registry, "test-host/0.0.0");
  auto _grid = load_by_id(loader, discovery, "dev.souxmar.examples.grid-mesher");

  pipeline::RegistryDispatcher dispatcher(registry);
  std::map<std::string, std::shared_ptr<void>> upstream;
  pipeline::DispatchContext dctx{
      "mesher.tetra.grid",
      pipeline::Value::map({}),
      upstream};
  auto dr = dispatcher.dispatch(dctx);
  ASSERT_TRUE(std::holds_alternative<pipeline::DispatchError>(dr));
  const auto& err = std::get<pipeline::DispatchError>(dr);
  EXPECT_NE(err.message.find("Geometry"), std::string::npos)
      << err.message;
}
