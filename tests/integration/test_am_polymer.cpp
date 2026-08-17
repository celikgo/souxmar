// SPDX-License-Identifier: Apache-2.0
//
// Manufacturing block — am-polymer integration.
//
//   mesher.am.layered → solver.am.polymer.fff → postproc.am.bond_strength
//
// Written against the frozen capability contract (AM_CONTRACT §1 rows 7-8,
// §3.7, §3.8), not against the plugin source:
//
//   * ids `solver.am.polymer.fff` (Solver) and `postproc.am.bond_strength`
//     (Postproc).
//   * `interface_temperature`: FL_NODAL / FK_SCALAR in °C, one value per
//     node, one step per layer.
//   * `bond_quality`: FL_CELL / FK_VECTOR, one step, components
//     [0] degree_of_healing (0..1), [1] z_strength_fraction (0..1),
//     [2] seconds_above_tg.
//   * Physics: a hotter chamber keeps the interface above the glass
//     transition for longer, so the reptation weld time — and therefore the
//     degree of healing — rises with chamber temperature. Lumped-capacitance
//     cooling also bounds every interface temperature between the coldest
//     environment and the nozzle.

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
#include <filesystem>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace fs = std::filesystem;
using namespace souxmar;

namespace {

fs::path plugins_root() {
  return fs::path(SOUXMAR_TEST_AM_POLYMER_DIR).parent_path();
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

double mean_component(const core::Field& f, std::size_t comp) {
  if (f.count() == 0)
    return 0.0;
  double sum = 0.0;
  for (std::size_t i = 0; i < f.count(); ++i) {
    sum += f.at(i, 0)[comp];
  }
  return sum / static_cast<double>(f.count());
}

constexpr double kTargetSize = 0.005;
constexpr double kNozzle = 250.0;    // °C
constexpr double kBed = 100.0;       // °C
constexpr double kTg = 145.0;        // °C, glass transition
constexpr double kLayerTime = 20.0;  // s

std::string polymer_pipeline_yaml(double chamber_temperature) {
  std::ostringstream y;
  y << "version: 1\n"
    << "stages:\n"
    << "  - id: mesh\n"
    << "    plugin: mesher.am.layered\n"
    << "    input:\n"
    << "      target_size: " << kTargetSize << "\n"
    << "  - id: fff\n"
    << "    plugin: solver.am.polymer.fff\n"
    << "    input:\n"
    << "      mesh: { from: mesh }\n"
    << "      nozzle_temperature: " << kNozzle << "\n"
    << "      bed_temperature: " << kBed << "\n"
    << "      chamber_temperature: " << chamber_temperature << "\n"
    << "      layer_time: " << kLayerTime << "\n"
    << "      layer_height: " << kTargetSize << "\n"
    << "      road_width: 4.0e-4\n"
    << "      convection_coefficient: 30.0\n"
    << "      density: 1010.0\n"
    << "      specific_heat: 1800.0\n"
    << "      thermal_conductivity: 0.25\n"
    << "      glass_transition_temperature: " << kTg << "\n"
    << "      build_direction: [0.0, 0.0, 1.0]\n"
    << "  - id: bond\n"
    << "    plugin: postproc.am.bond_strength\n"
    << "    input:\n"
    << "      mesh: { from: mesh }\n"
    << "      field: { from: fff }\n"
    << "      glass_transition_temperature: " << kTg << "\n"
    << "      reptation_time_reference: 2.0\n"
    << "      reptation_reference_temperature: 260.0\n"
    << "      activation_energy: 8.0e4\n"
    << "      layer_time: " << kLayerTime << "\n";
  return y.str();
}

class AmPolymerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    discovery_ = plugin::discover_plugins({plugins_root()});
    ASSERT_FALSE(discovery_.loaded.empty()) << "no plugins discovered under " << plugins_root();
    loader_ = std::make_unique<plugin::PluginLoader>(registry_, "test-host/0.0.0");
    held_.push_back(load_by_id(*loader_, discovery_, "dev.souxmar.examples.am-layered-mesher"));
    held_.push_back(load_by_id(*loader_, discovery_, "dev.souxmar.examples.am-polymer"));
  }

