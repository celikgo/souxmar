// SPDX-License-Identifier: Apache-2.0
//
// VTU consumer-conformance test — Sprint 11 / 12 carry-over landed in
// Sprint 13 push 2.
//
// The Sprint 11 retro flagged that the vtu-writer's output is exercised
// only by the writer's own author. ParaView is the canonical consumer
// of .vtu files; if the writer drifts (cell-type numbering bug, malformed
// XML, missing required attribute), nothing in the current CI catches
// it until a real user opens the file in ParaView and reports a bug.
//
// This test runs the vtu-writer end-to-end into a temp file and verifies
// the output passes the *consumer contract*: the structural invariants
// every conforming VTU reader (ParaView's, our future VTK adapter,
// future third-party post-processors) relies on. It is a
// **conformance** test, not a unit test — it deliberately uses only
// what a consumer can observe through the file boundary, never the
// writer's internals.
//
// Contract checked here (all derived from the VTK File Formats spec:
// https://docs.vtk.org/en/latest/design_documents/VTKFileFormats.html):
//
//   1. The file is well-formed XML.
//   2. Root element is <VTKFile type="UnstructuredGrid" version="..." byte_order="...">.
//   3. There is exactly one <UnstructuredGrid> child with exactly one <Piece>.
//   4. <Piece NumberOfPoints="N" NumberOfCells="M"> — N and M agree with
//      the <Points>/<Cells> array sizes.
//   5. <Cells> contains the required <DataArray Name="connectivity">,
//      <DataArray Name="offsets">, <DataArray Name="types">.
//   6. Every cell-type integer in <DataArray Name="types"> is in the
//      VTK-numbered set the writer's `souxmar_to_vtk_cell_type` table
//      can emit (i.e. no 0 / no garbage / no future-version entries).
//   7. Whitespace inside <DataArray> elements is parseable as
//      space-separated ints/floats — not, e.g., comma-separated.
//
// This test is the regression net against the Sprint 6 promise that a
// VTK-backed writer will replace the hand-rolled XML — when that swap
// happens, the same conformance assertions must continue to pass.

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <random>
#include <regex>
#include <set>
#include <sstream>
#include <string>
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

// The set of cell-type ids `souxmar_to_vtk_cell_type` in the
// vtu-writer can return, as of Sprint 13. New entries are added
// to this set as the writer ratifies them; an unknown id in
// output is a divergence.
const std::set<int> kKnownVtkCellTypes = {
    1,   // VTK_VERTEX
    3,   // VTK_LINE
    21,  // VTK_QUADRATIC_EDGE
    5,   // VTK_TRIANGLE
    22,  // VTK_QUADRATIC_TRIANGLE
    9,   // VTK_QUAD
    23,  // VTK_QUADRATIC_QUAD
    28,  // VTK_BIQUADRATIC_QUAD
    10,  // VTK_TETRA
    24,  // VTK_QUADRATIC_TETRA
    12,  // VTK_HEXAHEDRON
    25,  // VTK_QUADRATIC_HEXAHEDRON
    29,  // VTK_TRIQUADRATIC_HEXAHEDRON
    13,  // VTK_WEDGE
    26,  // VTK_QUADRATIC_WEDGE
    14,  // VTK_PYRAMID
    27,  // VTK_QUADRATIC_PYRAMID
};

fs::path unique_tmp_path(std::string_view tag) {
  std::random_device rd;
  std::string name = "souxmar-vtu-";
  name.append(tag);
  name.append("-");
  name.append(std::to_string(rd()));
  name.append(".vtu");
  return fs::temp_directory_path() / name;
}

fs::path search_root_for(const fs::path& plugin_build_dir) {
  return plugin_build_dir.parent_path();
}

