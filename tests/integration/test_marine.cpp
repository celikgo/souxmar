// SPDX-License-Identifier: Apache-2.0
//
// Marine block — the `marine` plugin (target marine_loads) integration.
//
//   solver.marine.hydrostatic / solver.marine.hull_collapse
//   solver.marine.corrosion   / writer.marine.qualification_report
//
// Written against the frozen capability contract (AM_CONTRACT §1 rows 15-18,
// §3.15 - §3.18), not against the plugin source.
//
// The physics cases run on a two-voxel column built here in the test, so the
// depth of every node is known exactly:
//
//   z = 0.02 m  →  the mesh top, which sits at design_depth x factor
//   z = 0.00 m  →  0.02 m deeper, i.e. rho*g*0.02 Pa more pressure
//
// Everything asserted with EXPECT_NEAR carries the arithmetic that produced
// the expected value, so a failure says which term is wrong.

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
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
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
  return fs::path(SOUXMAR_TEST_MARINE_LOADS_DIR).parent_path();
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

std::string read_file(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

bool contains_ci(const std::string& haystack, std::string needle) {
  std::string hay = haystack;
  std::transform(hay.begin(), hay.end(), hay.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return hay.find(needle) != std::string::npos;
}

constexpr double kEdge = 0.01;           // 10 mm voxel
constexpr double kHeight = 2.0 * kEdge;  // the column is 20 mm tall
constexpr double kRho = 1025.0;          // kg/m³, pinned so the test is exact
constexpr double kGravity = 9.80665;     // m/s², pinned for the same reason
constexpr double kDesignDepth = 300.0;   // m
constexpr double kSafetyFactor = 1.5;

// rho*g*300 = 3 015 544.875 Pa — "3 MPa at 300 m", the classic check.
constexpr double kPressureAt300m = kRho * kGravity * kDesignDepth;

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
    m.add_cell(core::ElementType::Hex8, nodes, core::EntityTag{static_cast<std::int32_t>(k)});
  }
  return m;
}

std::shared_ptr<void> as_upstream_mesh(core::Mesh&& mesh) {
  auto so = std::make_shared<pipeline::StageOutput>();
  so->kind = pipeline::StageOutput::Kind::Mesh;
  so->mesh = std::make_shared<core::Mesh>(std::move(mesh));
  return std::static_pointer_cast<void>(so);
}

pipeline::Value vec3(double x, double y, double z) {
  std::vector<pipeline::Value> v{
      pipeline::Value::number(x), pipeline::Value::number(y), pipeline::Value::number(z)};
  return pipeline::Value::list(std::move(v));
}

class MarineTest : public ::testing::Test {
 protected:
  void SetUp() override {
    discovery_ = plugin::discover_plugins({plugins_root()});
    ASSERT_FALSE(discovery_.loaded.empty()) << "no plugins discovered under " << plugins_root();
    loader_ = std::make_unique<plugin::PluginLoader>(registry_, "test-host/0.0.0");
    held_.push_back(load_by_id(*loader_, discovery_, "dev.souxmar.examples.am-layered-mesher"));
    held_.push_back(load_by_id(*loader_, discovery_, "dev.souxmar.examples.marine"));

    tmp_ = fs::temp_directory_path()
           / ("souxmar-marine-"
              + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
    std::error_code ec;
    fs::remove_all(tmp_, ec);
    fs::create_directories(tmp_);
  }

  void TearDown() override {
    std::error_code ec;
    fs::remove_all(tmp_, ec);
  }

  std::shared_ptr<pipeline::StageOutput> solve(const char* capability_id,
                                               std::map<std::string, pipeline::Value> inputs) {
    pipeline::RegistryDispatcher dispatcher(registry_);
    std::map<std::string, std::shared_ptr<void>> upstream;
    upstream.emplace("__mesh__", as_upstream_mesh(build_hex_column(2)));
    inputs.emplace("mesh", pipeline::Value::stage_ref("__mesh__"));
    const auto value = pipeline::Value::map(std::move(inputs));
    auto dr = dispatcher.dispatch(pipeline::DispatchContext{capability_id, value, upstream});
    if (auto* err = std::get_if<pipeline::DispatchError>(&dr)) {
      ADD_FAILURE() << capability_id << " dispatch failed: " << err->message;
      return nullptr;
    }
    return std::static_pointer_cast<pipeline::StageOutput>(std::get<pipeline::DispatchSuccess>(dr));
  }

  plugin::DiscoveryReport discovery_;
  plugin::Registry registry_;
  std::unique_ptr<plugin::PluginLoader> loader_;
  std::vector<plugin::LoadedPlugin> held_;
  fs::path tmp_;
};

}  // namespace

