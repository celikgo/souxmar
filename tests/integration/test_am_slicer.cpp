// SPDX-License-Identifier: Apache-2.0
//
// Manufacturing block — am-slicer integration.
//
//   mesher.am.layered → writer.am.gcode / writer.am.cli / writer.am.report
//
// Written against the frozen capability contract (AM_CONTRACT §1 rows 12-14,
// §3.12, §3.13, §3.14), not against the plugin source. The writers produce
// files, so the assertions are: the file lands on disk, it carries the
// structural markers the contract names verbatim, and the numbers inside it
// agree with the mesh that was sliced.
//
// The demo build box is 0.06 x 0.02 x 0.02 m (§3.1), so slicing it at a
// 0.005 m layer height gives exactly 4 layers, and each layer's cross
// section is a single rectangle — one closed contour per layer.
//
// Every file is written into a per-test temporary directory and removed in
// TearDown; nothing is ever written into the source tree.

#include "souxmar/core/mesh.h"
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
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
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
  return fs::path(SOUXMAR_TEST_AM_SLICER_DIR).parent_path();
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

std::size_t count_occurrences(const std::string& haystack, const std::string& needle) {
  if (needle.empty())
    return 0;
  std::size_t n = 0;
  for (std::size_t pos = haystack.find(needle); pos != std::string::npos;
       pos = haystack.find(needle, pos + needle.size())) {
    ++n;
  }
  return n;
}

std::string lowered(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return s;
}

bool contains_ci(const std::string& haystack, const std::string& needle) {
  return lowered(haystack).find(lowered(needle)) != std::string::npos;
}

// Absolute E value of every extruding move: a `G1` that also moves in X.
// Retract / unretract moves are excluded deliberately — with M82 (absolute
// extrusion) a retraction legitimately writes a *lower* E before restoring
// it, so the monotonic quantity is the extruded filament on printing moves.
std::vector<double> extruding_move_e_values(const std::string& gcode) {
  std::vector<double> out;
  std::istringstream in(gcode);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.starts_with("G1"))
      continue;
    const auto semi = line.find(';');
    const std::string body = semi == std::string::npos ? line : line.substr(0, semi);
    if (body.find('X') == std::string::npos)
      continue;
    const auto epos = body.find('E');
    if (epos == std::string::npos)
      continue;
    const char* start = body.c_str() + epos + 1;
    char* end = nullptr;
    const double v = std::strtod(start, &end);
    if (end != start)
      out.push_back(v);
  }
  return out;
}

constexpr double kTargetSize = 0.005;        // voxel edge == slice layer height
constexpr std::size_t kExpectedLayers = 4u;  // 0.02 m tall box / 0.005 m

class AmSlicerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    discovery_ = plugin::discover_plugins({plugins_root()});
    ASSERT_FALSE(discovery_.loaded.empty()) << "no plugins discovered under " << plugins_root();
    loader_ = std::make_unique<plugin::PluginLoader>(registry_, "test-host/0.0.0");
    held_.push_back(load_by_id(*loader_, discovery_, "dev.souxmar.examples.am-layered-mesher"));
    held_.push_back(load_by_id(*loader_, discovery_, "dev.souxmar.examples.am-slicer"));

    tmp_ = fs::temp_directory_path()
           / ("souxmar-am-slicer-"
              + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
    std::error_code ec;
    fs::remove_all(tmp_, ec);
    fs::create_directories(tmp_);
  }

  void TearDown() override {
    std::error_code ec;
    fs::remove_all(tmp_, ec);
  }

  // Runs `mesher.am.layered` → one writer stage. Returns the run result so
  // callers can assert on failures too.
  pipeline::RunResult run_writer(const std::string& capability_id,
                                 const std::string& extra_inputs,
                                 pipeline::Cache& cache) {
    std::ostringstream y;
    y << "version: 1\n"
      << "stages:\n"
      << "  - id: mesh\n"
      << "    plugin: mesher.am.layered\n"
      << "    input:\n"
      << "      target_size: " << kTargetSize << "\n"
      << "  - id: write\n"
      << "    plugin: " << capability_id << "\n"
      << "    input:\n"
      << "      mesh: { from: mesh }\n"
      << extra_inputs;
    auto parsed = pipeline::parse_pipeline(y.str());
    if (auto* err = std::get_if<pipeline::ParseError>(&parsed)) {
      ADD_FAILURE() << "pipeline parse failed: " << err->message;
      return pipeline::RunResult{pipeline::RunResult::Status::ValidationFailed, {}, {}, {}};
    }
    pipeline::RegistryDispatcher dispatcher(registry_);
    return pipeline::run_pipeline(std::get<pipeline::Pipeline>(parsed), dispatcher, cache);
  }

  static std::string first_error(const pipeline::RunResult& run) {
    for (const auto& sr : run.stage_results) {
      if (sr.error)
        return sr.stage_id + ": " + sr.error->message;
    }
    return "(no error)";
  }

  plugin::DiscoveryReport discovery_;
  plugin::Registry registry_;
  std::unique_ptr<plugin::PluginLoader> loader_;
  std::vector<plugin::LoadedPlugin> held_;
  fs::path tmp_;
};

}  // namespace

