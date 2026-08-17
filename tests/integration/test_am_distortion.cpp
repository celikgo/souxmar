// SPDX-License-Identifier: Apache-2.0
//
// Manufacturing block — am-distortion integration.
//
//   mesher.am.layered → solver.am.distortion.inherent_strain
//                     → postproc.am.residual_stress
//
// Written against the frozen capability contract (AM_CONTRACT §1 rows 5-6,
// §3.5, §3.6), not against the plugin source:
//
//   * ids `solver.am.distortion.inherent_strain` (Solver) and
//     `postproc.am.residual_stress` (Postproc).
//   * `distortion_displacement`: FL_NODAL / FK_VECTOR [ux, uy, uz] in m,
//     one triple per node, one step per layer — the field animates the build.
//   * `residual_stress`: FL_CELL / FK_VECTOR, one step, components
//     [0] sigma_vm_Pa, [1] sigma_vm_over_yield, [2] layer_index.
//   * Physics: with `baseplate_clamped` the clamped baseplate nodes stay
//     exactly zero at every step; component [1] is component [0] divided by
//     the supplied yield strength; component [2] is the cell's layer index,
//     which §2.2 defines as the cell tag.
//   * Determinism: two runs in one process are bit-identical.

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
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
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
  return fs::path(SOUXMAR_TEST_AM_DISTORTION_DIR).parent_path();
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

void expect_all_stages_executed(const pipeline::RunResult& run) {
  EXPECT_EQ(run.status, pipeline::RunResult::Status::Success);
  for (const auto& sr : run.stage_results) {
    EXPECT_EQ(sr.status, pipeline::StageRunResult::Status::Executed)
        << "stage '" << sr.stage_id
        << "': " << (sr.error ? sr.error->message : std::string{"(no error)"});
  }
}

std::size_t layer_count_from_tags(const core::Mesh& mesh) {
  std::int32_t max_tag = -1;
  for (std::size_t c = 0; c < mesh.num_cells(); ++c) {
    max_tag = std::max(max_tag, mesh.cell_tag(core::CellIndex{c}).value);
  }
  return max_tag < 0 ? std::size_t{0} : static_cast<std::size_t>(max_tag + 1);
}

void expect_bit_identical(std::span<const double> a, std::span<const double> b, const char* what) {
  ASSERT_EQ(a.size(), b.size()) << what << ": buffer sizes differ";
  EXPECT_EQ(std::memcmp(a.data(), b.data(), a.size_bytes()), 0)
      << what << ": two runs of the same pipeline produced different bytes";
}

constexpr double kTargetSize = 0.005;     // voxel edge == simulation layer height
constexpr double kYieldStrength = 5.0e8;  // Pa, the §3.6 default made explicit

std::string distortion_pipeline_yaml() {
  std::ostringstream y;
  y << "version: 1\n"
    << "stages:\n"
    << "  - id: mesh\n"
    << "    plugin: mesher.am.layered\n"
    << "    input:\n"
    << "      target_size: " << kTargetSize << "\n"
    << "  - id: distort\n"
    << "    plugin: solver.am.distortion.inherent_strain\n"
    << "    input:\n"
    << "      mesh: { from: mesh }\n"
    << "      youngs_modulus: 1.9e11\n"
    << "      poisson_ratio: 0.28\n"
    << "      cte: 1.6e-5\n"
    << "      melt_temperature: 1400.0\n"
    << "      preheat_temperature: 80.0\n"
    << "      strain_calibration: 0.30\n"
    << "      layer_height: " << kTargetSize << "\n"
    << "      build_direction: [0.0, 0.0, 1.0]\n"
    << "      baseplate_layers: 1\n"
    << "      baseplate_clamped: true\n"
    << "  - id: stress\n"
    << "    plugin: postproc.am.residual_stress\n"
    << "    input:\n"
    << "      mesh: { from: mesh }\n"
    << "      field: { from: distort }\n"
    << "      youngs_modulus: 1.9e11\n"
    << "      poisson_ratio: 0.28\n"
    << "      yield_strength: " << kYieldStrength << "\n"
    << "      layer_height: " << kTargetSize << "\n";
  return y.str();
}

class AmDistortionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    discovery_ = plugin::discover_plugins({plugins_root()});
    ASSERT_FALSE(discovery_.loaded.empty()) << "no plugins discovered under " << plugins_root();
    loader_ = std::make_unique<plugin::PluginLoader>(registry_, "test-host/0.0.0");
    held_.push_back(load_by_id(*loader_, discovery_, "dev.souxmar.examples.am-layered-mesher"));
    held_.push_back(load_by_id(*loader_, discovery_, "dev.souxmar.examples.am-distortion"));
  }

  plugin::DiscoveryReport discovery_;
  plugin::Registry registry_;
  std::unique_ptr<plugin::PluginLoader> loader_;
  std::vector<plugin::LoadedPlugin> held_;
};

}  // namespace

TEST_F(AmDistortionTest, RegistersSolverAndPostprocUnderExactIds) {
  ASSERT_NE(registry_.find_solver("solver.am.distortion.inherent_strain"), nullptr);
  ASSERT_NE(registry_.find_postproc("postproc.am.residual_stress"), nullptr);

  EXPECT_EQ(registry_.find_postproc("solver.am.distortion.inherent_strain"), nullptr);
  EXPECT_EQ(registry_.find_solver("postproc.am.residual_stress"), nullptr);

  const auto* solver = registry_.find("solver.am.distortion.inherent_strain");
  ASSERT_NE(solver, nullptr);
  EXPECT_EQ(solver->kind, plugin::CapabilityKind::Solver);
  EXPECT_EQ(solver->plugin_id, "dev.souxmar.examples.am-distortion");

  const auto* postproc = registry_.find("postproc.am.residual_stress");
  ASSERT_NE(postproc, nullptr);
  EXPECT_EQ(postproc->kind, plugin::CapabilityKind::Postproc);
  EXPECT_EQ(postproc->plugin_id, "dev.souxmar.examples.am-distortion");

  EXPECT_EQ(registry_.find("solver.am.distortion"), nullptr);
  EXPECT_EQ(registry_.find("postproc.am.residual_stresses"), nullptr);
}

TEST_F(AmDistortionTest, DistortionThenResidualStressRunsToSuccess) {
  auto parsed = pipeline::parse_pipeline(distortion_pipeline_yaml());
  ASSERT_TRUE(std::holds_alternative<pipeline::Pipeline>(parsed))
      << std::get<pipeline::ParseError>(parsed).message;

  pipeline::RegistryDispatcher dispatcher(registry_);
  pipeline::Cache cache;
  auto run = pipeline::run_pipeline(std::get<pipeline::Pipeline>(parsed), dispatcher, cache);
  ASSERT_EQ(run.status, pipeline::RunResult::Status::Success)
      << (run.stage_results.empty() || !run.stage_results.back().error
              ? std::string{"(no error)"}
              : run.stage_results.back().error->message);
  ASSERT_EQ(run.stage_results.size(), 3u);
  expect_all_stages_executed(run);

  const auto* mesh_out = static_cast<const pipeline::StageOutput*>(run.outputs.at("mesh").get());
  ASSERT_NE(mesh_out->mesh, nullptr);
  const auto& mesh = *mesh_out->mesh;
  const std::size_t layers = layer_count_from_tags(mesh);
  EXPECT_EQ(layers, 4u);  // 0.02 m default build box / 0.005 m layers

  // ---- distortion_displacement: §3.5 ----
  const auto* d_out = static_cast<const pipeline::StageOutput*>(run.outputs.at("distort").get());
  ASSERT_EQ(d_out->kind, pipeline::StageOutput::Kind::Field);
  ASSERT_NE(d_out->field, nullptr);
  const auto& disp = *d_out->field;
  EXPECT_EQ(std::string(disp.name()), "distortion_displacement");
  EXPECT_EQ(disp.location(), core::FieldLocation::Nodal);
  EXPECT_EQ(disp.kind(), core::FieldKind::Vector);
  EXPECT_EQ(disp.components(), 3u);
  EXPECT_EQ(disp.count(), mesh.num_nodes());
  EXPECT_EQ(disp.num_time_steps(), layers);
  EXPECT_EQ(disp.data().size(), mesh.num_nodes() * 3u * layers);
  for (const double v : disp.data()) {
    EXPECT_TRUE(std::isfinite(v));
    // A part-scale residual distortion of over a metre would be nonsense
    // for a 60 mm demo box; this catches a unit or sign blow-up.
    EXPECT_LT(std::abs(v), 1.0);
  }

  // §3.5: "Step j = distortion after depositing layer j, so the field
  // animates the build" — the first and last steps cannot be identical.
  const auto first = disp.step(0);
  const auto last = disp.step(layers - 1);
  ASSERT_EQ(first.size(), last.size());
  EXPECT_NE(std::memcmp(first.data(), last.data(), first.size_bytes()), 0)
      << "the distortion field must evolve between the first and last layer";

  // ---- residual_stress: §3.6 ----
  const auto* s_out = static_cast<const pipeline::StageOutput*>(run.outputs.at("stress").get());
  ASSERT_EQ(s_out->kind, pipeline::StageOutput::Kind::Field);
  ASSERT_NE(s_out->field, nullptr);
  const auto& stress = *s_out->field;
  EXPECT_EQ(std::string(stress.name()), "residual_stress");
  EXPECT_EQ(stress.location(), core::FieldLocation::Cell);
  EXPECT_EQ(stress.kind(), core::FieldKind::Vector);
  EXPECT_EQ(stress.components(), 3u);
  EXPECT_EQ(stress.count(), mesh.num_cells());
  EXPECT_EQ(stress.num_time_steps(), 1u);

  for (std::size_t c = 0; c < stress.count(); ++c) {
    const auto v = stress.at(c, 0);
    EXPECT_GE(v[0], 0.0) << "cell " << c << ": von-Mises equivalent stress is a magnitude";
    // [1] is [0] normalised by the supplied yield strength. Relative
    // tolerance 1e-9 — the only error source is one double division.
    const double expected_ratio = v[0] / kYieldStrength;
    EXPECT_NEAR(v[1], expected_ratio, 1e-9 * std::max(1.0, std::abs(expected_ratio)))
        << "cell " << c << ": sigma_vm_over_yield must equal sigma_vm_Pa / yield_strength";
    // [2] is the layer index, which §2.2 defines as the cell tag when the
    // tag is non-negative. Exact equality: it is copied, not computed.
    const double tag = static_cast<double>(mesh.cell_tag(core::CellIndex{c}).value);
    EXPECT_EQ(v[2], tag) << "cell " << c << ": component [2] must be the layer index";
  }
}