TEST_F(MarineTest, RegistersThreeSolversAndOneWriterUnderExactIds) {
  for (const char* id :
       {"solver.marine.hydrostatic", "solver.marine.hull_collapse", "solver.marine.corrosion"}) {
    ASSERT_NE(registry_.find_solver(id), nullptr) << id;
    EXPECT_EQ(registry_.find_writer(id), nullptr) << id;
    EXPECT_EQ(registry_.find_postproc(id), nullptr) << id;
    const auto* e = registry_.find(id);
    ASSERT_NE(e, nullptr) << id;
    EXPECT_EQ(e->kind, plugin::CapabilityKind::Solver) << id;
    EXPECT_EQ(e->plugin_id, "dev.souxmar.examples.marine") << id;
  }

  ASSERT_NE(registry_.find_writer("writer.marine.qualification_report"), nullptr);
  EXPECT_EQ(registry_.find_solver("writer.marine.qualification_report"), nullptr);
  const auto* w = registry_.find("writer.marine.qualification_report");
  ASSERT_NE(w, nullptr);
  EXPECT_EQ(w->kind, plugin::CapabilityKind::Writer);
  EXPECT_EQ(w->plugin_id, "dev.souxmar.examples.marine");

  EXPECT_EQ(registry_.find("solver.marine.hydrostatics"), nullptr);
  EXPECT_EQ(registry_.find("writer.marine.qualification"), nullptr);
}

// §3.15: the mesh top sits at design_depth x factor; deeper nodes see
// rho*g*(z_top - z_node) more. With rho, g and the depth pinned there is
// nothing left to estimate.
TEST_F(MarineTest, HydrostaticPressureIsThreeMegapascalAtThreeHundredMetres) {
  std::map<std::string, pipeline::Value> in;
  in.emplace("design_depth", pipeline::Value::number(kDesignDepth));
  std::vector<pipeline::Value> factors{
      pipeline::Value::number(1.0), pipeline::Value::number(1.5), pipeline::Value::number(2.25)};
  in.emplace("depth_factors", pipeline::Value::list(std::move(factors)));
  in.emplace("seawater_density", pipeline::Value::number(kRho));
  in.emplace("gravity", pipeline::Value::number(kGravity));
  in.emplace("include_atmospheric", pipeline::Value::boolean(false));
  in.emplace("build_direction", vec3(0.0, 0.0, 1.0));
  auto out = solve("solver.marine.hydrostatic", std::move(in));
  ASSERT_NE(out, nullptr);
  ASSERT_EQ(out->kind, pipeline::StageOutput::Kind::Field);
  ASSERT_NE(out->field, nullptr);

  const auto& f = *out->field;
  EXPECT_EQ(std::string(f.name()), "hydrostatic_pressure");
  EXPECT_EQ(f.location(), core::FieldLocation::Nodal);
  EXPECT_EQ(f.kind(), core::FieldKind::Scalar);
  EXPECT_EQ(f.components(), 1u);
  EXPECT_EQ(f.count(), 12u);          // 3 node planes x 4 nodes
  EXPECT_EQ(f.num_time_steps(), 3u);  // one load case per depth factor

  // Node layout from build_hex_column: 0-3 at z = 0, 4-7 at z = 0.01,
  // 8-11 at z = 0.02 (the top).
  const double p_top = f.at(8, 0)[0];
  const double p_bottom = f.at(0, 0)[0];

  // 2 Pa on 3.0155 MPa is 0.7 ppm — pure double round-off, since rho, g and
  // the depth are all supplied explicitly.
  EXPECT_NEAR(p_top, kPressureAt300m, 2.0)
      << "expected rho*g*D = " << kRho << " * " << kGravity << " * " << kDesignDepth;
  // And the headline sanity figure: ~3 MPa at 300 m.
  EXPECT_NEAR(p_top, 3.0e6, 2.0e4);

  // The bottom of the column is 0.02 m deeper: rho*g*0.02 = 201.04 Pa.
  EXPECT_NEAR(p_bottom - p_top, kRho * kGravity * kHeight, 0.5)
      << "the depth gradient through the part is wrong";
  EXPECT_GT(p_bottom, p_top) << "deeper nodes must see more pressure";

  // Every node on the top plane shares one depth.
  for (std::size_t n = 8; n < 12; ++n) {
    EXPECT_NEAR(f.at(n, 0)[0], p_top, 1e-9);
  }

  // Load cases scale linearly with the depth factor (1.0 / 1.5 / 2.25).
  EXPECT_NEAR(f.at(8, 1)[0], 1.5 * p_top, 1e-6 * p_top);
  EXPECT_NEAR(f.at(8, 2)[0], 2.25 * p_top, 1e-6 * p_top);
}

