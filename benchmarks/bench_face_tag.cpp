// SPDX-License-Identifier: Apache-2.0
//
// Sprint 9 push 6 — perf coverage for the ABI v1.3 per-face-tag
// surface (ADR-0012). The push-2 ratchet promised:
//
//   * untagged faces cost zero memory + constant-time lookup
//     regardless of mesh size;
//   * setting a face tag is amortised O(1);
//   * the full enumeration via `tagged_faces()` scales with the
//     number of tagged faces, not the mesh size.
//
// This benchmark pins those promises down. The perf-regression CI
// gate (5%, raised from 10% in this same push) catches any future
// change that breaks them.
//
// Workloads:
//
//   BM_FaceTag_Set        — sparse-map insert throughput on a fresh
//                           mesh: tag face 0 of every cell. Tests the
//                           insert-amortised-O(1) promise.
//
//   BM_FaceTag_GetHit     — lookup of a populated tag, exercises the
//                           happy-path indexed lookup.
//
//   BM_FaceTag_GetMiss    — lookup of an untagged face, exercises the
//                           empty-or-missing branch. ADR-0012 promises
//                           this is constant-time regardless of mesh
//                           size — a future regression that ever made
//                           it grow with size would surface here.
//
//   BM_FaceTag_Enumerate  — full `tagged_faces()` cost vs. tag count.
//
// Dimensions: 1K, 10K, 100K cells. Tet4 mesh — same shape the
// existing mesh-construction bench uses for its construction-throughput
// numbers, so the cell count is comparable across reports.

#include <benchmark/benchmark.h>

#include "souxmar/core/mesh.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <random>

using namespace souxmar::core;

namespace {

// Build a synthetic Tet4 mesh of `n_tets` independent (non-shared)
// tetrahedra. The shape doesn't matter for face-tag perf — we just
// need cells with valid local-face indices 0..3 — but using random
// coordinates rather than a regular grid stops a future
// position-based heuristic from accidentally short-circuiting.
Mesh build_tet_mesh(std::size_t n_tets) {
  Mesh m;
  m.reserve_nodes(4 * n_tets);
  m.reserve_cells(n_tets);
  std::mt19937                            rng(0xC0FFEEu);
  std::uniform_real_distribution<double>  coord(0.0, 1.0);
  for (std::size_t c = 0; c < n_tets; ++c) {
    const auto n0 = m.add_node({coord(rng), coord(rng), coord(rng)});
    const auto n1 = m.add_node({coord(rng), coord(rng), coord(rng)});
    const auto n2 = m.add_node({coord(rng), coord(rng), coord(rng)});
    const auto n3 = m.add_node({coord(rng), coord(rng), coord(rng)});
    const std::array<NodeIndex, 4> nodes{{n0, n1, n2, n3}};
    m.add_cell(ElementType::Tet4, nodes);
  }
  return m;
}

}  // namespace

// BM_FaceTag_Set — measures the per-tag insert cost on a fresh sparse
// map. One tag per cell (face index 0).
static void BM_FaceTag_Set(benchmark::State& state) {
  const std::size_t n_tets = static_cast<std::size_t>(state.range(0));
  for (auto _ : state) {
    state.PauseTiming();
    Mesh m = build_tet_mesh(n_tets);
    state.ResumeTiming();

    for (std::size_t c = 0; c < n_tets; ++c) {
      m.set_face_tag(CellIndex{c}, 0,
                     EntityTag{static_cast<std::int32_t>(c + 1)});
    }
    benchmark::DoNotOptimize(m);
  }
  state.SetItemsProcessed(state.iterations() *
                          static_cast<std::int64_t>(n_tets));
}
BENCHMARK(BM_FaceTag_Set)->Arg(1'000)->Arg(10'000)->Arg(100'000);

// BM_FaceTag_GetHit — every cell has face 0 tagged; benchmark looks
// up that face on a rotating cell index.
static void BM_FaceTag_GetHit(benchmark::State& state) {
  const std::size_t n_tets = static_cast<std::size_t>(state.range(0));
  Mesh m = build_tet_mesh(n_tets);
  for (std::size_t c = 0; c < n_tets; ++c) {
    m.set_face_tag(CellIndex{c}, 0,
                   EntityTag{static_cast<std::int32_t>(c + 1)});
  }
  std::size_t i = 0;
  for (auto _ : state) {
    const auto t = m.face_tag(CellIndex{i % n_tets}, 0);
    benchmark::DoNotOptimize(t);
    ++i;
  }
}
BENCHMARK(BM_FaceTag_GetHit)->Arg(1'000)->Arg(10'000)->Arg(100'000);

// BM_FaceTag_GetMiss — no face tags set anywhere; the sparse map is
// empty and the lookup must short-circuit constant-time. ADR-0012's
// "zero cost when nothing is tagged" promise lives here.
static void BM_FaceTag_GetMiss(benchmark::State& state) {
  const std::size_t n_tets = static_cast<std::size_t>(state.range(0));
  Mesh m = build_tet_mesh(n_tets);
  std::size_t i = 0;
  for (auto _ : state) {
    const auto t = m.face_tag(CellIndex{i % n_tets}, 0);
    benchmark::DoNotOptimize(t);
    ++i;
  }
}
BENCHMARK(BM_FaceTag_GetMiss)->Arg(1'000)->Arg(10'000)->Arg(100'000);

// BM_FaceTag_Enumerate — every cell has one tagged face; measures
// the full enumeration cost.
static void BM_FaceTag_Enumerate(benchmark::State& state) {
  const std::size_t n_tets = static_cast<std::size_t>(state.range(0));
  Mesh m = build_tet_mesh(n_tets);
  for (std::size_t c = 0; c < n_tets; ++c) {
    m.set_face_tag(CellIndex{c}, static_cast<std::uint8_t>(c % 4),
                   EntityTag{static_cast<std::int32_t>(c % 10 + 1)});
  }
  for (auto _ : state) {
    auto entries = m.tagged_faces();
    benchmark::DoNotOptimize(entries);
  }
  state.SetItemsProcessed(state.iterations() *
                          static_cast<std::int64_t>(n_tets));
}
BENCHMARK(BM_FaceTag_Enumerate)->Arg(1'000)->Arg(10'000)->Arg(100'000);