TEST_F(AmSlicerTest, RegistersThreeWritersUnderExactIds) {
  for (const char* id : {"writer.am.gcode", "writer.am.cli", "writer.am.report"}) {
    ASSERT_NE(registry_.find_writer(id), nullptr) << id;
    EXPECT_EQ(registry_.find_solver(id), nullptr) << id;
    EXPECT_EQ(registry_.find_postproc(id), nullptr) << id;
    const auto* e = registry_.find(id);
    ASSERT_NE(e, nullptr) << id;
    EXPECT_EQ(e->kind, plugin::CapabilityKind::Writer) << id;
    EXPECT_EQ(e->plugin_id, "dev.souxmar.examples.am-slicer") << id;
  }
  EXPECT_EQ(registry_.find("writer.am.g-code"), nullptr);
  EXPECT_EQ(registry_.find("writer.gcode"), nullptr);
}

// §3.12 structural markers, plus the one physical invariant a G-code file
// must satisfy: cumulative extrusion never runs backwards on printing moves.
TEST_F(AmSlicerTest, GcodeWriterEmitsHeaderPerLayerMovesAndMonotonicExtrusion) {
  const auto out_path = tmp_ / "box.gcode";
  std::ostringstream in;
  in << "      path: " << out_path.string() << "\n"
     << "      layer_height: " << kTargetSize << "\n"
     << "      road_width: 4.0e-4\n"
     << "      filament_diameter: 1.75e-3\n"
     << "      nozzle_temperature: 250.0\n"
     << "      bed_temperature: 100.0\n"
     << "      print_speed: 0.05\n"
     << "      travel_speed: 0.15\n"
     << "      infill_spacing: 0.002\n"
     << "      infill_angle_deg: 45.0\n"
     << "      retract_length: 0.001\n"
     << "      flow_multiplier: 1.0\n"
     << "      build_direction: [0.0, 0.0, 1.0]\n";

  pipeline::Cache cache;
  auto run = run_writer("writer.am.gcode", in.str(), cache);
  ASSERT_EQ(run.status, pipeline::RunResult::Status::Success) << first_error(run);
  ASSERT_TRUE(fs::exists(out_path)) << out_path;

  // The writer's StageOutput is the Path it wrote (§ dispatcher contract).
  ASSERT_NE(run.outputs.find("write"), run.outputs.end());
  const auto* wout = static_cast<const pipeline::StageOutput*>(run.outputs.at("write").get());
  EXPECT_EQ(wout->kind, pipeline::StageOutput::Kind::Path);
  EXPECT_EQ(wout->path, out_path.string());

  const std::string gcode = read_file(out_path);
  ASSERT_FALSE(gcode.empty());

  // Header block is `;` comments, then the machine preamble.
  EXPECT_EQ(gcode[0], ';') << "the file must open with the `;` parameter header";
  EXPECT_NE(gcode.find("M104"), std::string::npos) << "missing hot-end temperature command";
  EXPECT_NE(gcode.find("M140"), std::string::npos) << "missing bed temperature command";
  EXPECT_NE(gcode.find("G28"), std::string::npos) << "missing home";
  EXPECT_NE(gcode.find("G90"), std::string::npos) << "missing absolute positioning";
  EXPECT_NE(gcode.find("M82"), std::string::npos) << "missing absolute extrusion mode";

  // One `G1 Z…` per layer.
  EXPECT_EQ(count_occurrences(gcode, "G1 Z"), kExpectedLayers)
      << "expected exactly one layer change per layer";

  // Trailer turns the heaters off.
  EXPECT_NE(gcode.find("M104 S0"), std::string::npos) << "trailer must switch the hot end off";
  EXPECT_NE(gcode.find("M140 S0"), std::string::npos) << "trailer must switch the bed off";

  const auto e_values = extruding_move_e_values(gcode);
  ASSERT_GE(e_values.size(), kExpectedLayers)
      << "expected at least one extruding move per layer, got " << e_values.size();
  EXPECT_TRUE(std::is_sorted(e_values.begin(), e_values.end()))
      << "absolute E on extruding moves must increase monotonically";
  EXPECT_GT(e_values.back(), 0.0) << "nothing was extruded";
  EXPECT_DOUBLE_EQ(e_values.back(), *std::max_element(e_values.begin(), e_values.end()));
}