// The seawater-density correlation (§3.15: derive from salinity +
// temperature when seawater_density is 0) has to land in the right place.
// 5e4 Pa is 1.7 % of 3 MPa, which admits any rho in [1008, 1042] kg/m³ —
// i.e. every published surface-seawater correlation — but rejects fresh
// water (997) or a missing gravity term.
TEST_F(MarineTest, DerivedSeawaterDensityStillGivesAboutThreeMegapascal) {
  std::map<std::string, pipeline::Value> in;
  in.emplace("design_depth", pipeline::Value::number(kDesignDepth));
  in.emplace("seawater_density", pipeline::Value::number(0.0));  // 0 ⇒ derive
  in.emplace("salinity_psu", pipeline::Value::number(35.0));
  in.emplace("seawater_temperature", pipeline::Value::number(10.0));
  in.emplace("gravity", pipeline::Value::number(kGravity));
  auto out = solve("solver.marine.hydrostatic", std::move(in));
  ASSERT_NE(out, nullptr);
  ASSERT_NE(out->field, nullptr);
  // Default depth_factors are [1.0, 1.5, 2.25] ⇒ 3 load cases.
  EXPECT_EQ(out->field->num_time_steps(), 3u);
  const double p_top = out->field->at(8, 0)[0];
  EXPECT_NEAR(p_top, 3.0e6, 5.0e4);
  // Seawater is denser than fresh water, so the pressure must exceed the
  // fresh-water value at the same depth.
  EXPECT_GT(p_top, 997.0 * kGravity * kDesignDepth);
}

TEST_F(MarineTest, AtmosphericPressureIsAddedExactlyWhenAsked) {
  const auto pressure_at_top = [&](bool atmospheric) {
    std::map<std::string, pipeline::Value> in;
    in.emplace("design_depth", pipeline::Value::number(kDesignDepth));
    in.emplace("seawater_density", pipeline::Value::number(kRho));
    in.emplace("gravity", pipeline::Value::number(kGravity));
    in.emplace("include_atmospheric", pipeline::Value::boolean(atmospheric));
    in.emplace("atmospheric_pressure", pipeline::Value::number(101325.0));
    auto out = solve("solver.marine.hydrostatic", std::move(in));
    if (!out || !out->field)
      return 0.0;
    return out->field->at(8, 0)[0];
  };

  const double without = pressure_at_top(false);
  const double with = pressure_at_top(true);
  ASSERT_GT(without, 0.0);
  // Exactly one standard atmosphere apart; 1 Pa is double round-off.
  EXPECT_NEAR(with - without, 101325.0, 1.0);
}

