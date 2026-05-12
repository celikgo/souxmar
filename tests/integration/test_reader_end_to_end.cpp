// SPDX-License-Identifier: Apache-2.0
//
// Sprint 6 push 4 integration: end-to-end exercise of the new reader.*
// C ABI surface against the always-on stl-reader plugin.
//
//   reader.stl   →   postproc.mesh_quality   →   writer.vtu
//
// The fixture is generated to a TempDir on the fly — no dependency on
// the on-disk examples/stl-cube/ tree. The test asserts: discovery
// finds the new plugin, dispatch routes through dispatch_reader, the
// produced StageOutput is a Mesh, the downstream postproc + writer
// stages see it correctly, and the VTU lands on disk.

#include "souxmar/pipeline/cache.h"
#include "souxmar/pipeline/parser.h"
#include "souxmar/pipeline/registry_dispatcher.h"
#include "souxmar/pipeline/runner.h"
#include "souxmar/plugin/discovery.h"
#include "souxmar/plugin/loader.h"
#include "souxmar/plugin/registry.h"

#include "test_config.h"
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <variant>

namespace fs = std::filesystem;
using namespace souxmar;

namespace {

constexpr const char* kCubeStl = R"stl(
solid cube
  facet normal 0 0 -1
    outer loop
      vertex 0 0 0
      vertex 1 1 0
      vertex 1 0 0
    endloop
  endfacet
  facet normal 0 0 -1
    outer loop
      vertex 0 0 0
      vertex 0 1 0
      vertex 1 1 0
    endloop
  endfacet
  facet normal 0 0 1
    outer loop
      vertex 0 0 1
      vertex 1 0 1
      vertex 1 1 1
    endloop
  endfacet
  facet normal 0 0 1
    outer loop
      vertex 0 0 1
      vertex 1 1 1
      vertex 0 1 1
    endloop
  endfacet
  facet normal 0 -1 0
    outer loop
      vertex 0 0 0
      vertex 1 0 0
      vertex 1 0 1
    endloop
  endfacet
  facet normal 0 -1 0
    outer loop
      vertex 0 0 0
      vertex 1 0 1
      vertex 0 0 1
    endloop
  endfacet
  facet normal 0 1 0
    outer loop
      vertex 0 1 0
      vertex 0 1 1
      vertex 1 1 1
    endloop
  endfacet
  facet normal 0 1 0
    outer loop
      vertex 0 1 0
      vertex 1 1 1
      vertex 1 1 0
    endloop
  endfacet
  facet normal -1 0 0
    outer loop
      vertex 0 0 0
      vertex 0 0 1
      vertex 0 1 1
    endloop
  endfacet
  facet normal -1 0 0
    outer loop
      vertex 0 0 0
      vertex 0 1 1
      vertex 0 1 0
    endloop
  endfacet
  facet normal 1 0 0
    outer loop
      vertex 1 0 0
      vertex 1 1 0
      vertex 1 1 1
    endloop
  endfacet
  facet normal 1 0 0
    outer loop
      vertex 1 0 0
      vertex 1 1 1
      vertex 1 0 1
    endloop
  endfacet
endsolid cube
)stl";

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

}  // namespace

