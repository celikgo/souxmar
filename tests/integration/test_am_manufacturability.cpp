// SPDX-License-Identifier: Apache-2.0
//
// Manufacturing block — am-manufacturability integration.
//
//   solver.am.overhang / solver.am.printability / solver.am.buildtime
//
// Written against the frozen capability contract (AM_CONTRACT §1 rows 9-11,
// §3.9, §3.10, §3.11), not against the plugin source.
//
// The physics cases run on meshes built here in the test so the geometry is
// unambiguous. The key case is the two-cell vertical column:
//
//        +----+   cell 1 — its bottom face is shared with cell 0, its own
//        |    |            exposed faces are four vertical walls plus the
//        +----+            top, so n·b >= 0 everywhere: no downskin, no
//        |    |            support.
//        +----+   cell 0 — its bottom face is a boundary face with outward
//                          normal -Z, so n·b < 0 and its tilt from the build
//                          plate is 0°: a horizontal downskin that needs
//                          support.
//
// which is exactly the contract's rule set: "for an outward unit face normal
// n and unit build direction b, the face is downward iff n·b < 0; its tilt
// from the build plate is acos(|n·b|) in degrees; support_needed = 1 iff any
// downward boundary face has tilt < overhang_threshold_deg".

#include "souxmar/core/element_type.h"
#include "souxmar/core/field.h"
#include "souxmar/core/mesh.h"
#include "souxmar/core/tag.h"
#include "souxmar/pipeline/cache.h"
#include "souxmar/pipeline/parser.h"
#include "souxmar/pipeline/registry_dispatcher.h"
#include "souxmar/pipeline/runner.h"
#include "souxmar/pipeline/value.h"
#include "souxmar/plugin/discovery.h"
#include "souxmar/plugin/loader.h"
#include "souxmar/plugin/registry.h"

#include "test_config.h"
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <span>
#include <sstream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace fs = std::filesystem;
using namespace souxmar;

namespace {

fs::path plugins_root() {
  return fs::path(SOUXMAR_TEST_AM_MANUFACTURABILITY_DIR).parent_path();
}

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

constexpr double kEdge = 0.01;  // 10 mm voxel — a plausible AM part scale

// Vertical column of `n_cells` Hex8 voxels of edge `kEdge`, nodes shared
// between stacked cells so the interior face is genuinely interior (the
// contract defines a boundary face as one whose node set no other cell
// shares). Node order per level is the standard VTK/Gmsh Hex8 bottom quad
// CCW-from-above, then the same quad on the level above.
core::Mesh build_hex_column(std::size_t n_cells) {
  core::Mesh m;
  const std::array<std::array<double, 2>, 4> quad{
      {{0.0, 0.0}, {kEdge, 0.0}, {kEdge, kEdge}, {0.0, kEdge}}};
  for (std::size_t k = 0; k <= n_cells; ++k) {
    for (const auto& p : quad) {
      m.add_node({p[0], p[1], static_cast<double>(k) * kEdge});
    }
  }
  for (std::size_t k = 0; k < n_cells; ++k) {
    const std::uint64_t lo = 4u * static_cast<std::uint64_t>(k);
    const std::uint64_t hi = lo + 4u;
    const std::array<core::NodeIndex, 8> nodes{{
        core::NodeIndex{lo + 0},
        core::NodeIndex{lo + 1},
        core::NodeIndex{lo + 2},
        core::NodeIndex{lo + 3},
        core::NodeIndex{hi + 0},
        core::NodeIndex{hi + 1},
        core::NodeIndex{hi + 2},
        core::NodeIndex{hi + 3},
    }};
    // §2.2: a non-negative cell tag *is* the layer index.
    m.add_cell(core::ElementType::Hex8, nodes, core::EntityTag{static_cast<std::int32_t>(k)});
  }
  return m;
}

// Two disjoint Tri3 facets: cell 0 is horizontal with its right-hand normal
// pointing down (-Z), cell 1 is vertical (normal +X, so n·b == 0 exactly).
core::Mesh build_two_triangle_surface() {
  core::Mesh m;
  // cell 0 — horizontal, wound so that (v1-v0) x (v2-v0) = -Z.
  m.add_node({0.0, 0.0, 2.0 * kEdge});
  m.add_node({0.0, kEdge, 2.0 * kEdge});
  m.add_node({kEdge, 0.0, 2.0 * kEdge});
  // cell 1 — vertical, in the x = 0 plane.
  m.add_node({0.0, 0.0, 0.0});
  m.add_node({0.0, kEdge, 0.0});
  m.add_node({0.0, 0.0, kEdge});

  const std::array<core::NodeIndex, 3> horizontal{
      {core::NodeIndex{0}, core::NodeIndex{1}, core::NodeIndex{2}}};
  const std::array<core::NodeIndex, 3> vertical{
      {core::NodeIndex{3}, core::NodeIndex{4}, core::NodeIndex{5}}};
  m.add_cell(core::ElementType::Tri3, horizontal, core::EntityTag{1});
  m.add_cell(core::ElementType::Tri3, vertical, core::EntityTag{0});
  return m;
}

// Wrap a host-built Mesh as a Mesh-kind StageOutput so it can be handed to
// the dispatcher as an upstream handle (the trick test_cfd_stub.cpp uses for
// Geometry).
std::shared_ptr<void> as_upstream_mesh(core::Mesh&& mesh) {
  auto so = std::make_shared<pipeline::StageOutput>();
  so->kind = pipeline::StageOutput::Kind::Mesh;
  so->mesh = std::make_shared<core::Mesh>(std::move(mesh));
  return std::static_pointer_cast<void>(so);
}

void expect_all_stages_executed(const pipeline::RunResult& run) {
  EXPECT_EQ(run.status, pipeline::RunResult::Status::Success);
  for (const auto& sr : run.stage_results) {
    EXPECT_EQ(sr.status, pipeline::StageRunResult::Status::Executed)
        << "stage '" << sr.stage_id
        << "': " << (sr.error ? sr.error->message : std::string{"(no error)"});
  }
}

class AmManufacturabilityTest : public ::testing::Test {
 protected:
  void SetUp() override {
    discovery_ = plugin::discover_plugins({plugins_root()});
    ASSERT_FALSE(discovery_.loaded.empty()) << "no plugins discovered under " << plugins_root();
    loader_ = std::make_unique<plugin::PluginLoader>(registry_, "test-host/0.0.0");
    held_.push_back(load_by_id(*loader_, discovery_, "dev.souxmar.examples.am-layered-mesher"));
    held_.push_back(load_by_id(*loader_, discovery_, "dev.souxmar.examples.am-manufacturability"));
  }

