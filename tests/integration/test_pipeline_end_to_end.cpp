// SPDX-License-Identifier: Apache-2.0
//
// End-to-end pipeline test — the canary for Sprint 3 push 2.
//
// Steps:
//   1. Discover hello-mesher and hello-writer in their built directories.
//   2. Load both into a Registry via PluginLoader.
//   3. Parse a 2-stage pipeline: mesher.tetra.hello -> writer.text-summary.
//   4. Run it through RegistryDispatcher + Cache.
//   5. Verify the output file contains the expected summary line.
//   6. Re-run; verify cache hits skip dispatch.
//
// When this test passes on all three OSes, the entire critical path is
// proven: manifest -> discovery -> ABI handshake -> registration -> YAML
// parse -> DAG validate -> dispatch via C ABI -> mesh accessors -> writer
// vtable -> output -> RAII unload -> registry empties.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

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

// Resolve the parent directory of a built plugin so discover_plugins()
// can walk it (discovery scans subdirectories of the search root).
fs::path search_root_for(const fs::path& plugin_build_dir) {
  return plugin_build_dir.parent_path();
}

// Discover-and-load helper. Returns the LoadedPlugin so the caller owns its
// lifetime — the LoadedPlugin must outlive the Registry it was loaded into,
// otherwise its destructor would call remove_plugin on a dead registry.
//
// Throws std::runtime_error on any step's failure — gtest catches and
// reports as a test failure. ASSERT_* macros can't be used here because
// gtest requires the surrounding function to return void.
plugin::LoadedPlugin load_plugin_from_dir(plugin::PluginLoader& loader,
                                          const fs::path&       plugin_build_dir,
                                          const std::string&    expected_id) {
  const auto report = plugin::discover_plugins({search_root_for(plugin_build_dir)});
  if (report.loaded.empty()) {
    throw std::runtime_error(
        "discovery returned no plugins for " + plugin_build_dir.string() +
        " (rejected: " +
        (report.rejected.empty() ? std::string{"<none>"} : report.rejected[0].reason) + ")");
  }

  const plugin::DiscoveredPlugin* found = nullptr;
  for (const auto& p : report.loaded) {
    if (p.manifest.id == expected_id) found = &p;
  }
  if (!found) {
    throw std::runtime_error("did not find plugin id '" + expected_id + "'");
  }

  auto load_result = loader.load(*found);
  if (!std::holds_alternative<plugin::LoadedPlugin>(load_result)) {
    throw std::runtime_error(
        "load failed: " + std::get<plugin::LoadError>(load_result).message);
  }
  return std::move(std::get<plugin::LoadedPlugin>(load_result));
}

fs::path unique_tmp_path(std::string_view tag) {
  std::random_device rd;
  std::string name = "souxmar-it-";
  name.append(tag);
  name.append("-");
  name.append(std::to_string(rd()));
  name.append(".txt");
  return fs::temp_directory_path() / name;
}

TEST(EndToEnd, MesherToWriterPipeline) {
  plugin::Registry      registry;
  plugin::PluginLoader  loader(registry, "test-host/0.0.0");

  auto _mesher = load_plugin_from_dir(loader,
                                       SOUXMAR_TEST_HELLO_MESHER_DIR,
                                       "dev.souxmar.examples.hello-mesher");
  auto _writer = load_plugin_from_dir(loader,
                                       SOUXMAR_TEST_HELLO_WRITER_DIR,
                                       "dev.souxmar.examples.hello-writer");

  ASSERT_NE(registry.find_mesher("mesher.tetra.hello"), nullptr);
  ASSERT_NE(registry.find_writer("writer.text-summary"), nullptr);

  const fs::path output = unique_tmp_path("e2e");
  if (fs::exists(output)) fs::remove(output);

  std::ostringstream yaml;
  yaml << "version: 1\n"
       << "stages:\n"
       << "  - id: mesh\n"
       << "    plugin: mesher.tetra.hello\n"
       << "  - id: write\n"
       << "    plugin: writer.text-summary\n"
       << "    input:\n"
       << "      mesh: { from: mesh }\n"
       << "      path: " << output.string() << "\n";

  auto parse_result = pipeline::parse_pipeline(yaml.str());
  ASSERT_TRUE(std::holds_alternative<pipeline::Pipeline>(parse_result))
      << "parse failed: " << std::get<pipeline::ParseError>(parse_result).message;
  const auto& p = std::get<pipeline::Pipeline>(parse_result);

  pipeline::RegistryDispatcher dispatcher(registry);
  pipeline::Cache              cache;

  auto run = pipeline::run_pipeline(p, dispatcher, cache);
  ASSERT_EQ(run.status, pipeline::RunResult::Status::Success);
  for (const auto& sr : run.stage_results) {
    EXPECT_NE(sr.status, pipeline::StageRunResult::Status::Failed)
        << "stage '" << sr.stage_id << "' failed: "
        << (sr.error ? sr.error->message : std::string{"(no message)"});
  }

  // The writer wrote our 1-tet mesh summary to disk.
  ASSERT_TRUE(fs::exists(output)) << "writer did not produce output at " << output;
  std::ifstream in(output);
  std::string contents((std::istreambuf_iterator<char>(in)), {});
  EXPECT_NE(contents.find("num_nodes=4"), std::string::npos) << contents;
  EXPECT_NE(contents.find("num_cells=1"), std::string::npos) << contents;

  // Cleanup so repeated runs don't leave stragglers in /tmp.
  fs::remove(output);
}