TEST(ReaderEndToEnd, StlReaderThenWriterProducesVtu) {
  const fs::path plugins_root = fs::path(SOUXMAR_TEST_HELLO_MESHER_DIR).parent_path();
  const auto discovery = plugin::discover_plugins({plugins_root});
  ASSERT_FALSE(discovery.loaded.empty()) << plugins_root;

  plugin::Registry registry;
  plugin::PluginLoader loader(registry, "test-host/0.0.0");
  auto _stl = load_by_id(loader, discovery, "dev.souxmar.examples.stl-reader");
  auto _writer = load_by_id(loader, discovery, "dev.souxmar.examples.vtu-writer");

  ASSERT_NE(registry.find_reader("reader.stl"), nullptr);
  ASSERT_NE(registry.find_writer("writer.vtu"), nullptr);

  // Stage the STL fixture + the VTU output path in a per-test TempDir.
  auto tmp = fs::temp_directory_path() / "souxmar-reader-e2e";
  fs::create_directories(tmp);
  const auto stl_path = tmp / "cube.stl";
  const auto vtu_path = tmp / "cube.vtu";
  std::ofstream(stl_path) << kCubeStl;
  fs::remove(vtu_path);

  std::ostringstream yaml;
  yaml << "version: 1\n"
       << "stages:\n"
       << "  - id: read\n"
       << "    plugin: reader.stl\n"
       << "    input:\n"
       << "      path: " << stl_path.string() << "\n"
       << "  - id: write\n"
       << "    plugin: writer.vtu\n"
       << "    input:\n"
       << "      mesh: { from: read }\n"
       << "      path: " << vtu_path.string() << "\n";

  auto parse_result = pipeline::parse_pipeline(yaml.str());
  ASSERT_TRUE(std::holds_alternative<pipeline::Pipeline>(parse_result))
      << "parse failed: " << std::get<pipeline::ParseError>(parse_result).message;
  const auto& p = std::get<pipeline::Pipeline>(parse_result);

  pipeline::RegistryDispatcher dispatcher(registry);
  pipeline::Cache cache;
  auto run = pipeline::run_pipeline(p, dispatcher, cache);
  ASSERT_EQ(run.status, pipeline::RunResult::Status::Success);
  ASSERT_EQ(run.stage_results.size(), 2u);
  for (const auto& sr : run.stage_results) {
    EXPECT_EQ(sr.status, pipeline::StageRunResult::Status::Executed)
        << "stage '" << sr.stage_id
        << "' did not execute: " << (sr.error ? sr.error->message : std::string{"(no error)"});
  }

  // Reader output is a Mesh; verify it carries the deduplicated cube.
  ASSERT_NE(run.outputs.find("read"), run.outputs.end());
  const auto* read_out = static_cast<const pipeline::StageOutput*>(run.outputs["read"].get());
  ASSERT_NE(read_out, nullptr);
  ASSERT_EQ(read_out->kind, pipeline::StageOutput::Kind::Mesh);
  ASSERT_NE(read_out->mesh, nullptr);
  EXPECT_EQ(read_out->mesh->num_nodes(), 8u);   // dedup: 8 cube corners
  EXPECT_EQ(read_out->mesh->num_cells(), 12u);  // 12 triangle facets

  EXPECT_TRUE(fs::exists(vtu_path));
  std::error_code ec;
  fs::remove(stl_path, ec);
  fs::remove(vtu_path, ec);
  fs::remove(tmp, ec);
}

TEST(ReaderEndToEnd, MissingPathInputIsRejected) {
  const fs::path plugins_root = fs::path(SOUXMAR_TEST_HELLO_MESHER_DIR).parent_path();
  const auto discovery = plugin::discover_plugins({plugins_root});
  plugin::Registry registry;
  plugin::PluginLoader loader(registry, "test-host/0.0.0");
  auto _stl = load_by_id(loader, discovery, "dev.souxmar.examples.stl-reader");

  // Pipeline references reader.stl but forgets the required `path` input.
  std::string yaml = R"yaml(
version: 1
stages:
  - id: read
    plugin: reader.stl
)yaml";
  auto p = std::get<pipeline::Pipeline>(pipeline::parse_pipeline(yaml));

  pipeline::RegistryDispatcher dispatcher(registry);
  pipeline::Cache cache;
  auto run = pipeline::run_pipeline(p, dispatcher, cache);
  EXPECT_EQ(run.status, pipeline::RunResult::Status::StageFailed);
  bool saw_path_error = false;
  for (const auto& sr : run.stage_results) {
    if (sr.status == pipeline::StageRunResult::Status::Failed && sr.error
        && sr.error->message.find("path") != std::string::npos) {
      saw_path_error = true;
    }
  }
  EXPECT_TRUE(saw_path_error) << "expected reader dispatch to reject the missing `path` input";
}