  // Dispatch one solver over a mesh supplied by the caller.
  std::shared_ptr<pipeline::StageOutput> solve(const char* capability_id,
                                               std::shared_ptr<void> mesh_payload,
                                               std::map<std::string, pipeline::Value> inputs) {
    pipeline::RegistryDispatcher dispatcher(registry_);
    std::map<std::string, std::shared_ptr<void>> upstream;
    upstream.emplace("__mesh__", std::move(mesh_payload));
    inputs.emplace("mesh", pipeline::Value::stage_ref("__mesh__"));
    const auto value = pipeline::Value::map(std::move(inputs));
    auto dr = dispatcher.dispatch(pipeline::DispatchContext{capability_id, value, upstream});
    if (auto* err = std::get_if<pipeline::DispatchError>(&dr)) {
      ADD_FAILURE() << capability_id << " dispatch failed: " << err->message;
      return nullptr;
    }
    auto payload = std::get<pipeline::DispatchSuccess>(dr);
    return std::static_pointer_cast<pipeline::StageOutput>(payload);
  }

  plugin::DiscoveryReport discovery_;
  plugin::Registry registry_;
  std::unique_ptr<plugin::PluginLoader> loader_;
  std::vector<plugin::LoadedPlugin> held_;
};

}  // namespace

TEST_F(AmManufacturabilityTest, RegistersThreeSolversUnderExactIds) {
  for (const char* id : {"solver.am.overhang", "solver.am.printability", "solver.am.buildtime"}) {
    ASSERT_NE(registry_.find_solver(id), nullptr) << id;
    // §0.3 explains why these are solvers and not postprocs: a postproc
    // hard-requires an upstream field, and these are mesh-only analyses.
    EXPECT_EQ(registry_.find_postproc(id), nullptr) << id;
    const auto* e = registry_.find(id);
    ASSERT_NE(e, nullptr) << id;
    EXPECT_EQ(e->kind, plugin::CapabilityKind::Solver) << id;
    EXPECT_EQ(e->plugin_id, "dev.souxmar.examples.am-manufacturability") << id;
  }
  EXPECT_EQ(registry_.find("solver.am.overhangs"), nullptr);
  EXPECT_EQ(registry_.find("postproc.am.printability"), nullptr);
  EXPECT_EQ(registry_.find("solver.am.build_time"), nullptr);
}