// §3.13 structural markers. A rectangular box slices to exactly one closed
// contour per layer, which is the sanity check on the contour chaining.
TEST_F(AmSlicerTest, CliWriterEmitsAsciiHeaderAndOneContourPerLayer) {
  const auto out_path = tmp_ / "box.cli";
  std::ostringstream in;
  in << "      path: " << out_path.string() << "\n"
     << "      layer_height: " << kTargetSize << "\n"
     << "      units: 1.0\n"
     << "      label: souxmar-test\n"
     << "      binary: false\n"
     << "      build_direction: [0.0, 0.0, 1.0]\n";

  pipeline::Cache cache;
  auto run = run_writer("writer.am.cli", in.str(), cache);
  ASSERT_EQ(run.status, pipeline::RunResult::Status::Success) << first_error(run);
  ASSERT_TRUE(fs::exists(out_path)) << out_path;

  const std::string cli = read_file(out_path);
  ASSERT_FALSE(cli.empty());

  EXPECT_NE(cli.find("$$HEADERSTART"), std::string::npos);
  EXPECT_NE(cli.find("$$ASCII"), std::string::npos);
  EXPECT_NE(cli.find("$$UNITS/"), std::string::npos);
  EXPECT_NE(cli.find("$$VERSION/200"), std::string::npos);
  EXPECT_NE(cli.find("$$LABEL/"), std::string::npos);
  // Fixed date — the contract forbids reading the wall clock.
  EXPECT_NE(cli.find("$$DATE/010100"), std::string::npos);
  EXPECT_NE(cli.find("$$DIMENSION/"), std::string::npos);
  EXPECT_NE(cli.find("$$HEADEREND"), std::string::npos);
  EXPECT_NE(cli.find("$$GEOMETRYSTART"), std::string::npos);
  EXPECT_NE(cli.find("$$GEOMETRYEND"), std::string::npos);
  // The CLI record is `$$LABEL/<part id>,<text>` — the part id comes first,
  // so the label is the second field, not the whole payload.
  EXPECT_NE(cli.find("$$LABEL/1,souxmar-test"), std::string::npos)
      << "the `label` input must reach the $$LABEL record";

  EXPECT_NE(cli.find("$$LAYERS/" + std::to_string(kExpectedLayers)), std::string::npos)
      << "$$LAYERS must report the sliced layer count";
  EXPECT_EQ(count_occurrences(cli, "$$LAYER/"), kExpectedLayers);
  EXPECT_EQ(count_occurrences(cli, "$$POLYLINE/"), kExpectedLayers)
      << "a rectangular cross section is one closed contour per layer";

  // Header precedes geometry.
  EXPECT_LT(cli.find("$$HEADERSTART"), cli.find("$$HEADEREND"));
  EXPECT_LT(cli.find("$$HEADEREND"), cli.find("$$GEOMETRYSTART"));
  EXPECT_LT(cli.find("$$GEOMETRYSTART"), cli.find("$$GEOMETRYEND"));
}