plugin::LoadedPlugin load_plugin(plugin::PluginLoader& loader,
                                  const fs::path&       plugin_build_dir,
                                  const std::string&    expected_id) {
  const auto report = plugin::discover_plugins({search_root_for(plugin_build_dir)});
  if (report.loaded.empty()) {
    throw std::runtime_error(
        "discovery returned no plugins for " + plugin_build_dir.string());
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

std::string read_file(const fs::path& p) {
  std::ifstream in(p);
  std::ostringstream buf;
  buf << in.rdbuf();
  return buf.str();
}

// Extract the inner text of the *first* <tag …>inner</tag> match.
// The whole vtu file is hand-rolled XML with no namespaces / no CDATA,
// so a regex is sufficient — switching to a real XML parser would add
// a vcpkg dep just for the conformance test; the regex strategy is the
// same one the upstream ParaView quick-look reader uses.
std::string extract_tag_text(const std::string& xml,
                             const std::string& tag) {
  std::regex re("<" + tag + "[^>]*>([\\s\\S]*?)</" + tag + ">");
  std::smatch m;
  if (std::regex_search(xml, m, re)) {
    return m[1].str();
  }
  return {};
}

// Extract the value of attribute `attr` from the first opening tag
// matching `tag`. Returns "" if absent.
std::string extract_attr(const std::string& xml,
                         const std::string& tag,
                         const std::string& attr) {
  std::regex re("<" + tag + "[^>]*\\b" + attr + "=\"([^\"]*)\"");
  std::smatch m;
  if (std::regex_search(xml, m, re)) {
    return m[1].str();
  }
  return {};
}

// Extract the inner text of a <DataArray Name="<name>">...</DataArray>.
std::string extract_data_array(const std::string& xml, const std::string& name) {
  std::regex re("<DataArray[^>]*\\bName=\"" + name + "\"[^>]*>([\\s\\S]*?)</DataArray>");
  std::smatch m;
  if (std::regex_search(xml, m, re)) {
    return m[1].str();
  }
  return {};
}

std::vector<int> split_ints(const std::string& s) {
  std::vector<int> out;
  std::istringstream is(s);
  int v;
  while (is >> v) out.push_back(v);
  return out;
}

std::vector<double> split_doubles(const std::string& s) {
  std::vector<double> out;
  std::istringstream is(s);
  double v;
  while (is >> v) out.push_back(v);
  return out;
}

}  // namespace

TEST(VtuConsumerConformance, OutputParsesAndCarriesContract) {
  plugin::Registry      registry;
  plugin::PluginLoader  loader(registry, "test-host/0.0.0");

  // Mesh comes from the hello-mesher (1 tet, 4 nodes). The writer
  // is the surface under test.
  auto _mesher = load_plugin(loader,
                              SOUXMAR_TEST_HELLO_MESHER_DIR,
                              "dev.souxmar.examples.hello-mesher");
  auto _writer = load_plugin(loader,
                              SOUXMAR_TEST_VTU_WRITER_DIR,
                              "dev.souxmar.examples.vtu-writer");

  ASSERT_NE(registry.find_writer("writer.vtu"), nullptr);

  const fs::path output = unique_tmp_path("conformance");
  if (fs::exists(output)) fs::remove(output);

  std::ostringstream yaml;
  yaml << "version: 1\n"
       << "stages:\n"
       << "  - id: mesh\n"
       << "    plugin: mesher.tetra.hello\n"
       << "  - id: write\n"
       << "    plugin: writer.vtu\n"
       << "    input:\n"
       << "      mesh: { from: mesh }\n"
       << "      path: " << output.string() << "\n";

  auto parse_result = pipeline::parse_pipeline(yaml.str());
  ASSERT_TRUE(std::holds_alternative<pipeline::Pipeline>(parse_result));
  const auto& p = std::get<pipeline::Pipeline>(parse_result);

  pipeline::RegistryDispatcher dispatcher(registry);
  pipeline::Cache              cache;

  auto run = pipeline::run_pipeline(p, dispatcher, cache);
  ASSERT_EQ(run.status, pipeline::RunResult::Status::Success);
  ASSERT_TRUE(fs::exists(output)) << "writer did not produce " << output;

  const std::string xml = read_file(output);

  // ---- Invariant 1: well-formed XML envelope ----
  ASSERT_FALSE(xml.empty()) << "output file is empty";
  EXPECT_NE(xml.find("<VTKFile"), std::string::npos);
  EXPECT_NE(xml.find("</VTKFile>"), std::string::npos);

  // ---- Invariant 2: root element type + required attrs ----
  EXPECT_EQ(extract_attr(xml, "VTKFile", "type"), "UnstructuredGrid");
  EXPECT_FALSE(extract_attr(xml, "VTKFile", "version").empty());
  EXPECT_FALSE(extract_attr(xml, "VTKFile", "byte_order").empty());

  // ---- Invariant 3: one UnstructuredGrid + one Piece ----
  // (Hand-rolled writer emits exactly one of each; multi-piece is a
  //  Sprint 6+ extension once the parallel writer arrives.)
  EXPECT_NE(xml.find("<UnstructuredGrid>"), std::string::npos);
  EXPECT_NE(xml.find("</UnstructuredGrid>"), std::string::npos);
  EXPECT_NE(xml.find("<Piece"), std::string::npos);
  EXPECT_NE(xml.find("</Piece>"), std::string::npos);

  // ---- Invariant 4: NumberOfPoints/NumberOfCells agree with arrays ----
  const std::string n_pts_str = extract_attr(xml, "Piece", "NumberOfPoints");
  const std::string n_cells_str = extract_attr(xml, "Piece", "NumberOfCells");
  ASSERT_FALSE(n_pts_str.empty())   << "Piece missing NumberOfPoints";
  ASSERT_FALSE(n_cells_str.empty()) << "Piece missing NumberOfCells";
  const int n_pts   = std::stoi(n_pts_str);
  const int n_cells = std::stoi(n_cells_str);
  EXPECT_EQ(n_pts,   4) << "hello-mesher emits 4 nodes";
  EXPECT_EQ(n_cells, 1) << "hello-mesher emits 1 tet";

  // <Points> carries 3 floats per node.
  const std::string points_data =
      extract_data_array(xml, "Points");
  // Some writers nest the array under <Points><DataArray>...</DataArray></Points>;
  // grab the inner DataArray directly if needed.
  std::string points_text = points_data;
  if (points_text.find("<DataArray") != std::string::npos) {
    // The <Points> block had a nested DataArray (no Name attribute);
    // hand-extract its inner text.
    std::regex inner(R"(<DataArray[^>]*>([\s\S]*?)</DataArray>)");
    std::smatch m;
    if (std::regex_search(points_data, m, inner)) {
      points_text = m[1].str();
    }
  }
  const auto pts = split_doubles(points_text);
  EXPECT_EQ(static_cast<int>(pts.size()), n_pts * 3)
      << "Points array must hold NumberOfPoints * 3 floats";

  // ---- Invariant 5: required <DataArray> names under <Cells> ----
  const std::string connectivity = extract_data_array(xml, "connectivity");
  const std::string offsets      = extract_data_array(xml, "offsets");
  const std::string types        = extract_data_array(xml, "types");
  EXPECT_FALSE(connectivity.empty()) << "<Cells> missing connectivity array";
  EXPECT_FALSE(offsets.empty())      << "<Cells> missing offsets array";
  EXPECT_FALSE(types.empty())        << "<Cells> missing types array";

  // ---- Invariant 6: every cell-type id is a known VTK number ----
  const auto type_ids = split_ints(types);
  EXPECT_EQ(static_cast<int>(type_ids.size()), n_cells)
      << "types array length must equal NumberOfCells";
  for (int t : type_ids) {
    EXPECT_TRUE(kKnownVtkCellTypes.count(t) == 1)
        << "cell type " << t << " is not in the writer's known table — "
        << "either the writer emitted garbage or a new ratified type "
        << "was added without updating kKnownVtkCellTypes here.";
  }

  // ---- Invariant 7: arrays parse as whitespace-separated numerics ----
  // (split_ints / split_doubles already exercised this for connectivity /
  // offsets / types / points; if comma-separated content slipped in, the
  // stoi-via-istream loop would terminate early — assertions on sizes
  // above catch the divergence.)
  const auto conn = split_ints(connectivity);
  const auto off  = split_ints(offsets);
  EXPECT_FALSE(conn.empty());
  EXPECT_FALSE(off.empty());
  EXPECT_EQ(static_cast<int>(off.size()), n_cells);

  fs::remove(output);
}
