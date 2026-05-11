// SPDX-License-Identifier: Apache-2.0
//
// Sprint 9 push 9 — overhead benchmark for souxmar::plugin::HeapAccountant.
//
// The accountant is now in the AI tool-dispatcher hot path: every
// tool dispatch takes one `snapshot()` before the handler and one
// `delta_since(...)` after. If those two calls cost more than a small
// fraction of the documented per-dispatch budget (20 µs warm, from
// ENGINEERING_PRACTICES.md § Performance budgets), leaving accounting
// always-on becomes a measurable tax. This benchmark pins the cost
// down so the perf-regression gate catches any future drift.
//
// The headline number on Linux (glibc ≥ 2.33) is the cost of
// `mallinfo2()` — measured as < 1 µs per call on the reference
// hardware. On unsupported platforms the entire pair is essentially
// free (returns the zero-initialised Sample struct). The gate's 5%
// relative threshold + the inline-SVG bar chart in the dashboard
// surface either case.
//
// Workloads:
//
//   BM_HeapAccountant_Snapshot  — one snapshot per iteration.
//   BM_HeapAccountant_DeltaPair — snapshot + delta_since per iteration
//                                  (mirrors what the tool dispatcher
//                                  does on every call).
//   BM_HeapAccountant_IsSupported — one is_supported() call per
//                                    iteration. Cold-path sanity:
//                                    callers that gate fancy reporting
//                                    on supported-ness expect near-zero
//                                    cost here.

#include <benchmark/benchmark.h>

#include "souxmar/plugin/heap_accountant.h"

using souxmar::plugin::HeapAccountant;

static void BM_HeapAccountant_Snapshot(benchmark::State& state) {
  for (auto _ : state) {
    auto s = HeapAccountant::snapshot();
    benchmark::DoNotOptimize(s);
  }
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_HeapAccountant_Snapshot)->Unit(benchmark::kNanosecond);

static void BM_HeapAccountant_DeltaPair(benchmark::State& state) {
  for (auto _ : state) {
    const auto before = HeapAccountant::snapshot();
    benchmark::DoNotOptimize(before);
    auto delta = HeapAccountant::delta_since(before);
    benchmark::DoNotOptimize(delta);
  }
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_HeapAccountant_DeltaPair)->Unit(benchmark::kNanosecond);

static void BM_HeapAccountant_IsSupported(benchmark::State& state) {
  for (auto _ : state) {
    bool s = HeapAccountant::is_supported();
    benchmark::DoNotOptimize(s);
  }
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_HeapAccountant_IsSupported)->Unit(benchmark::kNanosecond);
