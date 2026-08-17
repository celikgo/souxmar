// SPDX-License-Identifier: Apache-2.0
//
// Manufacturing block — the two geometry producers.
//
//   mesher.am.layered  (am-layered-mesher)
//   reader.lattice     (lattice-reader)
//
// Written against the frozen capability contract (AM_CONTRACT §1 rows 1-2,
// §2.3, §3.1, §3.2), not against the plugin sources.
//
// mesher.am.layered, with no upstream geometry and target_size 0.005 m, must
// voxelise the documented demo build box [0,0,0]..[0.06,0.02,0.02] into a
// 12 x 4 x 4 Hex8 grid: 192 cells, 13 x 5 x 5 = 325 shared nodes, cell tag
// = layer index (0 at the bottom), and the outer boundary faces tagged
// 10/11 (∓X), 12/13 (∓Y), 14/15 (∓Z) with every interior face left
// untagged. The face-tag check walks the real face table
// (souxmar/core/face_topology.h) rather than assuming a local-face order.
//
// reader.lattice reads a spec file, is overridden by the YAML value bag,
// emits a mesh (never a geometry) of Edge2 struts deduplicated by sorted
// node-index pair, and tags each strut with its family.

#include "souxmar/core/element_type.h"
#include "souxmar/core/face_topology.h"
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
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace fs = std::filesystem;
using namespace souxmar;