// §3.16: the same analytical triple on every cell, and the margin is the
// collapse pressure over the factored design pressure — so doubling the
// design depth halves the margin.
TEST_F(MarineTest, CollapseMarginFallsAsDepthRises) {
  const auto collapse_at = [&](double depth) {
    std::map<std::string, pipeline::Value> in;
    in.emplace("hull_type", pipeline::Value::string("ring_stiffened_cylinder"));
    in.emplace("diameter", pipeline::Value::number(1.0));
    in.emplace("thickness", pipeline::Value::number(0.012));
    in.emplace("unsupported_length", pipeline::Value::number(0.5));
    in.emplace("youngs_modulus", pipeline::Value::number(1.9e11));
    in.emplace("poisson_ratio", pipeline::Value::number(0.28));
    in.emplace("yield_strength", pipeline::Value::number(5.0e8));
    in.emplace("design_depth", pipeline::Value::number(depth));
    in.emplace("seawater_density", pipeline::Value::number(kRho));
    in.emplace("gravity", pipeline::Value::number(kGravity));
    in.emplace("imperfection_knockdown", pipeline::Value::number(0.75));
    in.emplace("am_anisotropy_knockdown", pipeline::Value::number(0.90));
    in.emplace("safety_factor", pipeline::Value::number(kSafetyFactor));
    return solve("solver.marine.hull_collapse", std::move(in));
  };

  auto shallow = collapse_at(kDesignDepth);
  auto deep = collapse_at(2.0 * kDesignDepth);
  ASSERT_NE(shallow, nullptr);
  ASSERT_NE(deep, nullptr);
  ASSERT_NE(shallow->field, nullptr);
  ASSERT_NE(deep->field, nullptr);

  const auto& f = *shallow->field;
  EXPECT_EQ(std::string(f.name()), "collapse_margin");
  EXPECT_EQ(f.location(), core::FieldLocation::Cell);
  EXPECT_EQ(f.kind(), core::FieldKind::Vector);
  EXPECT_EQ(f.components(), 3u);
  ASSERT_EQ(f.count(), 2u);
  EXPECT_EQ(f.num_time_steps(), 1u);

  const auto c0 = f.at(0, 0);
  const auto c1 = f.at(1, 0);
  // "the same triple on every cell, documented as analytical rather than
  // mesh-resolved" — so exact equality is the right test.
  EXPECT_EQ(c0[0], c1[0]);
  EXPECT_EQ(c0[1], c1[1]);
  EXPECT_EQ(c0[2], c1[2]);

  EXPECT_GT(c0[0], 0.0) << "collapse pressure must be positive";
  EXPECT_GT(c0[1], 0.0) << "margin must be positive";
  // §3.16 enumerates modes 0..3 and nothing else.
  EXPECT_GE(c0[2], 0.0);
  EXPECT_LE(c0[2], 3.0);
  EXPECT_EQ(c0[2], std::floor(c0[2])) << "governing_mode_code must be integral";

  // margin = collapse ÷ (rho*g*D*safety_factor). 0.5 % relative tolerance:
  // the only arithmetic between the two reported components is a division.
  const double factored_design = kRho * kGravity * kDesignDepth * kSafetyFactor;
  EXPECT_NEAR(c0[1], c0[0] / factored_design, 5e-3 * (c0[0] / factored_design));

  const auto d0 = deep->field->at(0, 0);
  EXPECT_EQ(d0[0], c0[0]) << "collapse pressure depends on the hull, not the depth";
  EXPECT_LT(d0[1], c0[1]) << "the margin must fall as the design depth rises";
  // Twice the depth ⇒ twice the design pressure ⇒ half the margin.
  EXPECT_NEAR(d0[1], 0.5 * c0[1], 5e-3 * c0[1]);
}

// §3.17: thickness loss accumulates over the service life, cathodic
// protection suppresses it, and a galvanic couple only exists when a mating
// alloy is named.
TEST_F(MarineTest, CorrosionRespondsToServiceLifeProtectionAndGalvanicCouple) {
  const auto corrode =
      [&](const char* alloy, const char* mating, double years, bool cathodic_protection) {
        std::map<std::string, pipeline::Value> in;
        in.emplace("alloy", pipeline::Value::string(alloy));
        in.emplace("mating_alloy", pipeline::Value::string(mating));
        in.emplace("area_ratio_cathode_anode", pipeline::Value::number(10.0));
        in.emplace("seawater_temperature", pipeline::Value::number(10.0));
        in.emplace("salinity_psu", pipeline::Value::number(35.0));
        in.emplace("flow_velocity", pipeline::Value::number(0.5));
        in.emplace("oxygen_mg_per_l", pipeline::Value::number(8.0));
        in.emplace("service_life_years", pipeline::Value::number(years));
        in.emplace("cathodic_protection", pipeline::Value::boolean(cathodic_protection));
        in.emplace("coating_efficiency", pipeline::Value::number(0.0));
        in.emplace("as_built_surface", pipeline::Value::boolean(true));
        return solve("solver.marine.corrosion", std::move(in));
      };

  auto base = corrode("316L", "", 25.0, false);
  ASSERT_NE(base, nullptr);
  ASSERT_NE(base->field, nullptr);
  const auto& f = *base->field;
  EXPECT_EQ(std::string(f.name()), "corrosion");
  EXPECT_EQ(f.location(), core::FieldLocation::Cell);
  EXPECT_EQ(f.kind(), core::FieldKind::Vector);
  EXPECT_EQ(f.components(), 3u);
  ASSERT_EQ(f.count(), 2u);
  EXPECT_EQ(f.num_time_steps(), 1u);

  for (std::size_t c = 0; c < f.count(); ++c) {
    const auto v = f.at(c, 0);
    EXPECT_GE(v[0], 0.0) << "cell " << c << ": thickness loss cannot be negative";
    EXPECT_LT(v[0], 100.0) << "cell " << c << ": >100 mm loss in 25 a is not seawater corrosion";
    EXPECT_GE(v[1], 0.0);
    EXPECT_LE(v[1], 1.0) << "cell " << c << ": pitting_risk is a 0..1 score";
    EXPECT_GE(v[2], 0.0);
    EXPECT_LE(v[2], 1.0) << "cell " << c << ": galvanic_risk is a 0..1 score";
  }

  // No mating alloy ⇒ no couple ⇒ no galvanic risk.
  EXPECT_EQ(f.at(0, 0)[2], 0.0) << "an uncoupled alloy has no galvanic risk";

  auto longer = corrode("316L", "", 50.0, false);
  ASSERT_NE(longer, nullptr);
  ASSERT_NE(longer->field, nullptr);
  EXPECT_GT(longer->field->at(0, 0)[0], f.at(0, 0)[0])
      << "50 years must remove more thickness than 25";

  auto protected_run = corrode("316L", "", 25.0, true);
  ASSERT_NE(protected_run, nullptr);
  ASSERT_NE(protected_run->field, nullptr);
  EXPECT_LT(protected_run->field->at(0, 0)[0], f.at(0, 0)[0])
      << "cathodic protection must reduce the thickness loss";

  // AlSi10Mg coupled to IN625 is the worst-case pairing in the galvanic
  // series with a 10:1 cathode:anode area ratio — the aluminium is the
  // anode, so its galvanic risk must be non-zero.
  auto coupled = corrode("AlSi10Mg", "IN625", 25.0, false);
  ASSERT_NE(coupled, nullptr);
  ASSERT_NE(coupled->field, nullptr);
  EXPECT_GT(coupled->field->at(0, 0)[2], 0.0)
      << "a dissimilar-metal couple must raise the galvanic risk above zero";
}

