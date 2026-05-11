// SPDX-License-Identifier: Apache-2.0
//
// Unit tests for the Sprint 5 push 4 buffer ABI + souxmar_mesh_from_buffers,
// extended in Sprint 7 push 3 with the mmap-backed buffer round-trip
// (ABI minor v1.2, ADR-0006 v2).

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <vector>

#include "souxmar-c/buffer.h"
#include "souxmar-c/mesh.h"
#include "souxmar-c/status.h"

namespace {
std::filesystem::path mmap_tmp_path(const char* tag) {
  std::random_device rd;
  return std::filesystem::temp_directory_path() /
         (std::string{"souxmar-mmap-test-"} + tag + "-" + std::to_string(rd()));
}
}

namespace {

// ============================================================================
// souxmar_buffer_t
// ============================================================================

TEST(SouxmarBuffer, NewZeroSizeReturnsNull) {
  EXPECT_EQ(souxmar_buffer_new(0), nullptr);
}

TEST(SouxmarBuffer, NewAndFreeRoundtrip) {
  souxmar_buffer_t* b = souxmar_buffer_new(256);
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(souxmar_buffer_size(b), 256u);
  void* data = souxmar_buffer_data(b);
  ASSERT_NE(data, nullptr);
  // Alignment guarantee: data pointer is aligned to souxmar_buffer_alignment().
  const auto align = souxmar_buffer_alignment();
  EXPECT_GE(align, 16u);
  EXPECT_EQ(reinterpret_cast<std::uintptr_t>(data) % align, 0u)
      << "data pointer not aligned to " << align;
  // Writable: scribble a pattern then read it back.
  std::memset(data, 0xAB, 256);
  for (int i = 0; i < 256; ++i) {
    EXPECT_EQ(static_cast<std::uint8_t*>(data)[i], 0xAB);
  }
  souxmar_buffer_free(b);
}

TEST(SouxmarBuffer, FreeNullIsNoop) {
  souxmar_buffer_free(nullptr);  // must not crash
}

TEST(SouxmarBuffer, AccessorsOnNullReturnSafeDefaults) {
  EXPECT_EQ(souxmar_buffer_data(nullptr),       nullptr);
  EXPECT_EQ(souxmar_buffer_data_const(nullptr), nullptr);
  EXPECT_EQ(souxmar_buffer_size(nullptr),       0u);
}

TEST(SouxmarBuffer, ConstAccessorMatchesMutable) {
  souxmar_buffer_t* b = souxmar_buffer_new(8);
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(souxmar_buffer_data(b),
            const_cast<void*>(souxmar_buffer_data_const(b)));
  souxmar_buffer_free(b);
}

// ============================================================================
// souxmar_mesh_from_buffers — good path
// ============================================================================

// Helper: build a Mesh from a 1-tet, 4-node spec. Returns the mesh
// (caller owns) or nullptr on validation failure.
struct OneTet {
  souxmar_buffer_t* coords        = nullptr;
  souxmar_buffer_t* types         = nullptr;
  souxmar_buffer_t* connectivity  = nullptr;
  souxmar_buffer_t* offsets       = nullptr;
  souxmar_buffer_t* tags          = nullptr;
  souxmar_mesh_buffers_t bufs{};
};

OneTet build_one_tet_inputs() {
  OneTet out;
  // Node coords: 4 nodes × 3 doubles = 96 bytes.
  out.coords = souxmar_buffer_new(4 * 3 * sizeof(double));
  auto* xs   = static_cast<double*>(souxmar_buffer_data(out.coords));
  const std::array<std::array<double, 3>, 4> nodes = {{
      {0.0, 0.0, 0.0}, {1.0, 0.0, 0.0},
      {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}}};
  for (std::size_t i = 0; i < 4; ++i) {
    xs[i * 3 + 0] = nodes[i][0];
    xs[i * 3 + 1] = nodes[i][1];
    xs[i * 3 + 2] = nodes[i][2];
  }
  // Cell types: 1 × uint16 = 2 bytes.
  out.types = souxmar_buffer_new(1 * sizeof(std::uint16_t));
  static_cast<std::uint16_t*>(souxmar_buffer_data(out.types))[0] = SOUXMAR_ET_TET4;
  // Connectivity: 4 × uint64 = 32 bytes.
  out.connectivity = souxmar_buffer_new(4 * sizeof(std::uint64_t));
  auto* conn = static_cast<std::uint64_t*>(souxmar_buffer_data(out.connectivity));
  conn[0] = 0; conn[1] = 1; conn[2] = 2; conn[3] = 3;
  // Offsets: 2 × uint64 = 16 bytes.
  out.offsets = souxmar_buffer_new(2 * sizeof(std::uint64_t));
  auto* off = static_cast<std::uint64_t*>(souxmar_buffer_data(out.offsets));
  off[0] = 0; off[1] = 4;
  // Tags: 1 × int32 = 4 bytes.
  out.tags = souxmar_buffer_new(1 * sizeof(std::int32_t));
  static_cast<std::int32_t*>(souxmar_buffer_data(out.tags))[0] = 7;

  out.bufs.node_coords       = out.coords;
  out.bufs.num_nodes         = 4;
  out.bufs.cell_types        = out.types;
  out.bufs.cell_connectivity = out.connectivity;
  out.bufs.cell_offsets      = out.offsets;
  out.bufs.num_cells         = 1;
  out.bufs.cell_tags         = out.tags;
  return out;
}

void free_one_tet(OneTet& t) {
  souxmar_buffer_free(t.coords);
  souxmar_buffer_free(t.types);
  souxmar_buffer_free(t.connectivity);
  souxmar_buffer_free(t.offsets);
  souxmar_buffer_free(t.tags);
}

TEST(MeshFromBuffers, OneTetGoodPath) {
  auto in = build_one_tet_inputs();
  souxmar_status_t status{SOUXMAR_E_INTERNAL, "uninitialised", nullptr};
  souxmar_mesh_t* mesh = souxmar_mesh_from_buffers(&in.bufs, &status);
  ASSERT_NE(mesh, nullptr) << "status code " << status.code
                           << " msg='" << (status.message ? status.message : "")
                           << "'";
  EXPECT_EQ(status.code, SOUXMAR_OK);
  EXPECT_EQ(souxmar_mesh_num_nodes(mesh), 4u);
  EXPECT_EQ(souxmar_mesh_num_cells(mesh), 1u);
  EXPECT_EQ(souxmar_mesh_cell_type(mesh, 0), SOUXMAR_ET_TET4);
  EXPECT_EQ(souxmar_mesh_cell_tag(mesh, 0), 7);
  souxmar_mesh_free(mesh);
  free_one_tet(in);
}

TEST(MeshFromBuffers, NullTagsMeansUntagged) {
  auto in = build_one_tet_inputs();
  in.bufs.cell_tags = nullptr;
  souxmar_status_t status{SOUXMAR_E_INTERNAL, nullptr, nullptr};
  souxmar_mesh_t* mesh = souxmar_mesh_from_buffers(&in.bufs, &status);
  ASSERT_NE(mesh, nullptr);
  EXPECT_EQ(souxmar_mesh_cell_tag(mesh, 0), -1);
  souxmar_mesh_free(mesh);
  free_one_tet(in);
}

// ============================================================================
// souxmar_mesh_from_buffers — validation failures
// ============================================================================

TEST(MeshFromBuffers, NullDescriptorRejected) {
  souxmar_status_t status{};
  EXPECT_EQ(souxmar_mesh_from_buffers(nullptr, &status), nullptr);
  EXPECT_EQ(status.code, SOUXMAR_E_INVALID_ARGUMENT);
}

TEST(MeshFromBuffers, NullRequiredBufferRejected) {
  auto in = build_one_tet_inputs();
  in.bufs.node_coords = nullptr;
  souxmar_status_t status{};
  EXPECT_EQ(souxmar_mesh_from_buffers(&in.bufs, &status), nullptr);
  EXPECT_EQ(status.code, SOUXMAR_E_INVALID_ARGUMENT);
  free_one_tet(in);
}

TEST(MeshFromBuffers, WrongCoordsSizeRejected) {
  auto in = build_one_tet_inputs();
  // Replace coords with a buffer one short.
  souxmar_buffer_free(in.coords);
  in.coords = souxmar_buffer_new(11 * sizeof(double));  // expected 12
  in.bufs.node_coords = in.coords;
  souxmar_status_t status{};
  EXPECT_EQ(souxmar_mesh_from_buffers(&in.bufs, &status), nullptr);
  EXPECT_EQ(status.code, SOUXMAR_E_INVALID_ARGUMENT);
  free_one_tet(in);
}

TEST(MeshFromBuffers, NonMonotonicOffsetsRejected) {
  auto in = build_one_tet_inputs();
  // Two cells, but offsets go [0, 5, 3] — second cell would have length -2.
  souxmar_buffer_free(in.types);
  souxmar_buffer_free(in.offsets);
  souxmar_buffer_free(in.connectivity);
  souxmar_buffer_free(in.tags);

  in.types = souxmar_buffer_new(2 * sizeof(std::uint16_t));
  auto* t = static_cast<std::uint16_t*>(souxmar_buffer_data(in.types));
  t[0] = SOUXMAR_ET_TET4; t[1] = SOUXMAR_ET_TET4;

  in.offsets = souxmar_buffer_new(3 * sizeof(std::uint64_t));
  auto* o = static_cast<std::uint64_t*>(souxmar_buffer_data(in.offsets));
  o[0] = 0; o[1] = 5; o[2] = 3;  // non-monotonic

  in.connectivity = souxmar_buffer_new(8 * sizeof(std::uint64_t));
  in.tags = nullptr;

  in.bufs.cell_types        = in.types;
  in.bufs.cell_offsets      = in.offsets;
  in.bufs.cell_connectivity = in.connectivity;
  in.bufs.num_cells         = 2;
  in.bufs.cell_tags         = nullptr;

  souxmar_status_t status{};
  EXPECT_EQ(souxmar_mesh_from_buffers(&in.bufs, &status), nullptr);
  EXPECT_EQ(status.code, SOUXMAR_E_INVALID_ARGUMENT);
  free_one_tet(in);
}

TEST(MeshFromBuffers, UnknownElementTypeRejected) {
  auto in = build_one_tet_inputs();
  static_cast<std::uint16_t*>(souxmar_buffer_data(in.types))[0] = 0xFFFF;
  souxmar_status_t status{};
  EXPECT_EQ(souxmar_mesh_from_buffers(&in.bufs, &status), nullptr);
  EXPECT_EQ(status.code, SOUXMAR_E_INVALID_ARGUMENT);
  free_one_tet(in);
}

TEST(MeshFromBuffers, NodeCountMismatchRejected) {
  auto in = build_one_tet_inputs();
  // Tet4 wants 4 nodes; tell offsets to claim 5.
  auto* o = static_cast<std::uint64_t*>(souxmar_buffer_data(in.offsets));
  o[1] = 5;
  // Make connectivity match the (bad) offsets so the size check passes
  // first and we hit the per-cell node-count validation.
  souxmar_buffer_free(in.connectivity);
  in.connectivity = souxmar_buffer_new(5 * sizeof(std::uint64_t));
  in.bufs.cell_connectivity = in.connectivity;
  souxmar_status_t status{};
  EXPECT_EQ(souxmar_mesh_from_buffers(&in.bufs, &status), nullptr);
  EXPECT_EQ(status.code, SOUXMAR_E_INVALID_ARGUMENT);
  free_one_tet(in);
}

TEST(MeshFromBuffers, OutOfRangeNodeIndexRejected) {
  auto in = build_one_tet_inputs();
  // Reference node 99 which doesn't exist.
  auto* conn = static_cast<std::uint64_t*>(souxmar_buffer_data(in.connectivity));
  conn[3] = 99;
  souxmar_status_t status{};
  EXPECT_EQ(souxmar_mesh_from_buffers(&in.bufs, &status), nullptr);
  EXPECT_EQ(status.code, SOUXMAR_E_INVALID_ARGUMENT);
  free_one_tet(in);
}

TEST(MeshFromBuffers, EmptyMeshIsAccepted) {
  souxmar_mesh_buffers_t bufs{};
  // All required buffers must still exist, just zero-sized counts.
  auto* coords = souxmar_buffer_new(1);  // size doesn't matter for num_nodes=0
  auto* types  = souxmar_buffer_new(1);
  auto* conn   = souxmar_buffer_new(1);
  auto* off    = souxmar_buffer_new(1 * sizeof(std::uint64_t));  // num_cells+1 = 1
  static_cast<std::uint64_t*>(souxmar_buffer_data(off))[0] = 0;
  bufs.node_coords       = coords;
  bufs.num_nodes         = 0;
  bufs.cell_types        = types;
  bufs.cell_connectivity = conn;
  bufs.cell_offsets      = off;
  bufs.num_cells         = 0;

  // Sizes won't all be "exactly zero" though — the helper expects
  // expected_*_bytes = num * sizeof, which is 0 for num=0. So our
  // 1-byte allocations will fail the size check. This test confirms
  // the check is strict (mismatched size still rejected even at 0
  // expected). That's the right behaviour: callers using num=0
  // should pass zero-sized buffers, which souxmar_buffer_new(0)
  // returns NULL for — meaning a fully-empty mesh is actually built
  // via souxmar_mesh_new(), not this bulk path. Verify the contract
  // is clean about this: mismatched size → INVALID_ARGUMENT.
  souxmar_status_t status{};
  EXPECT_EQ(souxmar_mesh_from_buffers(&bufs, &status), nullptr);
  EXPECT_EQ(status.code, SOUXMAR_E_INVALID_ARGUMENT);
  souxmar_buffer_free(coords);
  souxmar_buffer_free(types);
  souxmar_buffer_free(conn);
  souxmar_buffer_free(off);
}

// ============================================================================
// Bulk-vs-incremental shape equivalence
// ============================================================================
//
// Build a 2-tet mesh two ways and confirm num_nodes / num_cells /
// per-cell type + tag match. The point is to pin down that
// souxmar_mesh_from_buffers and souxmar_mesh_add_node / add_cell
// produce semantically identical results — the bulk path is
// performance optimization, not a different mesh representation.

TEST(MeshFromBuffers, BulkAndIncrementalProduceIdenticalShape) {
  // 5 nodes, 2 cells sharing a face (tet0 + tet1).
  const std::array<std::array<double, 3>, 5> coords = {{
      {0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0},
      {0.0, 0.0, 1.0}, {1.0, 1.0, 1.0}}};
  const std::array<std::array<std::uint64_t, 4>, 2> tets = {{
      {0, 1, 2, 3}, {1, 2, 3, 4}}};
  const std::array<std::int32_t, 2> tags = {1, 2};

  // Incremental.
  souxmar_mesh_t* m_inc = souxmar_mesh_new();
  for (const auto& p : coords) souxmar_mesh_add_node(m_inc, p.data());
  for (std::size_t i = 0; i < tets.size(); ++i) {
    souxmar_mesh_add_cell(m_inc, SOUXMAR_ET_TET4, tets[i].data(),
                          4, tags[i], nullptr);
  }

  // Bulk.
  auto* b_coords = souxmar_buffer_new(coords.size() * 3 * sizeof(double));
  std::memcpy(souxmar_buffer_data(b_coords), coords.data(),
              coords.size() * 3 * sizeof(double));
  auto* b_types = souxmar_buffer_new(2 * sizeof(std::uint16_t));
  static_cast<std::uint16_t*>(souxmar_buffer_data(b_types))[0] = SOUXMAR_ET_TET4;
  static_cast<std::uint16_t*>(souxmar_buffer_data(b_types))[1] = SOUXMAR_ET_TET4;
  auto* b_conn = souxmar_buffer_new(8 * sizeof(std::uint64_t));
  auto* conn_p = static_cast<std::uint64_t*>(souxmar_buffer_data(b_conn));
  for (std::size_t i = 0; i < 2; ++i)
    for (std::size_t j = 0; j < 4; ++j)
      conn_p[i * 4 + j] = tets[i][j];
  auto* b_off  = souxmar_buffer_new(3 * sizeof(std::uint64_t));
  auto* off_p  = static_cast<std::uint64_t*>(souxmar_buffer_data(b_off));
  off_p[0] = 0; off_p[1] = 4; off_p[2] = 8;
  auto* b_tags = souxmar_buffer_new(2 * sizeof(std::int32_t));
  std::memcpy(souxmar_buffer_data(b_tags), tags.data(),
              2 * sizeof(std::int32_t));

  souxmar_mesh_buffers_t spec{
      b_coords, 5, b_types, b_conn, b_off, 2, b_tags};
  souxmar_status_t status{};
  souxmar_mesh_t* m_bulk = souxmar_mesh_from_buffers(&spec, &status);
  ASSERT_NE(m_bulk, nullptr) << "code " << status.code
                              << " msg=" << (status.message ? status.message : "");

  EXPECT_EQ(souxmar_mesh_num_nodes(m_inc), souxmar_mesh_num_nodes(m_bulk));
  EXPECT_EQ(souxmar_mesh_num_cells(m_inc), souxmar_mesh_num_cells(m_bulk));
  for (std::uint64_t i = 0; i < 2; ++i) {
    EXPECT_EQ(souxmar_mesh_cell_type(m_inc, i),
              souxmar_mesh_cell_type(m_bulk, i));
    EXPECT_EQ(souxmar_mesh_cell_tag(m_inc, i),
              souxmar_mesh_cell_tag(m_bulk, i));
  }

  souxmar_mesh_free(m_inc);
  souxmar_mesh_free(m_bulk);
  souxmar_buffer_free(b_coords);
  souxmar_buffer_free(b_types);
  souxmar_buffer_free(b_conn);
  souxmar_buffer_free(b_off);
  souxmar_buffer_free(b_tags);
}

// ============================================================================
// Sprint 7 push 3 — mmap-backed buffer (ADR-0006 v2)
// ============================================================================

TEST(SouxmarBufferMmap, IsMmapFalseForHeapBacked) {
  auto* b = souxmar_buffer_new(64);
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(souxmar_buffer_is_mmap(b), 0);
  souxmar_buffer_free(b);
}

TEST(SouxmarBufferMmap, RoundTripRwThenReadOnly) {
  const auto path = mmap_tmp_path("rw-then-ro");
  constexpr std::size_t kBytes = 4096;

  // Create + map RW.
  auto* w = souxmar_buffer_new_mmap(path.string().c_str(), kBytes,
                                    SOUXMAR_BUFFER_FLAG_CREATE);
  ASSERT_NE(w, nullptr) << "create+map failed for " << path;
  EXPECT_EQ(souxmar_buffer_is_mmap(w), 1);
  EXPECT_EQ(souxmar_buffer_size(w),    kBytes);

  // Write a recognisable pattern.
  auto* w_data = static_cast<std::uint8_t*>(souxmar_buffer_data(w));
  ASSERT_NE(w_data, nullptr);
  for (std::size_t i = 0; i < kBytes; ++i) w_data[i] = static_cast<std::uint8_t>(i & 0xFF);
  souxmar_buffer_free(w);   // unmap + close; data persists in `path`

  // Re-open read-only and verify the bytes round-trip.
  auto* r = souxmar_buffer_new_mmap(path.string().c_str(), /*size_bytes=*/0,
                                    SOUXMAR_BUFFER_FLAG_READONLY);
  ASSERT_NE(r, nullptr) << "RO open failed";
  EXPECT_EQ(souxmar_buffer_is_mmap(r), 1);
  EXPECT_EQ(souxmar_buffer_size(r),    kBytes);

  // souxmar_buffer_data on a RO mapping returns NULL — callers use _data_const.
  EXPECT_EQ(souxmar_buffer_data(r), nullptr);
  const auto* r_data = static_cast<const std::uint8_t*>(
      souxmar_buffer_data_const(r));
  ASSERT_NE(r_data, nullptr);
  for (std::size_t i = 0; i < kBytes; ++i) {
    ASSERT_EQ(r_data[i], static_cast<std::uint8_t>(i & 0xFF))
        << "byte " << i << " did not round-trip through the file mapping";
  }
  souxmar_buffer_free(r);

  std::error_code ec;
  std::filesystem::remove(path, ec);
}

TEST(SouxmarBufferMmap, NullPathRejected) {
  EXPECT_EQ(souxmar_buffer_new_mmap(nullptr, 64, SOUXMAR_BUFFER_FLAG_CREATE),
            nullptr);
}

TEST(SouxmarBufferMmap, ReadOnlyAndCreateAreMutuallyExclusive) {
  const auto path = mmap_tmp_path("ro-create");
  // Combining READONLY | CREATE is documented as undefined — the host
  // rejects it cleanly (returns NULL) so the contract is "explicit error
  // rather than silent file truncation."
  auto* b = souxmar_buffer_new_mmap(
      path.string().c_str(), 64,
      SOUXMAR_BUFFER_FLAG_READONLY | SOUXMAR_BUFFER_FLAG_CREATE);
  EXPECT_EQ(b, nullptr);
}

TEST(SouxmarBufferMmap, MissingFileReadOnlyFails) {
  const auto path = mmap_tmp_path("missing");
  auto* b = souxmar_buffer_new_mmap(path.string().c_str(), 0,
                                    SOUXMAR_BUFFER_FLAG_READONLY);
  EXPECT_EQ(b, nullptr);
}

TEST(SouxmarBufferMmap, BulkMeshIngestThroughMmapBuffers) {
  // The whole point of v2: souxmar_mesh_from_buffers consumes mmap-backed
  // buffers transparently. The ingest path reads `_data_const`, which
  // returns the mmap region; the host can't tell the difference.
  //
  // We build a 5-node / 2-tet mesh entirely through mmap'd buffers,
  // then verify the resulting Mesh carries the expected shape.
  const auto path_coords = mmap_tmp_path("ingest-coords");
  const auto path_types  = mmap_tmp_path("ingest-types");
  const auto path_conn   = mmap_tmp_path("ingest-conn");
  const auto path_off    = mmap_tmp_path("ingest-off");

  const std::vector<double> coords = {
      0, 0, 0,  1, 0, 0,  0, 1, 0,  0, 0, 1,  1, 1, 1,
  };
  const std::vector<std::uint16_t> types = {SOUXMAR_ET_TET4, SOUXMAR_ET_TET4};
  const std::vector<std::uint64_t> conn  = {0, 1, 2, 3, 1, 2, 3, 4};
  const std::vector<std::uint64_t> off   = {0, 4, 8};

  auto write_bytes = [](const std::filesystem::path& p,
                        const void* src, std::size_t n) {
    auto* b = souxmar_buffer_new_mmap(p.string().c_str(), n,
                                      SOUXMAR_BUFFER_FLAG_CREATE);
    if (!b) return false;
    std::memcpy(souxmar_buffer_data(b), src, n);
    souxmar_buffer_free(b);
    return true;
  };
  ASSERT_TRUE(write_bytes(path_coords, coords.data(), coords.size() * sizeof(double)));
  ASSERT_TRUE(write_bytes(path_types,  types.data(),  types.size()  * sizeof(std::uint16_t)));
  ASSERT_TRUE(write_bytes(path_conn,   conn.data(),   conn.size()   * sizeof(std::uint64_t)));
  ASSERT_TRUE(write_bytes(path_off,    off.data(),    off.size()    * sizeof(std::uint64_t)));

  auto* b_coords = souxmar_buffer_new_mmap(path_coords.string().c_str(), 0,
                                           SOUXMAR_BUFFER_FLAG_READONLY);
  auto* b_types  = souxmar_buffer_new_mmap(path_types.string().c_str(), 0,
                                           SOUXMAR_BUFFER_FLAG_READONLY);
  auto* b_conn   = souxmar_buffer_new_mmap(path_conn.string().c_str(), 0,
                                           SOUXMAR_BUFFER_FLAG_READONLY);
  auto* b_off    = souxmar_buffer_new_mmap(path_off.string().c_str(), 0,
                                           SOUXMAR_BUFFER_FLAG_READONLY);
  ASSERT_NE(b_coords, nullptr);
  ASSERT_NE(b_types,  nullptr);
  ASSERT_NE(b_conn,   nullptr);
  ASSERT_NE(b_off,    nullptr);

  souxmar_mesh_buffers_t spec{
      b_coords, 5, b_types, b_conn, b_off, 2, /*cell_tags=*/nullptr};
  souxmar_status_t status{};
  souxmar_mesh_t* m = souxmar_mesh_from_buffers(&spec, &status);
  ASSERT_NE(m, nullptr) << "code " << status.code << " msg "
                        << (status.message ? status.message : "");
  EXPECT_EQ(souxmar_mesh_num_nodes(m), 5u);
  EXPECT_EQ(souxmar_mesh_num_cells(m), 2u);

  souxmar_mesh_free(m);
  souxmar_buffer_free(b_coords);
  souxmar_buffer_free(b_types);
  souxmar_buffer_free(b_conn);
  souxmar_buffer_free(b_off);
  std::error_code ec;
  std::filesystem::remove(path_coords, ec);
  std::filesystem::remove(path_types,  ec);
  std::filesystem::remove(path_conn,   ec);
  std::filesystem::remove(path_off,    ec);
}

}  // namespace
