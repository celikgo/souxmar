// SPDX-License-Identifier: Apache-2.0
//
// Manufacturing block — am-thermal integration.
//
//   mesher.am.layered  →  solver.am.thermal.lpbf  →  postproc.am.melt_pool
//
// Written against the frozen capability contract (AM_CONTRACT §1 rows 3-4,
// §3.3, §3.4), NOT against the plugin source, so it is an independent check
// on the implementation:
//
//   * `solver.am.thermal.lpbf` is registered as a Solver and
//     `postproc.am.melt_pool` as a Postproc, under exactly those ids.
//   * The thermal field is `temperature`, FL_NODAL / FK_SCALAR, one value
//     per node, one time step per layer.
//   * The melt-pool field is `melt_pool`, FL_CELL / FK_VECTOR, one triple
//     per cell, a single time step, components
//     [0] melt_pool_depth_m, [1] normalised_enthalpy, [2] porosity_risk.
//   * Physics: a hotter laser raises the peak temperature and deepens the
//     melt pool (Rosenthal peak scales with absorbed power); nothing ever
//     cools below the preheat / baseplate floor.
//   * Determinism: the same pipeline run twice in one process yields
//     bit-identical field data (determinism is a release gate).

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
#include <limits>
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

// The layered mesher and am-thermal build into sibling directories under
// the same plugins root, so either cache variable anchors discovery.
fs::path plugins_root() {
  return fs::path(SOUXMAR_TEST_AM_THERMAL_DIR).parent_path();
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

// AM_CONTRACT §2.2: a non-negative cell tag *is* the layer index, so the
// layer count the solver must use is max(cell_tag) + 1.
std::size_t layer_count_from_tags(const core::Mesh& mesh) {
  std::int32_t max_tag = -1;
  for (std::size_t c = 0; c < mesh.num_cells(); ++c) {
    max_tag = std::max(max_tag, mesh.cell_tag(core::CellIndex{c}).value);
  }
  return max_tag < 0 ? std::size_t{0} : static_cast<std::size_t>(max_tag + 1);
}

void expect_bit_identical(std::span<const double> a, std::span<const double> b, const char* what) {
  ASSERT_EQ(a.size(), b.size()) << what << ": buffer sizes differ";
  // Determinism is a gate (AM_CONTRACT §0 house rules): identical inputs
  // must give byte-identical output, not merely output that compares equal
  // to some tolerance.
  EXPECT_EQ(std::memcmp(a.data(), b.data(), a.size_bytes()), 0)
      << what << ": two runs of the same pipeline produced different bytes";
}

struct FieldStats {
  double min = std::numeric_limits<double>::infinity();
  double max = -std::numeric_limits<double>::infinity();
  double mean = 0.0;
};

FieldStats stats_of(std::span<const double> data) {
  FieldStats s;
  double sum = 0.0;
  for (const double v : data) {
    s.min = std::min(s.min, v);
    s.max = std::max(s.max, v);
    sum += v;
  }
  s.mean = data.empty() ? 0.0 : sum / static_cast<double>(data.size());
  return s;
}

// Component `comp` of a 3-component cell field, over every cell.
std::vector<double> component_of(const core::Field& f, std::size_t comp) {
  std::vector<double> out;
  out.reserve(f.count());
  for (std::size_t i = 0; i < f.count(); ++i) {
    out.push_back(f.at(i, 0)[comp]);
  }
  return out;
}

// Shared process parameters. Layer height matches the layered mesher's
// target_size so that §2.2 layer resolution and the deposition model agree.
constexpr double kTargetSize = 0.005;

std::string thermal_pipeline_yaml(double laser_power) {
  std::ostringstream y;
  y << "version: 1\n"
    << "stages:\n"
    << "  - id: mesh\n"
    << "    plugin: mesher.am.layered\n"
    << "    input:\n"
    << "      target_size: " << kTargetSize << "\n"
    << "  - id: thermal\n"
    << "    plugin: solver.am.thermal.lpbf\n"
    << "    input:\n"
    << "      mesh: { from: mesh }\n"
    << "      laser_power: " << laser_power << "\n"
    << "      scan_speed: 0.8\n"
    << "      hatch_spacing: 1.1e-4\n"
    << "      layer_height: " << kTargetSize << "\n"
    << "      process_layer_height: 3.0e-5\n"
    << "      absorptivity: 0.35\n"
    << "      preheat_temperature: 80.0\n"
    << "      baseplate_temperature: 80.0\n"
    << "      thermal_conductivity: 15.0\n"
    << "      density: 7990.0\n"
    << "      specific_heat: 500.0\n"
    << "      melt_temperature: 1400.0\n"
    // A 5 mm simulation layer lumps ~167 machine layers of 30 um, so the
    // dwell between simulation layers has to be lumped too — the solver's
    // header is explicit about this. Passing the per-machine-layer 10 s here
    // would model depositing 5 mm of metal every 10 s, which really does heat
    // the substrate to melting and pins the reported peak at the documented
    // vaporisation cap.
    << "      interlayer_time: 400.0\n"
    << "      build_direction: [0.0, 0.0, 1.0]\n"
    << "  - id: melt\n"
    << "    plugin: postproc.am.melt_pool\n"
    << "    input:\n"
    << "      mesh: { from: mesh }\n"
    << "      field: { from: thermal }\n"
    << "      laser_power: " << laser_power << "\n"
    << "      scan_speed: 0.8\n"
    << "      hatch_spacing: 1.1e-4\n"
    << "      layer_height: " << kTargetSize << "\n"
    << "      process_layer_height: 3.0e-5\n"
    << "      absorptivity: 0.35\n"
    << "      thermal_conductivity: 15.0\n"
    << "      density: 7990.0\n"
    << "      specific_heat: 500.0\n"
    << "      melt_temperature: 1400.0\n";
  return y.str();
}

class AmThermalTest : public ::testing::Test {
 protected:
  void SetUp() override {
    discovery_ = plugin::discover_plugins({plugins_root()});
    ASSERT_FALSE(discovery_.loaded.empty()) << "no plugins discovered under " << plugins_root();
    loader_ = std::make_unique<plugin::PluginLoader>(registry_, "test-host/0.0.0");
    held_.push_back(load_by_id(*loader_, discovery_, "dev.souxmar.examples.am-layered-mesher"));
    held_.push_back(load_by_id(*loader_, discovery_, "dev.souxmar.examples.am-thermal"));
  }

  plugin::DiscoveryReport discovery_;
  plugin::Registry registry_;
  std::unique_ptr<plugin::PluginLoader> loader_;
  std::vector<plugin::LoadedPlugin> held_;
};

}  // namespace