// §3.18: an advisory dossier. The disclaimer and the evidence checklist are
// the point of the document, so they are what the test pins.
TEST_F(MarineTest, QualificationReportCarriesChecklistAndAdvisoryDisclaimer) {
  const auto out_path = tmp_ / "qualification.md";
  std::ostringstream y;
  y << "version: 1\n"
    << "stages:\n"
    << "  - id: mesh\n"
    << "    plugin: mesher.am.layered\n"
    << "    input:\n"
    << "      target_size: 0.005\n"
    << "  - id: corrosion\n"
    << "    plugin: solver.marine.corrosion\n"
    << "    input:\n"
    << "      mesh: { from: mesh }\n"
    << "      alloy: 316L\n"
    << "      service_life_years: 25.0\n"
    << "  - id: dossier\n"
    << "    plugin: writer.marine.qualification_report\n"
    << "    input:\n"
    << "      mesh: { from: mesh }\n"
    << "      field: { from: corrosion }\n"
    << "      path: " << out_path.string() << "\n"
    << "      part_name: AUV pressure hull ring\n"
    << "      process: lpbf\n"
    << "      alloy: 316L\n"
    << "      application: hull\n"
    << "      criticality: 1\n"
    << "      design_depth: " << kDesignDepth << "\n"
    << "      service_life_years: 25.0\n"
    << "      class_framework: generic\n"
    << "      redundancy: false\n";

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
  ASSERT_TRUE(fs::exists(out_path)) << out_path;

  const std::string md = read_file(out_path);
  ASSERT_FALSE(md.empty());
  EXPECT_EQ(md[0], '#') << "Markdown dossier must open with a heading";
  EXPECT_NE(md.find("## "), std::string::npos) << "expected Markdown section headings";

  // Part identification echoes the inputs.
  EXPECT_NE(md.find("AUV pressure hull ring"), std::string::npos);
  EXPECT_NE(md.find("316L"), std::string::npos);
  EXPECT_TRUE(contains_ci(md, "hull"));

  // The advisory-only disclaimer, verbatim on the two load-bearing phrases.
  EXPECT_TRUE(contains_ci(md, "classification society"))
      << "the dossier must state that souxmar is not a classification society";
  EXPECT_TRUE(contains_ci(md, "advisory")) << "the disclaimer must be flagged as advisory-only";
  EXPECT_NE(md.find("DNV-ST-B203"), std::string::npos)
      << "the checklist must cite the public practice it derives from";

  // Qualification-evidence checklist items named in §3.18.
  EXPECT_TRUE(contains_ci(md, "traceab")) << "feedstock traceability";
  EXPECT_TRUE(contains_ci(md, "coupon")) << "witness coupons";
  EXPECT_TRUE(contains_ci(md, "NDT")) << "NDT method + coverage";
  EXPECT_TRUE(contains_ci(md, "build direction") || contains_ci(md, "build-direction"))
      << "build-direction property declaration";

  // Simulation evidence from the supplied field.
  EXPECT_TRUE(contains_ci(md, "corrosion"));

  // No wall clock, no absolute paths (§3.18).
  EXPECT_EQ(md.find(tmp_.string()), std::string::npos)
      << "the dossier must not echo absolute filesystem paths";
}