namespace {

fs::path plugins_root() {
  return fs::path(SOUXMAR_TEST_AM_LAYERED_MESHER_DIR).parent_path();
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

constexpr double kTargetSize = 0.005;

// The §3.2 fixture: a 2 x 2 x 2 cubic lattice in a 20 mm box.
constexpr const char* kCubicLatticeSpec =
    "# souxmar lattice spec — integration fixture\n"
    "unit_cell = cubic\n"
    "bbox = [0.0, 0.0, 0.0, 0.02, 0.02, 0.02]\n"
    "cell_size = 0.01\n"
    "relative_density = 0.2\n";

// Line 7 has no `=`, so it is a parse error the message must locate. (7 was
// chosen so the assertion cannot be satisfied by the digits of the
// SOUXMAR_E_IO status code, 10, that the dispatcher also prints.)
constexpr const char* kMalformedLatticeSpec =
    "# souxmar lattice spec — deliberately malformed\n"
    "unit_cell = cubic\n"
    "bbox = [0.0, 0.0, 0.0, 0.02, 0.02, 0.02]\n"
    "cell_size = 0.01\n"
    "relative_density = 0.2\n"
    "\n"
    "this line has no equals sign\n";

class AmMeshersTest : public ::testing::Test {
 protected:
  void SetUp() override {
    discovery_ = plugin::discover_plugins({plugins_root()});
    ASSERT_FALSE(discovery_.loaded.empty()) << "no plugins discovered under " << plugins_root();
    loader_ = std::make_unique<plugin::PluginLoader>(registry_, "test-host/0.0.0");
    held_.push_back(load_by_id(*loader_, discovery_, "dev.souxmar.examples.am-layered-mesher"));
    held_.push_back(load_by_id(*loader_, discovery_, "dev.souxmar.examples.lattice-reader"));

    tmp_ = fs::temp_directory_path()
           / ("souxmar-am-meshers-"
              + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
    std::error_code ec;
    fs::remove_all(tmp_, ec);
    fs::create_directories(tmp_);
  }

  void TearDown() override {
    std::error_code ec;
    fs::remove_all(tmp_, ec);
  }

  fs::path write_spec(const char* contents) {
    const auto p = tmp_ / "lattice.txt";
    std::ofstream out(p, std::ios::binary);
    out << contents;
    out.close();
    return p;
  }

  pipeline::RunResult run(const std::string& yaml, pipeline::Cache& cache) {
    auto parsed = pipeline::parse_pipeline(yaml);
    if (auto* err = std::get_if<pipeline::ParseError>(&parsed)) {
      ADD_FAILURE() << "pipeline parse failed: " << err->message;
      return pipeline::RunResult{pipeline::RunResult::Status::ValidationFailed, {}, {}, {}};
    }
    pipeline::RegistryDispatcher dispatcher(registry_);
    return pipeline::run_pipeline(std::get<pipeline::Pipeline>(parsed), dispatcher, cache);
  }

  static std::string first_error(const pipeline::RunResult& r) {
    for (const auto& sr : r.stage_results) {
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

TEST_F(AmMeshersTest, RegistersMesherAndReaderUnderExactIds) {
  ASSERT_NE(registry_.find_mesher("mesher.am.layered"), nullptr);
  ASSERT_NE(registry_.find_reader("reader.lattice"), nullptr);

  EXPECT_EQ(registry_.find_reader("mesher.am.layered"), nullptr);
  EXPECT_EQ(registry_.find_mesher("reader.lattice"), nullptr);

  const auto* mesher = registry_.find("mesher.am.layered");
  ASSERT_NE(mesher, nullptr);
  EXPECT_EQ(mesher->kind, plugin::CapabilityKind::Mesher);
  EXPECT_EQ(mesher->plugin_id, "dev.souxmar.examples.am-layered-mesher");

  const auto* reader = registry_.find("reader.lattice");
  ASSERT_NE(reader, nullptr);
  EXPECT_EQ(reader->kind, plugin::CapabilityKind::Reader);
  EXPECT_EQ(reader->plugin_id, "dev.souxmar.examples.lattice-reader");

  EXPECT_EQ(registry_.find("mesher.am.layer"), nullptr);
  EXPECT_EQ(registry_.find("reader.lattices"), nullptr);
}

TEST_F(AmMeshersTest, LayeredMesherVoxelisesTheDemoBoxWithLayerTaggedCells) {
  std::ostringstream y;
  y << "version: 1\n"
    << "stages:\n"
    << "  - id: mesh\n"
    << "    plugin: mesher.am.layered\n"
    << "    input:\n"
    << "      target_size: " << kTargetSize << "\n";
  pipeline::Cache cache;
  auto r = run(y.str(), cache);
  ASSERT_EQ(r.status, pipeline::RunResult::Status::Success) << first_error(r);

  const auto* out = static_cast<const pipeline::StageOutput*>(r.outputs.at("mesh").get());
  ASSERT_EQ(out->kind, pipeline::StageOutput::Kind::Mesh);
  ASSERT_NE(out->mesh, nullptr);
  const auto& mesh = *out->mesh;

  // 0.06 / 0.005 = 12, 0.02 / 0.005 = 4, 0.02 / 0.005 = 4.
  EXPECT_EQ(mesh.num_cells(), 192u);
  // Structured grid with shared nodes: 13 x 5 x 5. A per-cell node
  // duplication (8 * 192 = 1536) would break every downstream face-sharing
  // and FEM assembly rule, so this is pinned.
  EXPECT_EQ(mesh.num_nodes(), 325u);

  const auto bbox = mesh.bounding_box();
  // 1e-12 m is coordinate round-off on an exact multiple of target_size.
  EXPECT_NEAR(bbox[0], 0.00, 1e-12);
  EXPECT_NEAR(bbox[1], 0.00, 1e-12);
  EXPECT_NEAR(bbox[2], 0.00, 1e-12);
  EXPECT_NEAR(bbox[3], 0.06, 1e-12);
  EXPECT_NEAR(bbox[4], 0.02, 1e-12);
  EXPECT_NEAR(bbox[5], 0.02, 1e-12);

  const auto hist = mesh.element_histogram();
  ASSERT_EQ(hist.size(), 1u) << "the layered mesher emits Hex8 and nothing else";
  EXPECT_EQ(hist[0].first, core::ElementType::Hex8);
  EXPECT_EQ(hist[0].second, 192u);

  // §3.1 / §2.2: cell tag == layer index, 0 at the bottom, along +Z.
  std::map<std::int32_t, std::size_t> per_layer;
  for (std::size_t c = 0; c < mesh.num_cells(); ++c) {
    const auto ci = core::CellIndex{c};
    ASSERT_EQ(mesh.cell_type(ci), core::ElementType::Hex8);
    const auto nodes = mesh.cell_nodes(ci);
    ASSERT_EQ(nodes.size(), 8u);
    double zc = 0.0;
    for (const auto n : nodes) {
      zc += mesh.node(n)[2];
    }
    zc /= 8.0;
    const auto expected_layer = static_cast<std::int32_t>(std::floor((zc - bbox[2]) / kTargetSize));
    EXPECT_EQ(mesh.cell_tag(ci).value, expected_layer)
        << "cell " << c << " centroid z=" << zc << " belongs to layer " << expected_layer;
    per_layer[mesh.cell_tag(ci).value]++;
  }
  ASSERT_EQ(per_layer.size(), 4u) << "0.02 m / 0.005 m = 4 layers";
  for (const auto& [layer, count] : per_layer) {
    EXPECT_GE(layer, 0);
    EXPECT_LE(layer, 3);
    EXPECT_EQ(count, 48u) << "layer " << layer << " must hold 12 x 4 voxels";
  }
}

// §2.3: outer boundary faces carry 10 = -X, 11 = +X, 12 = -Y, 13 = +Y,
// 14 = -Z (baseplate / downskin), 15 = +Z (top / upskin); interior faces
// stay untagged. Verified geometrically through the shared face table so the
// test does not encode a local-face order of its own.
TEST_F(AmMeshersTest, LayeredMesherTagsOuterBoundaryFacesPerContract) {
  std::ostringstream y;
  y << "version: 1\n"
    << "stages:\n"
    << "  - id: mesh\n"
    << "    plugin: mesher.am.layered\n"
    << "    input:\n"
    << "      target_size: " << kTargetSize << "\n";
  pipeline::Cache cache;
  auto r = run(y.str(), cache);
  ASSERT_EQ(r.status, pipeline::RunResult::Status::Success) << first_error(r);
  const auto* out = static_cast<const pipeline::StageOutput*>(r.outputs.at("mesh").get());
  ASSERT_NE(out->mesh, nullptr);
  const auto& mesh = *out->mesh;
  const auto bbox = mesh.bounding_box();

  const auto tagged = mesh.tagged_faces();
  // 2 * (4*4) + 2 * (12*4) + 2 * (12*4) = 32 + 96 + 96 = 224 outer faces.
  // Anything more means an interior face was tagged too.
  EXPECT_EQ(tagged.size(), 224u);

  std::map<std::int32_t, std::size_t> per_tag;
  for (const auto& tf : tagged) {
    const auto table = core::face_node_table(mesh.cell_type(tf.cell));
    ASSERT_LT(static_cast<std::size_t>(tf.local_face), table.size());
    const auto& lf = table[tf.local_face];
    const auto nodes = mesh.cell_nodes(tf.cell);

    // Which bounding-box plane does this face lie in? (xmin, xmax, ymin,
    // ymax, zmin, zmax) — a voxel face is planar and axis-aligned, so
    // exactly one of these survives.
    bool on_plane[6] = {true, true, true, true, true, true};
    for (std::uint8_t i = 0; i < lf.vertex_count; ++i) {
      const auto p = mesh.node(nodes[lf.cell_local_idx[i]]);
      for (std::size_t axis = 0; axis < 3; ++axis) {
        if (std::abs(p[axis] - bbox[axis]) > 1e-12)
          on_plane[2 * axis] = false;
        if (std::abs(p[axis] - bbox[3 + axis]) > 1e-12)
          on_plane[2 * axis + 1] = false;
      }
    }
    std::int32_t expected = -1;
    for (std::size_t slot = 0; slot < 6; ++slot) {
      if (on_plane[slot]) {
        expected = static_cast<std::int32_t>(10 + slot);
        break;
      }
    }
    EXPECT_NE(expected, -1) << "cell " << tf.cell.value << " face "
                            << static_cast<int>(tf.local_face)
                            << " is tagged but is not on the outer boundary";
    EXPECT_EQ(tf.tag.value, expected)
        << "cell " << tf.cell.value << " face " << static_cast<int>(tf.local_face)
        << " carries tag " << tf.tag.value << ", expected " << expected;
    per_tag[tf.tag.value]++;
  }

  EXPECT_EQ(per_tag[10], 16u) << "-X: 4 x 4 faces";
  EXPECT_EQ(per_tag[11], 16u) << "+X: 4 x 4 faces";
  EXPECT_EQ(per_tag[12], 48u) << "-Y: 12 x 4 faces";
  EXPECT_EQ(per_tag[13], 48u) << "+Y: 12 x 4 faces";
  EXPECT_EQ(per_tag[14], 48u) << "-Z (baseplate interface): 12 x 4 faces";
  EXPECT_EQ(per_tag[15], 48u) << "+Z (top): 12 x 4 faces";
}

// §3.1: element_order 2 ⇒ SOUXMAR_E_NOT_IMPLEMENTED, surfaced as a stage
// failure rather than a silently-linear mesh.
TEST_F(AmMeshersTest, LayeredMesherRejectsSecondOrderElements) {
  std::ostringstream y;
  y << "version: 1\n"
    << "stages:\n"
    << "  - id: mesh\n"
    << "    plugin: mesher.am.layered\n"
    << "    input:\n"
    << "      target_size: " << kTargetSize << "\n"
    << "      element_order: 2\n";
  pipeline::Cache cache;
  auto r = run(y.str(), cache);
  EXPECT_EQ(r.status, pipeline::RunResult::Status::StageFailed) << first_error(r);
}

// §3.1: hard cap of 200 000 cells. 1e-5 m voxels over the demo box would be
// 6000 x 2000 x 2000, so the mesher must refuse instead of trying.
TEST_F(AmMeshersTest, LayeredMesherRejectsAnExcessiveCellCount) {
  std::ostringstream y;
  y << "version: 1\n"
    << "stages:\n"
    << "  - id: mesh\n"
    << "    plugin: mesher.am.layered\n"
    << "    input:\n"
    << "      target_size: 1.0e-5\n";
  pipeline::Cache cache;
  auto r = run(y.str(), cache);
  EXPECT_EQ(r.status, pipeline::RunResult::Status::StageFailed) << first_error(r);
}

TEST_F(AmMeshersTest, LatticeReaderProducesDeduplicatedEdge2Struts) {
  const auto spec = write_spec(kCubicLatticeSpec);
  std::ostringstream y;
  y << "version: 1\n"
    << "stages:\n"
    << "  - id: lattice\n"
    << "    plugin: reader.lattice\n"
    << "    input:\n"
    << "      path: " << spec.string() << "\n";
  pipeline::Cache cache;
  auto r = run(y.str(), cache);
  ASSERT_EQ(r.status, pipeline::RunResult::Status::Success) << first_error(r);

  const auto* out = static_cast<const pipeline::StageOutput*>(r.outputs.at("lattice").get());
  // §3.2: the reader fills out_mesh and leaves out_geometry NULL, and the
  // dispatcher rejects anything else, so a Mesh-kind output proves it.
  ASSERT_EQ(out->kind, pipeline::StageOutput::Kind::Mesh);
  ASSERT_NE(out->mesh, nullptr);
  const auto& mesh = *out->mesh;

  // A 2 x 2 x 2 cubic grid: 3 x 3 x 3 = 27 corner nodes, and
  // 3 * (2 * 3 * 3) = 54 axial struts once duplicated cell edges collapse.
  EXPECT_GE(mesh.num_nodes(), 27u);
  EXPECT_EQ(mesh.num_cells(), 54u);

  const auto hist = mesh.element_histogram();
  ASSERT_EQ(hist.size(), 1u) << "a strut lattice is Edge2 and nothing else";
  EXPECT_EQ(hist[0].first, core::ElementType::Edge2);
  EXPECT_EQ(hist[0].second, mesh.num_cells());

  // Deduplication is by the sorted (lo, hi) node-index key, so no two cells
  // may share one.
  std::set<std::pair<std::uint64_t, std::uint64_t>> keys;
  std::map<std::int32_t, std::size_t> per_family;
  for (std::size_t c = 0; c < mesh.num_cells(); ++c) {
    const auto ci = core::CellIndex{c};
    const auto nodes = mesh.cell_nodes(ci);
    ASSERT_EQ(nodes.size(), 2u);
    EXPECT_NE(nodes[0].value, nodes[1].value) << "strut " << c << " is degenerate";
    const auto lo = std::min(nodes[0].value, nodes[1].value);
    const auto hi = std::max(nodes[0].value, nodes[1].value);
    EXPECT_TRUE(keys.emplace(lo, hi).second) << "duplicate strut (" << lo << "," << hi << ")";
    per_family[mesh.cell_tag(ci).value]++;
  }
  EXPECT_EQ(keys.size(), mesh.num_cells());

  // §3.2 strut families: 0 X-axial, 1 Y-axial, 2 Z-axial, 3 body diagonal,
  // 4 face diagonal. A cubic cell has only the three axial families, 18
  // struts each.
  EXPECT_EQ(per_family[0], 18u);
  EXPECT_EQ(per_family[1], 18u);
  EXPECT_EQ(per_family[2], 18u);
  EXPECT_EQ(per_family.count(3), 0u) << "a cubic unit cell has no body diagonals";
  EXPECT_EQ(per_family.count(4), 0u) << "a cubic unit cell has no face diagonals";

  // Struts stay inside the requested bounding box.
  const auto bbox = mesh.bounding_box();
  EXPECT_NEAR(bbox[0], 0.0, 1e-12);
  EXPECT_NEAR(bbox[3], 0.02, 1e-12);
  EXPECT_NEAR(bbox[4], 0.02, 1e-12);
  EXPECT_NEAR(bbox[5], 0.02, 1e-12);
}

// §3.2: "a YAML value-bag entry overrides the file". The file asks for a
// cubic cell (no body diagonals); the pipeline asks for bcc (which has
// them), and bcc must win.
TEST_F(AmMeshersTest, LatticeReaderYamlValueBagOverridesTheSpecFile) {
  const auto spec = write_spec(kCubicLatticeSpec);
  std::ostringstream y;
  y << "version: 1\n"
    << "stages:\n"
    << "  - id: lattice\n"
    << "    plugin: reader.lattice\n"
    << "    input:\n"
    << "      path: " << spec.string() << "\n"
    << "      unit_cell: bcc\n";
  pipeline::Cache cache;
  auto r = run(y.str(), cache);
  ASSERT_EQ(r.status, pipeline::RunResult::Status::Success) << first_error(r);
  const auto* out = static_cast<const pipeline::StageOutput*>(r.outputs.at("lattice").get());
  ASSERT_NE(out->mesh, nullptr);
  const auto& mesh = *out->mesh;

  bool saw_body_diagonal = false;
  for (std::size_t c = 0; c < mesh.num_cells(); ++c) {
    if (mesh.cell_tag(core::CellIndex{c}).value == 3)
      saw_body_diagonal = true;
  }
  EXPECT_TRUE(saw_body_diagonal)
      << "the YAML `unit_cell: bcc` must override the file's `cubic`, producing "
         "strut-family-3 body diagonals";
}

// §3.2: parse errors are SOUXMAR_E_IO with a message naming the offending
// line.
TEST_F(AmMeshersTest, LatticeReaderRejectsAMalformedSpecAndNamesTheLine) {
  const auto spec = write_spec(kMalformedLatticeSpec);
  std::ostringstream y;
  y << "version: 1\n"
    << "stages:\n"
    << "  - id: lattice\n"
    << "    plugin: reader.lattice\n"
    << "    input:\n"
    << "      path: " << spec.string() << "\n";
  pipeline::Cache cache;
  auto r = run(y.str(), cache);
  ASSERT_EQ(r.status, pipeline::RunResult::Status::StageFailed);
  const std::string msg = first_error(r);
  EXPECT_NE(msg.find('7'), std::string::npos)
      << "the error must name line 7 as the offending line; got: " << msg;
}

// §3.2: an unrecognised unit cell is SOUXMAR_E_INVALID_ARGUMENT, not a
// silent fallback to the default.
TEST_F(AmMeshersTest, LatticeReaderRejectsAnUnknownUnitCell) {
  const auto spec = write_spec(kCubicLatticeSpec);
  std::ostringstream y;
  y << "version: 1\n"
    << "stages:\n"
    << "  - id: lattice\n"
    << "    plugin: reader.lattice\n"
    << "    input:\n"
    << "      path: " << spec.string() << "\n"
    << "      unit_cell: gyroid\n";
  pipeline::Cache cache;
  auto r = run(y.str(), cache);
  EXPECT_EQ(r.status, pipeline::RunResult::Status::StageFailed) << first_error(r);
}

// §3.2: `path` is required. The reader dispatcher rejects a missing one
// before the plugin is ever called (same bar as reader.stl).
TEST_F(AmMeshersTest, LatticeReaderRequiresAPath) {
  std::ostringstream y;
  y << "version: 1\n"
    << "stages:\n"
    << "  - id: lattice\n"
    << "    plugin: reader.lattice\n";
  pipeline::Cache cache;
  auto r = run(y.str(), cache);
  ASSERT_EQ(r.status, pipeline::RunResult::Status::StageFailed);
  EXPECT_NE(first_error(r).find("path"), std::string::npos);
}