TEST(EndToEnd, CacheHitsSkipDispatchOnRerun) {
  plugin::Registry      registry;
  plugin::PluginLoader  loader(registry, "test-host/0.0.0");
  auto _mesher = load_plugin_from_dir(loader,
                                       SOUXMAR_TEST_HELLO_MESHER_DIR,
                                       "dev.souxmar.examples.hello-mesher");
  auto _writer = load_plugin_from_dir(loader,
                                       SOUXMAR_TEST_HELLO_WRITER_DIR,
                                       "dev.souxmar.examples.hello-writer");

  const fs::path output = unique_tmp_path("e2e-cache");
  if (fs::exists(output)) fs::remove(output);

  std::ostringstream yaml;
  yaml << "version: 1\n"
       << "stages:\n"
       << "  - id: mesh\n"
       << "    plugin: mesher.tetra.hello\n"
       << "  - id: write\n"
       << "    plugin: writer.text-summary\n"
       << "    input:\n"
       << "      mesh: { from: mesh }\n"
       << "      path: " << output.string() << "\n";
  const auto p = std::get<pipeline::Pipeline>(pipeline::parse_pipeline(yaml.str()));

  pipeline::RegistryDispatcher dispatcher(registry);
  pipeline::Cache              cache;

  auto run1 = pipeline::run_pipeline(p, dispatcher, cache);
  ASSERT_EQ(run1.status, pipeline::RunResult::Status::Success);

  auto run2 = pipeline::run_pipeline(p, dispatcher, cache);
  EXPECT_EQ(run2.status, pipeline::RunResult::Status::Success);
  for (const auto& sr : run2.stage_results) {
    EXPECT_EQ(sr.status, pipeline::StageRunResult::Status::Cached)
        << "stage '" << sr.stage_id << "' was not cached on rerun";
  }

  fs::remove(output);
}

TEST(EndToEnd, MissingMeshReferenceRejected) {
  plugin::Registry      registry;
  plugin::PluginLoader  loader(registry, "test-host/0.0.0");
  auto _writer = load_plugin_from_dir(loader,
                                       SOUXMAR_TEST_HELLO_WRITER_DIR,
                                       "dev.souxmar.examples.hello-writer");

  // Writer needs a `mesh: {from: ...}`; we omit it.
  std::string yaml = R"yaml(
version: 1
stages:
  - id: write
    plugin: writer.text-summary
    input:
      path: /tmp/should_not_exist
)yaml";
  const auto p = std::get<pipeline::Pipeline>(pipeline::parse_pipeline(yaml));

  pipeline::RegistryDispatcher dispatcher(registry);
  pipeline::Cache cache;
  auto run = pipeline::run_pipeline(p, dispatcher, cache);
  EXPECT_EQ(run.status, pipeline::RunResult::Status::StageFailed);
  ASSERT_EQ(run.stage_results.size(), 1u);
  ASSERT_TRUE(run.stage_results[0].error.has_value());
  EXPECT_NE(run.stage_results[0].error->message.find("mesh"),
            std::string::npos);
}

}  // namespace