// The headline DfAM case: horizontal downskin needs support, vertical wall
// does not.
TEST_F(AmManufacturabilityTest, OverhangFlagsHorizontalDownskinAndClearsVerticalWall) {
  std::map<std::string, pipeline::Value> in;
  in.emplace("overhang_threshold_deg", pipeline::Value::number(45.0));
  in.emplace("layer_height", pipeline::Value::number(kEdge));
  auto out = solve("solver.am.overhang", as_upstream_mesh(build_hex_column(2)), std::move(in));
  ASSERT_NE(out, nullptr);
  ASSERT_EQ(out->kind, pipeline::StageOutput::Kind::Field);
  ASSERT_NE(out->field, nullptr);

  const auto& f = *out->field;
  EXPECT_EQ(std::string(f.name()), "overhang");
  EXPECT_EQ(f.location(), core::FieldLocation::Cell);
  EXPECT_EQ(f.kind(), core::FieldKind::Vector);
  EXPECT_EQ(f.components(), 3u);
  ASSERT_EQ(f.count(), 2u);
  EXPECT_EQ(f.num_time_steps(), 1u);

  // cell 0 — the horizontal downskin sitting on the build plate.
  const auto bottom = f.at(0, 0);
  // acos(|n·b|) with |n·b| == 1 is ill-conditioned; 1e-6 degrees covers the
  // clamp any implementation needs around ±1 without admitting a real tilt.
  EXPECT_NEAR(bottom[0], 0.0, 1e-6) << "a -Z boundary face is 0° from the build plate";
  EXPECT_EQ(bottom[1], 1.0) << "0° < 45° threshold, so support is needed";
  // Downskin area is the single 10 x 10 mm face: 1e-4 m². 1e-12 m² is
  // double round-off on a cross product, i.e. 1e-8 relative.
  EXPECT_NEAR(bottom[2], kEdge * kEdge, 1e-12);

  // cell 1 — walls and a top face only.
  const auto top = f.at(1, 0);
  EXPECT_DOUBLE_EQ(top[0], 90.0) << "no downward boundary face ⇒ the contract's 90° sentinel";
  EXPECT_EQ(top[1], 0.0) << "a vertical wall needs no support";
  EXPECT_EQ(top[2], 0.0) << "no downward boundary face ⇒ zero downskin area";
}

// §3.9 requires Tri3/Quad4 surface meshes to be handled. A vertical facet
// has n·b == 0 exactly whichever way it is wound, so its verdict is
// orientation-independent and can be pinned; the horizontal facet's verdict
// depends on the winding the implementation treats as outward, so it is
// pinned only to the two contract-legal outcomes.
TEST_F(AmManufacturabilityTest, OverhangHandlesTri3SurfaceMesh) {
  std::map<std::string, pipeline::Value> in;
  in.emplace("overhang_threshold_deg", pipeline::Value::number(45.0));
  auto out =
      solve("solver.am.overhang", as_upstream_mesh(build_two_triangle_surface()), std::move(in));
  ASSERT_NE(out, nullptr);
  ASSERT_NE(out->field, nullptr);
  const auto& f = *out->field;
  ASSERT_EQ(f.count(), 2u);
  EXPECT_EQ(f.location(), core::FieldLocation::Cell);
  EXPECT_EQ(f.components(), 3u);

  const auto horizontal = f.at(0, 0);
  const bool down = horizontal[1] == 1.0;
  if (down) {
    EXPECT_NEAR(horizontal[0], 0.0, 1e-6);
    EXPECT_NEAR(horizontal[2], 0.5 * kEdge * kEdge, 1e-12);  // right triangle: ab/2
  } else {
    EXPECT_DOUBLE_EQ(horizontal[0], 90.0);
    EXPECT_EQ(horizontal[2], 0.0);
  }

  const auto vertical = f.at(1, 0);
  EXPECT_DOUBLE_EQ(vertical[0], 90.0) << "n·b == 0 is not < 0, so the wall has no downskin";
  EXPECT_EQ(vertical[1], 0.0) << "a vertical wall never needs support";
  EXPECT_EQ(vertical[2], 0.0);
}