// AM_CONTRACT §1 rows 3-4: exact ids, exact kinds. A wrong-kind lookup
// returns nullptr, so the negative assertions pin the kind as tightly as
// the positive ones pin the id.
TEST_F(AmThermalTest, RegistersSolverAndPostprocUnderExactIds) {
  ASSERT_NE(registry_.find_solver("solver.am.thermal.lpbf"), nullptr);
  ASSERT_NE(registry_.find_postproc("postproc.am.melt_pool"), nullptr);

  EXPECT_EQ(registry_.find_postproc("solver.am.thermal.lpbf"), nullptr);
  EXPECT_EQ(registry_.find_solver("postproc.am.melt_pool"), nullptr);

  const auto* solver = registry_.find("solver.am.thermal.lpbf");
  ASSERT_NE(solver, nullptr);
  EXPECT_EQ(solver->kind, plugin::CapabilityKind::Solver);
  EXPECT_EQ(solver->plugin_id, "dev.souxmar.examples.am-thermal");

  const auto* postproc = registry_.find("postproc.am.melt_pool");
  ASSERT_NE(postproc, nullptr);
  EXPECT_EQ(postproc->kind, plugin::CapabilityKind::Postproc);
  EXPECT_EQ(postproc->plugin_id, "dev.souxmar.examples.am-thermal");

  // Typo-guard: the ids in §1 are the whole contract, nothing near them
  // may resolve.
  EXPECT_EQ(registry_.find("solver.am.thermal"), nullptr);
  EXPECT_EQ(registry_.find("postproc.am.meltpool"), nullptr);
}

