// SPDX-License-Identifier: Apache-2.0
//
// Sprint 6 push 1 integration: discover + load hello-mesher and the new
// mesh-quality plugin, run a 2-stage pipeline (mesh → mesh_quality),
// inspect the resulting per-cell Field and confirm the components match
// `souxmar::core::quality::summarise`.

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <variant>

#include "souxmar/core/mesh_quality.h"
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

}  // namespace

TEST(MeshQualityPluginEndToEnd, MesherFollowedByMeshQuality) {
  const fs::path plugins_root =
      fs::path(SOUXMAR_TEST_HELLO_MESHER_DIR).parent_path();
  const auto discovery = plugin::discover_plugins({plugins_root});
  ASSERT_FALSE(discovery.loaded.empty()) << plugins_root;

  plugin::Registry      registry;
  plugin::PluginLoader  loader(registry, "test-host/0.0.0");

  auto _mesher  = load_by_id(loader, discovery, "dev.souxmar.examples.hello-mesher");
  auto _quality = load_by_id(loader, discovery, "dev.souxmar.examples.mesh-quality");

  ASSERT_NE(registry.find_mesher("mesher.tetra.hello"),          nullptr);
  ASSERT_NE(registry.find_postproc("postproc.mesh_quality"),     nullptr);

  std::ostringstream yaml;
  yaml << "version: 1\n"
       << "stages:\n"
       << "  - id: mesh\n"
       << "    plugin: mesher.tetra.hello\n"
       << "  - id: quality\n"
       << "    plugin: postproc.mesh_quality\n"
       << "    input:\n"
       << "      mesh: { from: mesh }\n";

  auto parse_result = pipeline::parse_pipeline(yaml.str());
  ASSERT_TRUE(std::holds_alternative<pipeline::Pipeline>(parse_result));
  const auto& p = std::get<pipeline::Pipeline>(parse_result);

  pipeline::RegistryDispatcher dispatcher(registry);
  pipeline::Cache              cache;
  auto run = pipeline::run_pipeline(p, dispatcher, cache);
  ASSERT_EQ(run.status, pipeline::RunResult::Status::Success);
  ASSERT_EQ(run.stage_results.size(), 2u);
  for (const auto& sr : run.stage_results) {
    EXPECT_EQ(sr.status, pipeline::StageRunResult::Status::Executed)
        << "stage '" << sr.stage_id << "' did not execute";
  }

  ASSERT_NE(run.outputs.find("quality"), run.outputs.end());
  const auto* qout = static_cast<const pipeline::StageOutput*>(
      run.outputs["quality"].get());
  ASSERT_NE(qout, nullptr);
  ASSERT_EQ(qout->kind, pipeline::StageOutput::Kind::Field);
  ASSERT_NE(qout->field, nullptr);

  // hello-mesher emits a single tet → one cell, 3 components, 1 step.
  EXPECT_EQ(qout->field->location(), core::FieldLocation::Cell);
  EXPECT_EQ(qout->field->kind(),     core::FieldKind::Vector);
  EXPECT_EQ(qout->field->count(),    1u);
  EXPECT_EQ(qout->field->components(), 3u);
  EXPECT_EQ(qout->field->num_time_steps(), 1u);

  const auto report = core::quality::summarise(qout->field->data(), 1);

  // hello-mesher's tet is a regular-ish unit tet (positively oriented).
  // The exact metric values depend on the mesher's vertex layout, but
  // we can pin the threshold flags: no cell should fall foul of them.
  EXPECT_EQ(report.cells_inverted,        0u);
  EXPECT_EQ(report.cells_sliver_dihedral, 0u);
  EXPECT_EQ(report.cells_extreme_aspect,  0u);
  EXPECT_EQ(report.cells_unsupported,     0u);

  // Volume is positive, edge ratio is finite ≥ 1, min dihedral is finite.
  const auto& vol = report.per_metric[
      static_cast<std::size_t>(core::quality::Metric::SignedVolume)];
  const auto& er  = report.per_metric[
      static_cast<std::size_t>(core::quality::Metric::EdgeRatio)];
  const auto& dih = report.per_metric[
      static_cast<std::size_t>(core::quality::Metric::MinDihedralDeg)];
  EXPECT_GT(vol.min, 0.0);
  EXPECT_GE(er.min,  1.0);
  EXPECT_GT(dih.min, 0.0);
  EXPECT_LE(dih.max, 180.0);
}
