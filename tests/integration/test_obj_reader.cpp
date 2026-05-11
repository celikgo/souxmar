// SPDX-License-Identifier: Apache-2.0
//
// Sprint 8 push 3 integration test — exercises the always-on obj-reader
// plugin through the same `reader.* → writer.vtu` flow that push 4 of
// Sprint 6 verified for STL.
//
// The opt-in blender-reader sibling is exercised on the nightly
// Blender-bearing matrix (Docker image with Blender 4.x pre-staged);
// the default-CI bar lives here.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <variant>

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

// A simple tetrahedron in OBJ form: 4 vertices, 4 triangular faces.
constexpr const char* kTetObj = R"obj(
# unit tetrahedron
v 0 0 0
v 1 0 0
v 0 1 0
v 0 0 1
f 1 2 3
f 1 4 2
f 1 3 4
f 2 4 3
)obj";

// A square as a single quad face; obj-reader fan-triangulates to 2 tris.
constexpr const char* kQuadObj = R"obj(
v 0 0 0
v 1 0 0
v 1 1 0
v 0 1 0
f 1 2 3 4
)obj";

// Tetrahedron expressed with `f v/vt/vn` triples — exercises the
// face-field parser's slash-stripping.
constexpr const char* kTetWithUvNormals = R"obj(
v 0 0 0
v 1 0 0
v 0 1 0
v 0 0 1
vt 0 0
vt 1 0
vt 0 1
vn 0 0 -1
vn 0 -1 0
vn -1 0 0
vn 1 1 1
f 1/1/1 2/2/1 3/3/1
f 1/1/2 4/3/2 2/2/2
f 1/1/3 3/3/3 4/2/3
f 2/1/4 4/2/4 3/3/4
)obj";

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

fs::path stage_obj_to_tempdir(const std::string& subdir,
                              const std::string& filename,
                              const char*        content) {
  const auto tmp = fs::temp_directory_path() / subdir;
  fs::create_directories(tmp);
  const auto p = tmp / filename;
  std::ofstream(p) << content;
  return p;
}

}  // namespace

TEST(ObjReaderEndToEnd, TetrahedronProducesFourTrianglesAndVtu) {
  const fs::path plugins_root =
      fs::path(SOUXMAR_TEST_HELLO_MESHER_DIR).parent_path();
  const auto discovery = plugin::discover_plugins({plugins_root});
  ASSERT_FALSE(discovery.loaded.empty()) << plugins_root;

  plugin::Registry      registry;
  plugin::PluginLoader  loader(registry, "test-host/0.0.0");
  auto _obj    = load_by_id(loader, discovery, "dev.souxmar.examples.obj-reader");
  auto _writer = load_by_id(loader, discovery, "dev.souxmar.examples.vtu-writer");

  ASSERT_NE(registry.find_reader("reader.obj"), nullptr);
  ASSERT_NE(registry.find_writer("writer.vtu"), nullptr);

  const auto obj_path = stage_obj_to_tempdir(
      "souxmar-obj-e2e", "tet.obj", kTetObj);
  const auto vtu_path = obj_path.parent_path() / "tet.vtu";
  fs::remove(vtu_path);

  std::ostringstream yaml;
  yaml << "version: 1\n"
       << "stages:\n"
       << "  - id: read\n"
       << "    plugin: reader.obj\n"
       << "    input:\n"
       << "      path: " << obj_path.string() << "\n"
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
  pipeline::Cache              cache;
  auto run = pipeline::run_pipeline(p, dispatcher, cache);
  ASSERT_EQ(run.status, pipeline::RunResult::Status::Success);
  ASSERT_EQ(run.stage_results.size(), 2u);
  for (const auto& sr : run.stage_results) {
    EXPECT_EQ(sr.status, pipeline::StageRunResult::Status::Executed)
        << "stage '" << sr.stage_id << "' did not execute: "
        << (sr.error ? sr.error->message : std::string{"(no error)"});
  }

  ASSERT_NE(run.outputs.find("read"), run.outputs.end());
  const auto* read_out = static_cast<const pipeline::StageOutput*>(
      run.outputs["read"].get());
  ASSERT_NE(read_out, nullptr);
  ASSERT_EQ(read_out->kind, pipeline::StageOutput::Kind::Mesh);
  ASSERT_NE(read_out->mesh, nullptr);
  EXPECT_EQ(read_out->mesh->num_nodes(), 4u);
  EXPECT_EQ(read_out->mesh->num_cells(), 4u);  // 4 triangular faces.

  EXPECT_TRUE(fs::exists(vtu_path));
  std::error_code ec;
  fs::remove(obj_path, ec);
  fs::remove(vtu_path, ec);
  fs::remove(obj_path.parent_path(), ec);
}