// §3.13: `binary: true` ⇒ SOUXMAR_E_NOT_IMPLEMENTED, surfaced as a stage
// failure — not a silently-ASCII file.
TEST_F(AmSlicerTest, CliWriterRejectsBinaryOutput) {
  const auto out_path = tmp_ / "binary.cli";
  std::ostringstream in;
  in << "      path: " << out_path.string() << "\n"
     << "      layer_height: " << kTargetSize << "\n"
     << "      binary: true\n";

  pipeline::Cache cache;
  auto run = run_writer("writer.am.cli", in.str(), cache);
  EXPECT_EQ(run.status, pipeline::RunResult::Status::StageFailed);
  bool saw_failure = false;
  for (const auto& sr : run.stage_results) {
    if (sr.stage_id == "write" && sr.status == pipeline::StageRunResult::Status::Failed)
      saw_failure = true;
  }
  EXPECT_TRUE(saw_failure) << "writer.am.cli must refuse `binary: true`";
}

// §3.14: the report always renders a mesh summary, the process parameters
// and a Traceability section; with no upstream field it says so rather than
// inventing one. No wall clock, no absolute paths.
TEST_F(AmSlicerTest, ReportWriterRendersMeshSummaryAndTraceabilityWithoutAField) {
  const auto out_path = tmp_ / "build-report.md";
  std::ostringstream in;
  in << "      path: " << out_path.string() << "\n"
     << "      title: souxmar AM integration report\n"
     << "      process: lpbf\n"
     << "      material: 316L\n"
     << "      design_notes: written by the integration suite\n";

  pipeline::Cache cache;
  auto run = run_writer("writer.am.report", in.str(), cache);
  ASSERT_EQ(run.status, pipeline::RunResult::Status::Success) << first_error(run);
  ASSERT_TRUE(fs::exists(out_path)) << out_path;

  const auto* mesh_out = static_cast<const pipeline::StageOutput*>(run.outputs.at("mesh").get());
  ASSERT_NE(mesh_out->mesh, nullptr);
  const auto& mesh = *mesh_out->mesh;

  const std::string md = read_file(out_path);
  ASSERT_FALSE(md.empty());

  EXPECT_EQ(md[0], '#') << "Markdown output must open with a heading";
  EXPECT_NE(md.find("souxmar AM integration report"), std::string::npos)
      << "the `title` input must reach the document";
  EXPECT_NE(md.find("## "), std::string::npos) << "expected Markdown section headings";
  EXPECT_NE(md.find("Traceability"), std::string::npos)
      << "§3.14 requires a Traceability section with a content digest";

  // Mesh summary: the node and cell counts of the mesh actually written.
  EXPECT_NE(md.find(std::to_string(mesh.num_nodes())), std::string::npos)
      << "report must state the node count (" << mesh.num_nodes() << ")";
  EXPECT_NE(md.find(std::to_string(mesh.num_cells())), std::string::npos)
      << "report must state the cell count (" << mesh.num_cells() << ")";
  EXPECT_TRUE(contains_ci(md, "hex")) << "element histogram must name the Hex8 elements";

  // Process parameters echoed from the inputs.
  EXPECT_TRUE(contains_ci(md, "lpbf"));
  EXPECT_NE(md.find("316L"), std::string::npos);

  // No field was wired in — say so.
  EXPECT_TRUE(contains_ci(md, "no field"))
      << "a NULL field must produce a 'no field supplied' note";

  // §3.14: no absolute paths in the output.
  EXPECT_EQ(md.find(tmp_.string()), std::string::npos)
      << "the report must not echo absolute filesystem paths";
}