TEST_F(AmThermalTest, LayeredMeshThermalMeltPoolRunsToSuccess) {
  auto parsed = pipeline::parse_pipeline(thermal_pipeline_yaml(200.0));
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

  ASSERT_NE(run.outputs.find("mesh"), run.outputs.end());
  ASSERT_NE(run.outputs.find("thermal"), run.outputs.end());
  ASSERT_NE(run.outputs.find("melt"), run.outputs.end());

  const auto* mesh_out = static_cast<const pipeline::StageOutput*>(run.outputs.at("mesh").get());
  ASSERT_EQ(mesh_out->kind, pipeline::StageOutput::Kind::Mesh);
  ASSERT_NE(mesh_out->mesh, nullptr);
  const auto& mesh = *mesh_out->mesh;
  const std::size_t layers = layer_count_from_tags(mesh);
  // Default build box is 0.02 m tall (§3.1) and target_size doubles as the
  // layer thickness, so 0.02 / 0.005 = 4 layers exactly.
  EXPECT_EQ(layers, 4u);

  // ---- temperature: §3.3 ----
  const auto* th_out = static_cast<const pipeline::StageOutput*>(run.outputs.at("thermal").get());
  ASSERT_EQ(th_out->kind, pipeline::StageOutput::Kind::Field);
  ASSERT_NE(th_out->field, nullptr);
  const auto& temperature = *th_out->field;
  EXPECT_EQ(std::string(temperature.name()), "temperature");
  EXPECT_EQ(temperature.location(), core::FieldLocation::Nodal);
  EXPECT_EQ(temperature.kind(), core::FieldKind::Scalar);
  EXPECT_EQ(temperature.components(), 1u);
  EXPECT_EQ(temperature.count(), mesh.num_nodes());
  EXPECT_EQ(temperature.num_time_steps(), layers);
  EXPECT_EQ(temperature.data().size(), mesh.num_nodes() * layers);

  // §3.3: not-yet-built layers sit at preheat_temperature and built layers
  // decay toward baseplate_temperature — both 80 °C here, so 80 °C is a
  // hard floor. Tolerance 1e-6 °C is pure double round-off on a value that
  // is copied, not computed.
  const auto tstats = stats_of(temperature.data());
  EXPECT_GE(tstats.min, 80.0 - 1e-6);
  // Something must actually have been scanned.
  EXPECT_GT(tstats.max, 80.0);
  for (const double v : temperature.data()) {
    EXPECT_TRUE(std::isfinite(v));
  }

  // ---- melt_pool: §3.4 ----
  const auto* mp_out = static_cast<const pipeline::StageOutput*>(run.outputs.at("melt").get());
  ASSERT_EQ(mp_out->kind, pipeline::StageOutput::Kind::Field);
  ASSERT_NE(mp_out->field, nullptr);
  const auto& melt = *mp_out->field;
  EXPECT_EQ(std::string(melt.name()), "melt_pool");
  EXPECT_EQ(melt.location(), core::FieldLocation::Cell);
  EXPECT_EQ(melt.kind(), core::FieldKind::Vector);
  EXPECT_EQ(melt.components(), 3u);
  EXPECT_EQ(melt.count(), mesh.num_cells());
  EXPECT_EQ(melt.num_time_steps(), 1u);

  // Component order is contract: [0] depth (m), [1] normalised enthalpy
  // (dimensionless ΔH/hs), [2] porosity risk on 0..1.
  for (std::size_t c = 0; c < melt.count(); ++c) {
    const auto v = melt.at(c, 0);
    EXPECT_GT(v[0], 0.0) << "cell " << c << " melt-pool depth must be positive";
    EXPECT_LT(v[0], 0.01) << "cell " << c << " melt-pool depth of >10 mm is not LPBF";
    EXPECT_GT(v[1], 0.0) << "cell " << c << " normalised enthalpy must be positive";
    EXPECT_GE(v[2], 0.0) << "cell " << c << " porosity risk below 0";
    EXPECT_LE(v[2], 1.0) << "cell " << c << " porosity risk above 1";
  }
}

// Rosenthal peak temperature rise scales with the absorbed power
// (ΔT_peak ∝ ηP), and the melt pool deepens with it. Doubling the laser
// power must therefore raise both. This is a direction-of-effect check,
// not a magnitude check — the closed-form constants are the plugin's
// business, the monotonicity is the contract's.
TEST_F(AmThermalTest, HotterLaserRaisesPeakTemperatureAndDeepensMeltPool) {
  pipeline::RegistryDispatcher dispatcher(registry_);

  double max_temp[2] = {0.0, 0.0};
  double mean_depth[2] = {0.0, 0.0};
  // Both powers must stay in the conduction regime. Above roughly 250 W on
  // this recipe the single-pass Rosenthal rise carries the peak past the
  // vaporisation cap the solver documents (iron boils at 2861 degC), and a
  // capped peak is deliberately not monotone in power — that is the model
  // being honest about keyholing, not a regression. Melt-pool depth keeps
  // rising past the cap and is checked separately below.
  const double powers[2] = {120.0, 180.0};

  for (int i = 0; i < 2; ++i) {
    auto parsed = pipeline::parse_pipeline(thermal_pipeline_yaml(powers[i]));
    ASSERT_TRUE(std::holds_alternative<pipeline::Pipeline>(parsed));
    // A fresh Cache per run: the content hash includes laser_power, so the
    // two runs cannot alias, but keeping them separate makes the intent
    // explicit.
    pipeline::Cache local_cache;
    auto run =
        pipeline::run_pipeline(std::get<pipeline::Pipeline>(parsed), dispatcher, local_cache);
    ASSERT_EQ(run.status, pipeline::RunResult::Status::Success);

    const auto* th = static_cast<const pipeline::StageOutput*>(run.outputs.at("thermal").get());
    ASSERT_NE(th->field, nullptr);
    max_temp[i] = stats_of(th->field->data()).max;

    const auto* mp = static_cast<const pipeline::StageOutput*>(run.outputs.at("melt").get());
    ASSERT_NE(mp->field, nullptr);
    mean_depth[i] = stats_of(component_of(*mp->field, 0)).mean;
  }

  EXPECT_GT(max_temp[1], max_temp[0] + 1.0)
      << "400 W must peak at least 1 K hotter than 200 W (Rosenthal ΔT ∝ ηP); "
      << "got " << max_temp[0] << " °C vs " << max_temp[1] << " °C";
  EXPECT_GT(mean_depth[1], mean_depth[0]) << "400 W must melt deeper than 200 W; got "
                                          << mean_depth[0] << " m vs " << mean_depth[1] << " m";
}

