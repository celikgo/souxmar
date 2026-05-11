// SPDX-License-Identifier: Apache-2.0
//
// Benchmark: per-element vs bulk mesh construction.
//
// The per-element path (souxmar_mesh_add_node / souxmar_mesh_add_cell)
// is dominated by per-call ABI overhead at scale. The bulk path
// (souxmar_mesh_from_buffers, Sprint 5 push 4) writes nodes / cells
// directly into host-owned buffers and ingests them in one ABI call.
// This benchmark pins down the actual speedup per platform; the
// nightly perf-regression CI watches the ratio.
//
// Mesh shape: a regular tetrahedral grid of size N×N×N nodes, with 5
// tets per N×N×N cell. The work fans out cleanly so the benchmark
// scales linearly with the problem size.

#include <benchmark/benchmark.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

#include "souxmar-c/buffer.h"
#include "souxmar-c/mesh.h"
#include "souxmar-c/status.h"

namespace {

// One unit cube → 5 tets (the standard "5-tet hex" subdivision used
// for sanity-grade benchmarks). Node ids are relative to the cube's
// 8-node block; the caller maps them to global mesh ids.
constexpr std::array<std::array<std::uint64_t, 4>, 5> kHexToTets = {{
    {0, 1, 2, 5}, {0, 2, 3, 7}, {0, 5, 6, 7},
    {2, 5, 6, 7}, {0, 2, 5, 7},
}};

struct GridSpec {
  std::size_t nx, ny, nz;       // node counts per axis
  std::size_t num_nodes() const { return nx * ny * nz; }
  std::size_t num_cells() const {
    return (nx > 0 ? nx - 1 : 0) *
           (ny > 0 ? ny - 1 : 0) *
           (nz > 0 ? nz - 1 : 0) * kHexToTets.size();
  }
};

std::uint64_t node_at(const GridSpec& g, std::size_t i, std::size_t j, std::size_t k) {
  return static_cast<std::uint64_t>((k * g.ny + j) * g.nx + i);
}

// ============================================================================
// Per-element construction (the slow path we're measuring against)
// ============================================================================
void run_per_element(const GridSpec& g) {
  souxmar_mesh_t* mesh = souxmar_mesh_new();
  souxmar_mesh_reserve_nodes(mesh, g.num_nodes());
  souxmar_mesh_reserve_cells(mesh, g.num_cells());

  for (std::size_t k = 0; k < g.nz; ++k) {
    for (std::size_t j = 0; j < g.ny; ++j) {
      for (std::size_t i = 0; i < g.nx; ++i) {
        const double p[3] = {
            static_cast<double>(i),
            static_cast<double>(j),
            static_cast<double>(k)};
        souxmar_mesh_add_node(mesh, p);
      }
    }
  }
  const std::array<std::uint64_t, 8> corner_offsets = {
      0, 1,
      g.nx, g.nx + 1,
      g.nx * g.ny, g.nx * g.ny + 1,
      g.nx * g.ny + g.nx, g.nx * g.ny + g.nx + 1};
  for (std::size_t k = 0; k + 1 < g.nz; ++k) {
    for (std::size_t j = 0; j + 1 < g.ny; ++j) {
      for (std::size_t i = 0; i + 1 < g.nx; ++i) {
        const std::uint64_t base = node_at(g, i, j, k);
        std::array<std::uint64_t, 8> corners;
        for (std::size_t c = 0; c < 8; ++c) corners[c] = base + corner_offsets[c];
        for (const auto& tet : kHexToTets) {
          std::array<std::uint64_t, 4> nodes = {
              corners[tet[0]], corners[tet[1]], corners[tet[2]], corners[tet[3]]};
          souxmar_mesh_add_cell(mesh, SOUXMAR_ET_TET4, nodes.data(), 4, -1, nullptr);
        }
      }
    }
  }
  benchmark::DoNotOptimize(mesh);
  souxmar_mesh_free(mesh);
}

// ============================================================================
// Bulk construction via souxmar_mesh_from_buffers
// ============================================================================
void run_bulk(const GridSpec& g) {
  const std::size_t nN = g.num_nodes();
  const std::size_t nC = g.num_cells();
  const std::size_t nConn = nC * 4;  // 4 nodes per tet

  // Allocate the bulk buffers up front.
  souxmar_buffer_t* coords = souxmar_buffer_new(nN * 3 * sizeof(double));
  souxmar_buffer_t* types  = souxmar_buffer_new(nC * sizeof(std::uint16_t));
  souxmar_buffer_t* conn   = souxmar_buffer_new(nConn * sizeof(std::uint64_t));
  souxmar_buffer_t* off    = souxmar_buffer_new((nC + 1) * sizeof(std::uint64_t));

  auto* coords_p = static_cast<double*>(souxmar_buffer_data(coords));
  auto* types_p  = static_cast<std::uint16_t*>(souxmar_buffer_data(types));
  auto* conn_p   = static_cast<std::uint64_t*>(souxmar_buffer_data(conn));
  auto* off_p    = static_cast<std::uint64_t*>(souxmar_buffer_data(off));

  // Fill nodes.
  for (std::size_t k = 0; k < g.nz; ++k) {
    for (std::size_t j = 0; j < g.ny; ++j) {
      for (std::size_t i = 0; i < g.nx; ++i) {
        const std::size_t base = (k * g.ny + j) * g.nx + i;
        coords_p[base * 3 + 0] = static_cast<double>(i);
        coords_p[base * 3 + 1] = static_cast<double>(j);
        coords_p[base * 3 + 2] = static_cast<double>(k);
      }
    }
  }

  // Fill cell types + offsets + connectivity.
  std::size_t cell = 0;
  off_p[0] = 0;
  const std::array<std::uint64_t, 8> corner_offsets = {
      0, 1,
      g.nx, g.nx + 1,
      g.nx * g.ny, g.nx * g.ny + 1,
      g.nx * g.ny + g.nx, g.nx * g.ny + g.nx + 1};
  for (std::size_t k = 0; k + 1 < g.nz; ++k) {
    for (std::size_t j = 0; j + 1 < g.ny; ++j) {
      for (std::size_t i = 0; i + 1 < g.nx; ++i) {
        const std::uint64_t base = node_at(g, i, j, k);
        std::array<std::uint64_t, 8> corners;
        for (std::size_t c = 0; c < 8; ++c) corners[c] = base + corner_offsets[c];
        for (const auto& tet : kHexToTets) {
          types_p[cell] = SOUXMAR_ET_TET4;
          for (std::size_t n = 0; n < 4; ++n) {
            conn_p[cell * 4 + n] = corners[tet[n]];
          }
          ++cell;
          off_p[cell] = cell * 4;
        }
      }
    }
  }

  souxmar_mesh_buffers_t spec{
      coords, nN, types, conn, off, nC, /*tags=*/nullptr};
  souxmar_status_t status{};
  souxmar_mesh_t* mesh = souxmar_mesh_from_buffers(&spec, &status);
  benchmark::DoNotOptimize(mesh);
  souxmar_mesh_free(mesh);

  souxmar_buffer_free(coords);
  souxmar_buffer_free(types);
  souxmar_buffer_free(conn);
  souxmar_buffer_free(off);
}

// ============================================================================
// Google Benchmark entry points
// ============================================================================

void BM_PerElement(benchmark::State& state) {
  const auto n = static_cast<std::size_t>(state.range(0));
  GridSpec g{n, n, n};
  for (auto _ : state) run_per_element(g);
  state.SetItemsProcessed(state.iterations() *
      static_cast<std::int64_t>(g.num_nodes() + g.num_cells()));
}

void BM_Bulk(benchmark::State& state) {
  const auto n = static_cast<std::size_t>(state.range(0));
  GridSpec g{n, n, n};
  for (auto _ : state) run_bulk(g);
  state.SetItemsProcessed(state.iterations() *
      static_cast<std::int64_t>(g.num_nodes() + g.num_cells()));
}

}  // namespace

// Grid sizes: 8³ = 512 nodes / 1715 cells (cheap; warmup).
//             16³ ≈ 4k nodes / 18k cells (typical small mesh).
//             32³ ≈ 33k nodes / 150k cells (the speedup window opens here).
//             64³ ≈ 260k nodes / 1.25M cells (where the bulk path's
//                   amortisation matters most).
BENCHMARK(BM_PerElement)->Arg(8)->Arg(16)->Arg(32)->Arg(64)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_Bulk)      ->Arg(8)->Arg(16)->Arg(32)->Arg(64)->Unit(benchmark::kMillisecond);
