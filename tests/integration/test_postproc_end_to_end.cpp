// SPDX-License-Identifier: Apache-2.0
//
// Sprint 5 push 3 integration: discover + load hello-mesher,
// heat-solver, scalar-magnitude; run a 3-stage pipeline that exercises
// the new postproc.* C ABI surface end-to-end.
//
//   mesh (mesher.tetra.hello)
//   ↓
//   heat (solver.heat.linear)            → nodal scalar field, 3 time steps
//   ↓
//   mag  (postproc.scalar_magnitude)     → nodal scalar field, 3 time steps,
//                                          values = abs() of heat values
//
// Asserts: parse, validation, dispatch, all three stages Executed,
// field handle threading through mesh→heat→mag preserves shape.

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "souxmar/pipeline/cache.h"
#include "souxmar/pipeline/parser.h"
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

TEST(PostprocEndToEnd, MesherHeatScalarMagnitudeChain) {
  // Discovery root is the shared parent of every in-tree plugin's build dir.
  const fs::path plugins_root =
      fs::path(SOUXMAR_TEST_HELLO_MESHER_DIR).parent_path();
  const auto discovery = plugin::discover_plugins({plugins_root});
  ASSERT_FALSE(discovery.loaded.empty()) << plugins_root;

  plugin::Registry      registry;
  plugin::PluginLoader  loader(registry, "test-host/0.0.0");

  auto _mesher = load_by_id(loader, discovery, "dev.souxmar.examples.hello-mesher");
  auto _heat   = load_by_id(loader, discovery, "dev.souxmar.examples.heat-solver");
  auto _mag    = load_by_id(loader, discovery, "dev.souxmar.examples.scalar-magnitude");

  ASSERT_NE(registry.find_mesher("mesher.tetra.hello"),     nullptr);
  ASSERT_NE(registry.find_solver("solver.heat.linear"),     nullptr);
  ASSERT_NE(registry.find_postproc("postproc.scalar_magnitude"), nullptr);

  // Pipeline:
  //   mesh    : mesher.tetra.hello       (no inputs)
  //   heat    : solver.heat.linear       (mesh upstream, time-series options)
  //   mag     : postproc.scalar_magnitude (mesh + field upstream)
  std::ostringstream yaml;
  yaml << "version: 1\n"
       << "stages:\n"
       << "  - id: mesh\n"
       << "    plugin: mesher.tetra.hello\n"
       << "  - id: heat\n"
       << "    plugin: solver.heat.linear\n"
       << "    input:\n"
       << "      mesh: { from: mesh }\n"
       << "      num_time_steps: 3\n"
       << "      dt: 0.5\n"
       << "      tau: 1.0\n"
       << "  - id: mag\n"
       << "    plugin: postproc.scalar_magnitude\n"
       << "    input:\n"
       << "      mesh:  { from: mesh }\n"
       << "      field: { from: heat }\n";

  auto parse_result = pipeline::parse_pipeline(yaml.str());
  ASSERT_TRUE(std::holds_alternative<pipeline::Pipeline>(parse_result))
      << "parse failed: " << std::get<pipeline::ParseError>(parse_result).message;
  const auto& p = std::get<pipeline::Pipeline>(parse_result);

  pipeline::RegistryDispatcher dispatcher(registry);
  pipeline::Cache              cache;
  auto run = pipeline::run_pipeline(p, dispatcher, cache);

  ASSERT_EQ(run.status, pipeline::RunResult::Status::Success)
      << "validation_errors=" << (run.validation_errors.empty() ? "" : run.validation_errors[0]);
  ASSERT_EQ(run.stage_results.size(), 3u);
  for (const auto& sr : run.stage_results) {
    EXPECT_EQ(sr.status, pipeline::StageRunResult::Status::Executed)
        << "stage '" << sr.stage_id << "' did not execute: "
        << (sr.error ? sr.error->message : std::string{"(no error)"});
  }

  // Inspect the postproc output: it should be a Field StageOutput with
  // num_time_steps == 3 (same as the heat solver produced).
  ASSERT_NE(run.outputs.find("mag"), run.outputs.end());
  const auto* mag_out = static_cast<const pipeline::StageOutput*>(
      run.outputs["mag"].get());
  ASSERT_NE(mag_out, nullptr);
  EXPECT_EQ(mag_out->kind, pipeline::StageOutput::Kind::Field);
  ASSERT_NE(mag_out->field, nullptr);
  EXPECT_EQ(mag_out->field->num_time_steps(), 3u);
  EXPECT_EQ(static_cast<int>(mag_out->field->kind()),
            static_cast<int>(souxmar::core::FieldKind::Scalar));
  // The hello-mesher emits 4 nodes; the heat solver writes a per-node
  // value; scalar-magnitude carries shape forward.
  EXPECT_EQ(mag_out->field->count(), 4u);
}

TEST(PostprocEndToEnd, MissingFieldUpstreamRejectedAtPostprocStage) {
  // Build a registry with just the postproc plugin loaded; run a
  // pipeline that names postproc.* but omits the required field upstream.
  const fs::path plugins_root =
      fs::path(SOUXMAR_TEST_HELLO_MESHER_DIR).parent_path();
  const auto discovery = plugin::discover_plugins({plugins_root});

  plugin::Registry      registry;
  plugin::PluginLoader  loader(registry, "test-host/0.0.0");
  auto _mesher = load_by_id(loader, discovery, "dev.souxmar.examples.hello-mesher");
  auto _mag    = load_by_id(loader, discovery, "dev.souxmar.examples.scalar-magnitude");

  std::string yaml = R"yaml(
version: 1
stages:
  - id: mesh
    plugin: mesher.tetra.hello
  - id: mag
    plugin: postproc.scalar_magnitude
    input:
      mesh: { from: mesh }
)yaml";
  auto p = std::get<pipeline::Pipeline>(pipeline::parse_pipeline(yaml));

  pipeline::RegistryDispatcher dispatcher(registry);
  pipeline::Cache cache;
  auto run = pipeline::run_pipeline(p, dispatcher, cache);
  EXPECT_EQ(run.status, pipeline::RunResult::Status::StageFailed);
  bool saw_field_error = false;
  for (const auto& sr : run.stage_results) {
    if (sr.status == pipeline::StageRunResult::Status::Failed &&
        sr.error && sr.error->message.find("field") != std::string::npos) {
      saw_field_error = true;
    }
  }
  EXPECT_TRUE(saw_field_error) << "expected postproc dispatch to reject missing field upstream";
}

}  // namespace