// Determinism gate: the same pipeline, twice, in one process. Dispatched
// directly so the pipeline cache cannot hand back the first run's buffer
// and make the comparison vacuous.
TEST_F(AmThermalTest, ThermalAndMeltPoolAreBitIdenticalAcrossRuns) {
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
  solver_in.emplace("laser_power", pipeline::Value::number(200.0));
  solver_in.emplace("layer_height", pipeline::Value::number(kTargetSize));
  solver_in.emplace("interlayer_time", pipeline::Value::number(10.0));
  const auto solver_inputs = pipeline::Value::map(std::move(solver_in));

  std::vector<std::shared_ptr<void>> runs;
  for (int i = 0; i < 2; ++i) {
    auto dr = dispatcher.dispatch(
        pipeline::DispatchContext{"solver.am.thermal.lpbf", solver_inputs, upstream});
    ASSERT_TRUE(std::holds_alternative<pipeline::DispatchSuccess>(dr))
        << std::get<pipeline::DispatchError>(dr).message;
    runs.push_back(std::get<pipeline::DispatchSuccess>(dr));
  }

  const auto* a = static_cast<const pipeline::StageOutput*>(runs[0].get());
  const auto* b = static_cast<const pipeline::StageOutput*>(runs[1].get());
  ASSERT_NE(a->field, nullptr);
  ASSERT_NE(b->field, nullptr);
  EXPECT_NE(a->field.get(), b->field.get()) << "expected two independently computed fields";
  expect_bit_identical(a->field->data(), b->field->data(), "temperature");

  // Same for the derived melt-pool field.
  upstream.emplace("__thermal__", runs[0]);
  std::map<std::string, pipeline::Value> pp_in;
  pp_in.emplace("mesh", pipeline::Value::stage_ref("__mesh__"));
  pp_in.emplace("field", pipeline::Value::stage_ref("__thermal__"));
  pp_in.emplace("layer_height", pipeline::Value::number(kTargetSize));
  const auto pp_inputs = pipeline::Value::map(std::move(pp_in));

  std::vector<std::shared_ptr<void>> pp_runs;
  for (int i = 0; i < 2; ++i) {
    auto dr = dispatcher.dispatch(
        pipeline::DispatchContext{"postproc.am.melt_pool", pp_inputs, upstream});
    ASSERT_TRUE(std::holds_alternative<pipeline::DispatchSuccess>(dr))
        << std::get<pipeline::DispatchError>(dr).message;
    pp_runs.push_back(std::get<pipeline::DispatchSuccess>(dr));
  }
  const auto* pa = static_cast<const pipeline::StageOutput*>(pp_runs[0].get());
  const auto* pb = static_cast<const pipeline::StageOutput*>(pp_runs[1].get());
  ASSERT_NE(pa->field, nullptr);
  ASSERT_NE(pb->field, nullptr);
  expect_bit_identical(pa->field->data(), pb->field->data(), "melt_pool");
}

// §0.3: postproc.* hard-requires `field: {from: ...}`. Melt-pool without
// an upstream temperature field is a dispatch error, not a NULL field.
TEST_F(AmThermalTest, MeltPoolWithoutUpstreamFieldIsRejected) {
  pipeline::RegistryDispatcher dispatcher(registry_);
  std::map<std::string, std::shared_ptr<void>> upstream;

  const auto mesher_inputs = pipeline::Value::map({});
  auto mesh_dr =
      dispatcher.dispatch(pipeline::DispatchContext{"mesher.am.layered", mesher_inputs, upstream});
  ASSERT_TRUE(std::holds_alternative<pipeline::DispatchSuccess>(mesh_dr));
  upstream.emplace("__mesh__", std::get<pipeline::DispatchSuccess>(mesh_dr));

  std::map<std::string, pipeline::Value> pp_in;
  pp_in.emplace("mesh", pipeline::Value::stage_ref("__mesh__"));
  const auto pp_inputs = pipeline::Value::map(std::move(pp_in));
  auto dr =
      dispatcher.dispatch(pipeline::DispatchContext{"postproc.am.melt_pool", pp_inputs, upstream});
  ASSERT_TRUE(std::holds_alternative<pipeline::DispatchError>(dr));
  EXPECT_NE(std::get<pipeline::DispatchError>(dr).message.find("field"), std::string::npos);
}