// One pipeline, every marine capability, straight through.
TEST_F(MarineTest, FullMarinePipelineRunsToSuccess) {
  const auto out_path = tmp_ / "pipeline.md";
  std::ostringstream y;
  y << "version: 1\n"
    << "stages:\n"
    << "  - id: mesh\n"
    << "    plugin: mesher.am.layered\n"
    << "    input:\n"
    << "      target_size: 0.005\n"
    << "  - id: pressure\n"
    << "    plugin: solver.marine.hydrostatic\n"
    << "    input:\n"
    << "      mesh: { from: mesh }\n"
    << "      design_depth: " << kDesignDepth << "\n"
    << "      depth_factors: [1.0, 1.5, 2.25]\n"
    << "      seawater_density: " << kRho << "\n"
    << "      gravity: " << kGravity << "\n"
    << "  - id: collapse\n"
    << "    plugin: solver.marine.hull_collapse\n"
    << "    input:\n"
    << "      mesh: { from: mesh }\n"
    << "      hull_type: ring_stiffened_cylinder\n"
    << "      design_depth: " << kDesignDepth << "\n"
    << "  - id: corrosion\n"
    << "    plugin: solver.marine.corrosion\n"
    << "    input:\n"
    << "      mesh: { from: mesh }\n"
    << "      alloy: 316L\n"
    << "  - id: dossier\n"
    << "    plugin: writer.marine.qualification_report\n"
    << "    input:\n"
    << "      mesh: { from: mesh }\n"
    << "      field: { from: collapse }\n"
    << "      path: " << out_path.string() << "\n"
    << "      part_name: pressure hull ring\n";

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
  ASSERT_EQ(run.stage_results.size(), 5u);
  for (const auto& sr : run.stage_results) {
    EXPECT_EQ(sr.status, pipeline::StageRunResult::Status::Executed)
        << "stage '" << sr.stage_id
        << "': " << (sr.error ? sr.error->message : std::string{"(no error)"});
  }

  const auto* mesh_out = static_cast<const pipeline::StageOutput*>(run.outputs.at("mesh").get());
  ASSERT_NE(mesh_out->mesh, nullptr);
  const std::size_t nodes = mesh_out->mesh->num_nodes();
  const std::size_t cells = mesh_out->mesh->num_cells();

  const auto* p = static_cast<const pipeline::StageOutput*>(run.outputs.at("pressure").get());
  ASSERT_NE(p->field, nullptr);
  EXPECT_EQ(std::string(p->field->name()), "hydrostatic_pressure");
  EXPECT_EQ(p->field->location(), core::FieldLocation::Nodal);
  EXPECT_EQ(p->field->kind(), core::FieldKind::Scalar);
  EXPECT_EQ(p->field->count(), nodes);
  EXPECT_EQ(p->field->num_time_steps(), 3u);

  const auto* c = static_cast<const pipeline::StageOutput*>(run.outputs.at("collapse").get());
  ASSERT_NE(c->field, nullptr);
  EXPECT_EQ(std::string(c->field->name()), "collapse_margin");
  EXPECT_EQ(c->field->location(), core::FieldLocation::Cell);
  EXPECT_EQ(c->field->kind(), core::FieldKind::Vector);
  EXPECT_EQ(c->field->count(), cells);
  EXPECT_EQ(c->field->num_time_steps(), 1u);

  const auto* k = static_cast<const pipeline::StageOutput*>(run.outputs.at("corrosion").get());
  ASSERT_NE(k->field, nullptr);
  EXPECT_EQ(std::string(k->field->name()), "corrosion");
  EXPECT_EQ(k->field->location(), core::FieldLocation::Cell);
  EXPECT_EQ(k->field->kind(), core::FieldKind::Vector);
  EXPECT_EQ(k->field->count(), cells);
  EXPECT_EQ(k->field->num_time_steps(), 1u);

  const auto* w = static_cast<const pipeline::StageOutput*>(run.outputs.at("dossier").get());
  EXPECT_EQ(w->kind, pipeline::StageOutput::Kind::Path);
  EXPECT_TRUE(fs::exists(out_path));
}