// §3.5: "With `baseplate_clamped`, baseplate-layer nodes stay exactly zero
// at every step." The bottom-most nodes of the build (z == bbox z-min) are
// in baseplate layer 0 under any §2.2-conforming layer resolution, so they
// are the nodes this test pins.
TEST_F(AmDistortionTest, ClampedBaseplateNodesStayExactlyZero) {
  auto parsed = pipeline::parse_pipeline(distortion_pipeline_yaml());
  ASSERT_TRUE(std::holds_alternative<pipeline::Pipeline>(parsed));
  pipeline::RegistryDispatcher dispatcher(registry_);
  pipeline::Cache cache;
  auto run = pipeline::run_pipeline(std::get<pipeline::Pipeline>(parsed), dispatcher, cache);
  ASSERT_EQ(run.status, pipeline::RunResult::Status::Success);

  const auto* mesh_out = static_cast<const pipeline::StageOutput*>(run.outputs.at("mesh").get());
  ASSERT_NE(mesh_out->mesh, nullptr);
  const auto& mesh = *mesh_out->mesh;
  const auto* d_out = static_cast<const pipeline::StageOutput*>(run.outputs.at("distort").get());
  ASSERT_NE(d_out->field, nullptr);
  const auto& disp = *d_out->field;

  const auto bbox = mesh.bounding_box();
  const double z_min = bbox[2];

  std::size_t clamped_nodes = 0;
  for (std::size_t n = 0; n < mesh.num_nodes(); ++n) {
    const auto p = mesh.node(core::NodeIndex{n});
    // 1e-12 m is coordinate round-off, not a physical band: the mesher
    // places the bottom node plane exactly on the bounding-box minimum.
    if (std::abs(p[2] - z_min) > 1e-12)
      continue;
    ++clamped_nodes;
    for (std::size_t step = 0; step < disp.num_time_steps(); ++step) {
      const auto u = disp.at(n, step);
      EXPECT_EQ(u[0], 0.0) << "node " << n << " step " << step << " ux";
      EXPECT_EQ(u[1], 0.0) << "node " << n << " step " << step << " uy";
      EXPECT_EQ(u[2], 0.0) << "node " << n << " step " << step << " uz";
    }
  }
  // 13 x 5 node plane on the default build box at target_size 0.005.
  EXPECT_EQ(clamped_nodes, 65u);

  // The free surface, by contrast, must have moved somewhere by the last
  // step — otherwise "zero on the baseplate" is trivially true everywhere.
  double max_abs = 0.0;
  for (const double v : disp.step(disp.num_time_steps() - 1)) {
    max_abs = std::max(max_abs, std::abs(v));
  }
  EXPECT_GT(max_abs, 0.0) << "a clamped build with a non-zero inherent strain must distort";
}