// §3.10: the score is a documented heuristic, so the test pins the
// direction of effect (a part that cannot fit the machine scores worse) and
// the enumerated limiting-factor codes, not the weights.
TEST_F(AmManufacturabilityTest, PrintabilityScoreFallsWhenThePartCannotFitTheMachine) {
  const auto run_with_volume = [&](double extent) {
    std::vector<pipeline::Value> volume{pipeline::Value::number(extent),
                                        pipeline::Value::number(extent),
                                        pipeline::Value::number(extent)};
    std::map<std::string, pipeline::Value> in;
    in.emplace("overhang_threshold_deg", pipeline::Value::number(45.0));
    in.emplace("min_wall_thickness", pipeline::Value::number(5.0e-4));
    in.emplace("machine_build_volume", pipeline::Value::list(std::move(volume)));
    in.emplace("layer_height", pipeline::Value::number(kEdge));
    return solve("solver.am.printability", as_upstream_mesh(build_hex_column(2)), std::move(in));
  };

  auto fits = run_with_volume(0.25);       // 250 mm machine, 20 mm part
  auto too_small = run_with_volume(1e-3);  // 1 mm machine, 20 mm part
  ASSERT_NE(fits, nullptr);
  ASSERT_NE(too_small, nullptr);
  ASSERT_NE(fits->field, nullptr);
  ASSERT_NE(too_small->field, nullptr);

  const auto& a = *fits->field;
  EXPECT_EQ(std::string(a.name()), "printability");
  EXPECT_EQ(a.location(), core::FieldLocation::Cell);
  EXPECT_EQ(a.kind(), core::FieldKind::Vector);
  EXPECT_EQ(a.components(), 3u);
  ASSERT_EQ(a.count(), 2u);
  EXPECT_EQ(a.num_time_steps(), 1u);

  const auto& b = *too_small->field;
  double mean_fits = 0.0;
  double mean_small = 0.0;
  bool saw_build_volume_code = false;
  std::ostringstream codes;
  for (std::size_t c = 0; c < 2; ++c) {
    const std::span<const double> va = a.at(c, 0);
    const std::span<const double> vb = b.at(c, 0);
    for (const auto& v : {va, vb}) {
      EXPECT_GE(v[0], 0.0);
      EXPECT_LE(v[0], 1.0);
      // §3.10 enumerates 0..5 and nothing else.
      EXPECT_GE(v[1], 0.0);
      EXPECT_LE(v[1], 5.0);
      EXPECT_EQ(v[1], std::floor(v[1])) << "limiting_factor_code must be integral";
      EXPECT_GT(v[2], 0.0) << "wall_thickness_proxy_m must be positive";
    }
    mean_fits += 0.5 * va[0];
    mean_small += 0.5 * vb[0];
    codes << " cell" << c << "=" << vb[1];
    if (vb[1] == 3.0)
      saw_build_volume_code = true;
  }

  EXPECT_LT(mean_small, mean_fits)
      << "a part that does not fit the build volume must score lower: " << mean_small << " vs "
      << mean_fits;
  EXPECT_TRUE(saw_build_volume_code)
      << "expected limiting_factor_code 3 (build-volume fit) on at least one cell; got"
      << codes.str();
}

// §3.11: "area = volume_in_bin / layer_height" is an exact statement, so it
// is checked exactly; the time model is checked for internal consistency
// (cumulative advances by this layer's time) which holds whether the
// implementation reports cumulative time inclusive or exclusive of the
// current layer.
TEST_F(AmManufacturabilityTest, BuildtimeReportsPerLayerAreaAndCumulativeTime) {
  std::map<std::string, pipeline::Value> in;
  in.emplace("process", pipeline::Value::string("lpbf"));
  in.emplace("layer_height", pipeline::Value::number(kEdge));
  in.emplace("process_layer_height", pipeline::Value::number(3.0e-5));
  in.emplace("scan_speed", pipeline::Value::number(0.8));
  in.emplace("hatch_spacing", pipeline::Value::number(1.1e-4));
  in.emplace("recoat_time", pipeline::Value::number(8.0));
  in.emplace("density", pipeline::Value::number(7990.0));
  auto out = solve("solver.am.buildtime", as_upstream_mesh(build_hex_column(2)), std::move(in));
  ASSERT_NE(out, nullptr);
  ASSERT_NE(out->field, nullptr);

  const auto& f = *out->field;
  EXPECT_EQ(std::string(f.name()), "buildtime");
  EXPECT_EQ(f.location(), core::FieldLocation::Cell);
  EXPECT_EQ(f.kind(), core::FieldKind::Vector);
  EXPECT_EQ(f.components(), 3u);
  ASSERT_EQ(f.count(), 2u);
  EXPECT_EQ(f.num_time_steps(), 1u);

  const auto layer0 = f.at(0, 0);
  const auto layer1 = f.at(1, 0);

  // Each layer bin holds exactly one 10 mm voxel: 1e-6 m³ / 0.01 m = 1e-4 m².
  // 1e-14 m² is round-off on the volume sum (1e-10 relative).
  EXPECT_NEAR(layer0[2], kEdge * kEdge, 1e-14);
  EXPECT_NEAR(layer1[2], kEdge * kEdge, 1e-14);

  EXPECT_GT(layer0[0], 0.0) << "layer_time_s must be positive";
  // Identical layers, identical formula, identical inputs ⇒ identical time.
  EXPECT_NEAR(layer1[0], layer0[0], 1e-9 * layer0[0]);

  EXPECT_GE(layer1[1], layer0[1]) << "cumulative_time_s must not decrease with layer index";
  EXPECT_NEAR(layer1[1] - layer0[1], layer1[0], 1e-6 * layer1[0])
      << "cumulative time must advance by this layer's own layer_time_s";
  EXPECT_GT(layer1[1], 0.0);
}