// §3.14 field dispatch: a `buildtime` field must produce its own section.
TEST_F(AmSlicerTest, ReportWriterRendersASectionForTheSuppliedField) {
  held_.push_back(load_by_id(*loader_, discovery_, "dev.souxmar.examples.am-manufacturability"));
  ASSERT_NE(registry_.find_solver("solver.am.buildtime"), nullptr);

  const auto out_path = tmp_ / "traveller.md";
  std::ostringstream y;
  y << "version: 1\n"
    << "stages:\n"
    << "  - id: mesh\n"
    << "    plugin: mesher.am.layered\n"
    << "    input:\n"
    << "      target_size: " << kTargetSize << "\n"
    << "  - id: buildtime\n"
    << "    plugin: solver.am.buildtime\n"
    << "    input:\n"
    << "      mesh: { from: mesh }\n"
    << "      process: lpbf\n"
    << "      layer_height: " << kTargetSize << "\n"
    << "  - id: write\n"
    << "    plugin: writer.am.report\n"
    << "    input:\n"
    << "      mesh: { from: mesh }\n"
    << "      field: { from: buildtime }\n"
    << "      path: " << out_path.string() << "\n"
    << "      title: souxmar traveller sheet\n"
    << "      process: lpbf\n"
    << "      material: 316L\n"
    << "      material_cost_per_kg: 90.0\n"
    << "      machine_rate_per_hour: 45.0\n";

  auto parsed = pipeline::parse_pipeline(y.str());
  ASSERT_TRUE(std::holds_alternative<pipeline::Pipeline>(parsed))
      << std::get<pipeline::ParseError>(parsed).message;
  pipeline::RegistryDispatcher dispatcher(registry_);
  pipeline::Cache cache;
  auto run = pipeline::run_pipeline(std::get<pipeline::Pipeline>(parsed), dispatcher, cache);
  ASSERT_EQ(run.status, pipeline::RunResult::Status::Success) << first_error(run);
  ASSERT_TRUE(fs::exists(out_path));

  const std::string md = read_file(out_path);
  EXPECT_NE(md.find("souxmar traveller sheet"), std::string::npos);
  EXPECT_TRUE(contains_ci(md, "buildtime"))
      << "the report dispatches on the field name and must render its section";
  EXPECT_NE(md.find("Traceability"), std::string::npos);
  EXPECT_FALSE(contains_ci(md, "no field"))
      << "a field was supplied; the 'no field' note must not appear";
}

// Determinism gate for the writer path: the same pipeline, twice, in one
// process, to the same output path — byte-identical files. Two separate
// Cache instances so the second run really re-executes instead of being
// served from the content-addressed cache.
TEST_F(AmSlicerTest, GcodeAndCliOutputsAreByteIdenticalAcrossRuns) {
  const auto gcode_path = tmp_ / "determinism.gcode";
  std::ostringstream gin;
  gin << "      path: " << gcode_path.string() << "\n"
      << "      layer_height: " << kTargetSize << "\n"
      << "      infill_spacing: 0.002\n"
      << "      infill_angle_deg: 45.0\n";

  std::string first;
  std::string second;
  {
    pipeline::Cache cache;
    auto run = run_writer("writer.am.gcode", gin.str(), cache);
    ASSERT_EQ(run.status, pipeline::RunResult::Status::Success) << first_error(run);
    first = read_file(gcode_path);
    std::error_code ec;
    fs::remove(gcode_path, ec);
  }
  {
    pipeline::Cache cache;
    auto run = run_writer("writer.am.gcode", gin.str(), cache);
    ASSERT_EQ(run.status, pipeline::RunResult::Status::Success) << first_error(run);
    second = read_file(gcode_path);
  }
  ASSERT_FALSE(first.empty());
  ASSERT_EQ(first.size(), second.size()) << "G-code length changed between identical runs";
  EXPECT_EQ(std::memcmp(first.data(), second.data(), first.size()), 0)
      << "G-code output is not byte-identical across two runs in one process";

  const auto cli_path = tmp_ / "determinism.cli";
  std::ostringstream cli_in;
  cli_in << "      path: " << cli_path.string() << "\n"
         << "      layer_height: " << kTargetSize << "\n";

  std::string cfirst;
  std::string csecond;
  {
    pipeline::Cache cache;
    auto run = run_writer("writer.am.cli", cli_in.str(), cache);
    ASSERT_EQ(run.status, pipeline::RunResult::Status::Success) << first_error(run);
    cfirst = read_file(cli_path);
    std::error_code ec;
    fs::remove(cli_path, ec);
  }
  {
    pipeline::Cache cache;
    auto run = run_writer("writer.am.cli", cli_in.str(), cache);
    ASSERT_EQ(run.status, pipeline::RunResult::Status::Success) << first_error(run);
    csecond = read_file(cli_path);
  }
  ASSERT_FALSE(cfirst.empty());
  ASSERT_EQ(cfirst.size(), csecond.size()) << "CLI length changed between identical runs";
  EXPECT_EQ(std::memcmp(cfirst.data(), csecond.data(), cfirst.size()), 0)
      << "CLI output is not byte-identical across two runs in one process";
}