TEST_F(AmDistortionTest, DistortionAndResidualStressAreBitIdenticalAcrossRuns) {
  pipeline::RegistryDispatcher dispatcher(registry_);
  std::map<std::string, std::shared_ptr<void>> upstream;

  std::map<std::string, pipeline::Value> mesher_in;
  mesher_in.emplace("target_size", pipeline::Value::number(kTargetSize));
  const auto mesher_inputs = pipeline::Value::map(std::move(mesher_in));
  auto mesh_dr =
      dispatcher.dispatch(pipeline::DispatchContext{"mesher.am.layered", mesher_inputs, upstream});
  ASSERT_TRUE(std::holds_alternative<pipeline::DispatchSuccess>(mesh_dr))
      << std::get<pipeline::DispatchError>(mesh_dr).message;
  upstream.emplace("__mesh__", std::get<pipeline::DispatchSuccess>(mesh_dr));

  std::map<std::string, pipeline::Value> solver_in;
  solver_in.emplace("mesh", pipeline::Value::stage_ref("__mesh__"));
  solver_in.emplace("layer_height", pipeline::Value::number(kTargetSize));
  solver_in.emplace("baseplate_layers", pipeline::Value::number(1));
  solver_in.emplace("baseplate_clamped", pipeline::Value::boolean(true));
  const auto solver_inputs = pipeline::Value::map(std::move(solver_in));

  std::vector<std::shared_ptr<void>> runs;
  for (int i = 0; i < 2; ++i) {
    auto dr = dispatcher.dispatch(
        pipeline::DispatchContext{"solver.am.distortion.inherent_strain", solver_inputs, upstream});
    ASSERT_TRUE(std::holds_alternative<pipeline::DispatchSuccess>(dr))
        << std::get<pipeline::DispatchError>(dr).message;
    runs.push_back(std::get<pipeline::DispatchSuccess>(dr));
  }
  const auto* a = static_cast<const pipeline::StageOutput*>(runs[0].get());
  const auto* b = static_cast<const pipeline::StageOutput*>(runs[1].get());
  ASSERT_NE(a->field, nullptr);
  ASSERT_NE(b->field, nullptr);
  EXPECT_NE(a->field.get(), b->field.get()) << "expected two independently computed fields";
  expect_bit_identical(a->field->data(), b->field->data(), "distortion_displacement");

  upstream.emplace("__distort__", runs[0]);
  std::map<std::string, pipeline::Value> pp_in;
  pp_in.emplace("mesh", pipeline::Value::stage_ref("__mesh__"));
  pp_in.emplace("field", pipeline::Value::stage_ref("__distort__"));
  pp_in.emplace("yield_strength", pipeline::Value::number(kYieldStrength));
  const auto pp_inputs = pipeline::Value::map(std::move(pp_in));

  std::vector<std::shared_ptr<void>> pp_runs;
  for (int i = 0; i < 2; ++i) {
    auto dr = dispatcher.dispatch(
        pipeline::DispatchContext{"postproc.am.residual_stress", pp_inputs, upstream});
    ASSERT_TRUE(std::holds_alternative<pipeline::DispatchSuccess>(dr))
        << std::get<pipeline::DispatchError>(dr).message;
    pp_runs.push_back(std::get<pipeline::DispatchSuccess>(dr));
  }
  const auto* pa = static_cast<const pipeline::StageOutput*>(pp_runs[0].get());
  const auto* pb = static_cast<const pipeline::StageOutput*>(pp_runs[1].get());
  ASSERT_NE(pa->field, nullptr);
  ASSERT_NE(pb->field, nullptr);
  expect_bit_identical(pa->field->data(), pb->field->data(), "residual_stress");
}