  plugin::DiscoveryReport discovery_;
  plugin::Registry registry_;
  std::unique_ptr<plugin::PluginLoader> loader_;
  std::vector<plugin::LoadedPlugin> held_;
};

}  // namespace

TEST_F(AmPolymerTest, RegistersSolverAndPostprocUnderExactIds) {
  ASSERT_NE(registry_.find_solver("solver.am.polymer.fff"), nullptr);
  ASSERT_NE(registry_.find_postproc("postproc.am.bond_strength"), nullptr);

  EXPECT_EQ(registry_.find_postproc("solver.am.polymer.fff"), nullptr);
  EXPECT_EQ(registry_.find_solver("postproc.am.bond_strength"), nullptr);

  const auto* solver = registry_.find("solver.am.polymer.fff");
  ASSERT_NE(solver, nullptr);
  EXPECT_EQ(solver->kind, plugin::CapabilityKind::Solver);
  EXPECT_EQ(solver->plugin_id, "dev.souxmar.examples.am-polymer");

  const auto* postproc = registry_.find("postproc.am.bond_strength");
  ASSERT_NE(postproc, nullptr);
  EXPECT_EQ(postproc->kind, plugin::CapabilityKind::Postproc);
  EXPECT_EQ(postproc->plugin_id, "dev.souxmar.examples.am-polymer");

  EXPECT_EQ(registry_.find("solver.am.polymer"), nullptr);
  EXPECT_EQ(registry_.find("postproc.am.bond_quality"), nullptr);
}

TEST_F(AmPolymerTest, FffThenBondStrengthRunsToSuccess) {
  auto parsed = pipeline::parse_pipeline(polymer_pipeline_yaml(40.0));
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
  EXPECT_EQ(layers, 4u);

  // ---- interface_temperature: §3.7 ----
  const auto* f_out = static_cast<const pipeline::StageOutput*>(run.outputs.at("fff").get());
  ASSERT_EQ(f_out->kind, pipeline::StageOutput::Kind::Field);
  ASSERT_NE(f_out->field, nullptr);
  const auto& itemp = *f_out->field;
  EXPECT_EQ(std::string(itemp.name()), "interface_temperature");
  EXPECT_EQ(itemp.location(), core::FieldLocation::Nodal);
  EXPECT_EQ(itemp.kind(), core::FieldKind::Scalar);
  EXPECT_EQ(itemp.components(), 1u);
  EXPECT_EQ(itemp.count(), mesh.num_nodes());
  EXPECT_EQ(itemp.num_time_steps(), layers);
  EXPECT_EQ(itemp.data().size(), mesh.num_nodes() * layers);

  // Lumped-capacitance cooling from a deposition temperature that is at
  // most the nozzle temperature, toward an environment that is at least the
  // coldest of chamber/bed: every value is inside that window. 1e-9 °C is
  // double round-off on the exponential, not a physical band.
  const double lower = std::min(40.0, kBed) - 1e-9;
  const double upper = kNozzle + 1e-9;
  for (const double v : itemp.data()) {
    EXPECT_TRUE(std::isfinite(v));
    EXPECT_GE(v, lower);
    EXPECT_LE(v, upper);
  }

  // ---- bond_quality: §3.8 ----
  const auto* b_out = static_cast<const pipeline::StageOutput*>(run.outputs.at("bond").get());
  ASSERT_EQ(b_out->kind, pipeline::StageOutput::Kind::Field);
  ASSERT_NE(b_out->field, nullptr);
  const auto& bond = *b_out->field;
  EXPECT_EQ(std::string(bond.name()), "bond_quality");
  EXPECT_EQ(bond.location(), core::FieldLocation::Cell);
  EXPECT_EQ(bond.kind(), core::FieldKind::Vector);
  EXPECT_EQ(bond.components(), 3u);
  EXPECT_EQ(bond.count(), mesh.num_cells());
  EXPECT_EQ(bond.num_time_steps(), 1u);

  for (std::size_t c = 0; c < bond.count(); ++c) {
    const auto v = bond.at(c, 0);
    EXPECT_GE(v[0], 0.0) << "cell " << c << " degree_of_healing below 0";
    EXPECT_LE(v[0], 1.0) << "cell " << c << " degree_of_healing above 1";
    EXPECT_GE(v[1], 0.0) << "cell " << c << " z_strength_fraction below 0";
    EXPECT_LE(v[1], 1.0) << "cell " << c << " z_strength_fraction above 1";
    EXPECT_GE(v[2], 0.0) << "cell " << c << " seconds_above_tg cannot be negative";
    // Generous upper bound: even if the model credits every later layer's
    // reheat to this interface, the weld window cannot outlast the whole
    // build (layer_time x layers). This catches a unit slip (ms vs s) or a
    // runaway accumulation, without pinning how many cycles the model sums.
    EXPECT_LE(v[2], kLayerTime * static_cast<double>(layers) + 1e-9)
        << "cell " << c << " seconds_above_tg exceeds the whole build time";
  }
}