TEST(ObjReaderEndToEnd, QuadFaceFanTriangulatesIntoTwoTris) {
  const fs::path plugins_root =
      fs::path(SOUXMAR_TEST_HELLO_MESHER_DIR).parent_path();
  const auto discovery = plugin::discover_plugins({plugins_root});
  plugin::Registry      registry;
  plugin::PluginLoader  loader(registry, "test-host/0.0.0");
  auto _obj = load_by_id(loader, discovery, "dev.souxmar.examples.obj-reader");

  const auto obj_path = stage_obj_to_tempdir(
      "souxmar-obj-quad", "quad.obj", kQuadObj);

  std::ostringstream yaml;
  yaml << "version: 1\n"
       << "stages:\n"
       << "  - id: read\n"
       << "    plugin: reader.obj\n"
       << "    input:\n"
       << "      path: " << obj_path.string() << "\n";

  auto p = std::get<pipeline::Pipeline>(pipeline::parse_pipeline(yaml.str()));
  pipeline::RegistryDispatcher dispatcher(registry);
  pipeline::Cache              cache;
  auto run = pipeline::run_pipeline(p, dispatcher, cache);
  ASSERT_EQ(run.status, pipeline::RunResult::Status::Success);

  const auto* out = static_cast<const pipeline::StageOutput*>(
      run.outputs["read"].get());
  ASSERT_NE(out, nullptr);
  ASSERT_NE(out->mesh, nullptr);
  EXPECT_EQ(out->mesh->num_nodes(), 4u);
  EXPECT_EQ(out->mesh->num_cells(), 2u);  // 4-vert face → 2 fan tris.

  std::error_code ec;
  fs::remove(obj_path, ec);
  fs::remove(obj_path.parent_path(), ec);
}

TEST(ObjReaderEndToEnd, FaceFieldsWithUvAndNormalsParseCleanly) {
  const fs::path plugins_root =
      fs::path(SOUXMAR_TEST_HELLO_MESHER_DIR).parent_path();
  const auto discovery = plugin::discover_plugins({plugins_root});
  plugin::Registry      registry;
  plugin::PluginLoader  loader(registry, "test-host/0.0.0");
  auto _obj = load_by_id(loader, discovery, "dev.souxmar.examples.obj-reader");

  const auto obj_path = stage_obj_to_tempdir(
      "souxmar-obj-uvn", "tet_uvn.obj", kTetWithUvNormals);

  std::ostringstream yaml;
  yaml << "version: 1\n"
       << "stages:\n"
       << "  - id: read\n"
       << "    plugin: reader.obj\n"
       << "    input:\n"
       << "      path: " << obj_path.string() << "\n";

  auto p = std::get<pipeline::Pipeline>(pipeline::parse_pipeline(yaml.str()));
  pipeline::RegistryDispatcher dispatcher(registry);
  pipeline::Cache              cache;
  auto run = pipeline::run_pipeline(p, dispatcher, cache);
  ASSERT_EQ(run.status, pipeline::RunResult::Status::Success);

  const auto* out = static_cast<const pipeline::StageOutput*>(
      run.outputs["read"].get());
  ASSERT_NE(out, nullptr);
  ASSERT_NE(out->mesh, nullptr);
  EXPECT_EQ(out->mesh->num_nodes(), 4u);
  EXPECT_EQ(out->mesh->num_cells(), 4u);

  std::error_code ec;
  fs::remove(obj_path, ec);
  fs::remove(obj_path.parent_path(), ec);
}