TEST_F(AmManufacturabilityTest, LayeredMeshThenAllThreeDfamSolversRunToSuccess) {
  std::ostringstream y;
  y << "version: 1\n"
    << "stages:\n"
    << "  - id: mesh\n"
    << "    plugin: mesher.am.layered\n"
    << "    input:\n"
    << "      target_size: 0.005\n"
    << "  - id: overhang\n"
    << "    plugin: solver.am.overhang\n"
    << "    input:\n"
    << "      mesh: { from: mesh }\n"
    << "      overhang_threshold_deg: 45.0\n"
    << "      layer_height: 0.005\n"
    << "      build_direction: [0.0, 0.0, 1.0]\n"
    << "  - id: printability\n"
    << "    plugin: solver.am.printability\n"
    << "    input:\n"
    << "      mesh: { from: mesh }\n"
    << "      overhang_threshold_deg: 45.0\n"
    << "      min_wall_thickness: 5.0e-4\n"
    << "      machine_build_volume: [0.25, 0.25, 0.3]\n"
    << "      layer_height: 0.005\n"
    << "  - id: buildtime\n"
    << "    plugin: solver.am.buildtime\n"
    << "    input:\n"
    << "      mesh: { from: mesh }\n"
    << "      process: lpbf\n"
    << "      layer_height: 0.005\n"
    << "      material_cost_per_kg: 90.0\n"
    << "      machine_rate_per_hour: 45.0\n";

  auto parsed = pipeline::parse_pipeline(y.str());
  ASSERT_TRUE(std::holds_alternative<pipeline::Pipeline>(parsed))
      << std::get<pipeline::ParseError>(parsed).message;

  pipeline::RegistryDispatcher dispatcher(registry_);
  pipeline::Cache cache;
  auto run = pipeline::run_pipeline(std::get<pipeline::Pipeline>(parsed), dispatcher, cache);
  ASSERT_EQ(run.status, pipeline::RunResult::Status::Success)
      << (run.stage_results.empty() || !run.stage_results.back().error
              ? std::string{"(no error)"}
              : run.stage_results.back().error->message);
  ASSERT_EQ(run.stage_results.size(), 4u);
  expect_all_stages_executed(run);

  const auto* mesh_out = static_cast<const pipeline::StageOutput*>(run.outputs.at("mesh").get());
  ASSERT_NE(mesh_out->mesh, nullptr);
  const std::size_t cells = mesh_out->mesh->num_cells();

  for (const char* stage : {"overhang", "printability", "buildtime"}) {
    ASSERT_NE(run.outputs.find(stage), run.outputs.end()) << stage;
    const auto* so = static_cast<const pipeline::StageOutput*>(run.outputs.at(stage).get());
    ASSERT_EQ(so->kind, pipeline::StageOutput::Kind::Field) << stage;
    ASSERT_NE(so->field, nullptr) << stage;
    // The field name is the stage's capability output name, which for all
    // three §3.9-§3.11 solvers is the same word as the stage id used here.
    EXPECT_EQ(std::string(so->field->name()), std::string(stage));
    EXPECT_EQ(so->field->location(), core::FieldLocation::Cell) << stage;
    EXPECT_EQ(so->field->kind(), core::FieldKind::Vector) << stage;
    EXPECT_EQ(so->field->components(), 3u) << stage;
    EXPECT_EQ(so->field->count(), cells) << stage;
    EXPECT_EQ(so->field->num_time_steps(), 1u) << stage;
    for (const double v : so->field->data()) {
      EXPECT_TRUE(std::isfinite(v)) << stage;
    }
  }
}