// §3.8 physics: the reptation weld time is the time the interface spends
// above Tg, and a hotter chamber raises the whole cooling curve, so both
// the weld window and the degree of healing must rise with chamber
// temperature. Cold = 25 °C (interface drops below the 145 °C Tg quickly),
// hot = 140 °C (the asymptote sits just under Tg, so the interface lingers).
TEST_F(AmPolymerTest, DegreeOfHealingRisesWithAHotterChamber) {
  pipeline::RegistryDispatcher dispatcher(registry_);

  double healing[2] = {0.0, 0.0};
  double seconds_above_tg[2] = {0.0, 0.0};
  double mean_interface[2] = {0.0, 0.0};
  const double chambers[2] = {25.0, 140.0};

  for (int i = 0; i < 2; ++i) {
    auto parsed = pipeline::parse_pipeline(polymer_pipeline_yaml(chambers[i]));
    ASSERT_TRUE(std::holds_alternative<pipeline::Pipeline>(parsed));
    pipeline::Cache cache;
    auto run = pipeline::run_pipeline(std::get<pipeline::Pipeline>(parsed), dispatcher, cache);
    ASSERT_EQ(run.status, pipeline::RunResult::Status::Success);

    const auto* f = static_cast<const pipeline::StageOutput*>(run.outputs.at("fff").get());
    ASSERT_NE(f->field, nullptr);
    double sum = 0.0;
    for (const double v : f->field->data())
      sum += v;
    mean_interface[i] =
        f->field->data().empty() ? 0.0 : sum / static_cast<double>(f->field->data().size());

    const auto* b = static_cast<const pipeline::StageOutput*>(run.outputs.at("bond").get());
    ASSERT_NE(b->field, nullptr);
    healing[i] = mean_component(*b->field, 0);
    seconds_above_tg[i] = mean_component(*b->field, 2);
  }

  EXPECT_GT(mean_interface[1], mean_interface[0])
      << "a hotter chamber must raise the interface temperature history";
  EXPECT_GT(seconds_above_tg[1], seconds_above_tg[0])
      << "a hotter chamber must keep the interface above Tg for longer; got " << seconds_above_tg[0]
      << " s vs " << seconds_above_tg[1] << " s";
  // Degree of healing is clamped at 1 by the reptation law
  // D_h = min(1, (t_weld/t_rep)^0.25), so the strict inequality lives on the
  // weld window above and this stays a >= — a saturated cold case would
  // otherwise make the test unsatisfiable rather than informative.
  EXPECT_GE(healing[1], healing[0])
      << "degree of healing must not fall when the chamber gets hotter; got " << healing[0]
      << " vs " << healing[1];
  EXPECT_GT(healing[1], 0.0) << "a 140 °C chamber with a 250 °C nozzle must heal something";
}
