// SPDX-License-Identifier: Apache-2.0
//
// Sprint 9 push 7 — plugin-dispatch hot-path benchmark.
//
// The ENGINEERING_PRACTICES.md § Performance budgets table has named
// "Plugin call overhead (no-op tool) < 20 µs (warm)" since Sprint 0;
// Sprint 9's "Performance + scale" theme is where that budget moves
// from "documented" to "enforced". This benchmark + the
// perf-regression CI gate (Sprint 9 push 6) close the loop.
//
// What's measured: end-to-end cost of one `RegistryDispatcher::dispatch`
// call against a no-op mesher capability. That's
//   * the dispatcher's namespace-prefix routing (`mesher.*` →
//     `find_mesher`),
//   * Value-tree input handling (the stage input bag, here empty),
//   * the C ABI shim that lowers the call into the plugin vtable,
//   * the plugin's `mesh_fn` (here returning a freshly-allocated
//     empty Mesh — the minimum legal output),
//   * the host-side StageOutput wrapping that the runner expects.
//
// What's NOT measured:
//   * Plugin discovery (dlopen + symbol resolution) — that's a session
//     amortised cost, not per-call. The benchmark statically registers
//     a vtable into the Registry instead.
//   * Pipeline parsing / DAG analysis / cache lookups — those live one
//     layer up at the Runner and have their own benchmarks (planned).
//
// Workload shape: a single call per benchmark iteration. Google
// Benchmark's `--benchmark_min_time` knob controls how many iterations
// it runs before the timing settles. The CI gate runs at
// `min_time=0.2s repetitions=3` — plenty for the µs-scale budget.

#include <benchmark/benchmark.h>

#include "souxmar-c/abi.h"
#include "souxmar-c/mesh.h"
#include "souxmar-c/mesher.h"
#include "souxmar-c/status.h"
#include "souxmar/pipeline/registry_dispatcher.h"
#include "souxmar/pipeline/runner.h"
#include "souxmar/pipeline/value.h"
#include "souxmar/plugin/registry.h"

#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <variant>

namespace {

// Minimal mesher vtable: produce an empty Mesh and return OK. The
// dispatcher wraps the resulting souxmar_mesh_t* into a StageOutput
// of Kind::Mesh, which is the path most callers exercise.
souxmar_status_t noop_mesh_fn(const souxmar_geometry_t*       /*geometry*/,
                              const souxmar_mesher_options_t* /*options*/,
                              souxmar_mesh_t**                out_mesh,
                              void*                           /*user_data*/) {
  if (out_mesh == nullptr) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "out_mesh is NULL");
  }
  *out_mesh = souxmar_mesh_new();
  return (*out_mesh != nullptr)
      ? souxmar_status_ok()
      : souxmar_status_error(SOUXMAR_E_OUT_OF_MEMORY, "souxmar_mesh_new");
}

constexpr souxmar_mesher_vtable_t kNoopVtable = {
    SOUXMAR_ABI_VERSION_MAJOR,
    &noop_mesh_fn,
    nullptr,
};

// A self-contained dispatcher harness — owns the Registry, the
// RegistryDispatcher, and a stable empty Value the benchmark hot
// loop reuses on every call. Construction cost is amortised across
// `--benchmark_min_time`; only the dispatch call itself is timed.
struct DispatchHarness {
  souxmar::plugin::Registry                              registry;
  std::unique_ptr<souxmar::pipeline::RegistryDispatcher> dispatcher;
  souxmar::pipeline::Value                               empty_inputs;
  std::map<std::string, std::shared_ptr<void>>           empty_upstream;

  DispatchHarness() : empty_inputs(souxmar::pipeline::Value::map({})) {
    const auto reg = registry.add_mesher(
        "mesher.noop",
        "dev.souxmar.bench.noop",  // synthetic plugin id
        &kNoopVtable,
        /*user_data=*/nullptr);
    if (std::holds_alternative<souxmar::plugin::RegistryError>(reg)) {
      // Registration failure is a structural bug — surface immediately
      // so the benchmark doesn't silently report wildly-inflated numbers
      // from a missing capability path.
      std::abort();
    }
    dispatcher = std::make_unique<souxmar::pipeline::RegistryDispatcher>(registry);
  }
};

}  // namespace

// BM_PluginDispatch_Warm — every iteration dispatches one no-op call.
// The Registry + dispatcher are constructed once (outside the timed
// region); the call path that runs N times per iteration is the
// production hot path. Per-call cost target: < 20 µs.
static void BM_PluginDispatch_Warm(benchmark::State& state) {
  DispatchHarness h;
  souxmar::pipeline::DispatchContext ctx{
      /*capability_id=*/"mesher.noop",
      /*inputs=*/h.empty_inputs,
      /*upstream_outputs=*/h.empty_upstream,
  };
  for (auto _ : state) {
    auto result = h.dispatcher->dispatch(ctx);
    benchmark::DoNotOptimize(result);
  }
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_PluginDispatch_Warm)->Unit(benchmark::kMicrosecond);

// BM_PluginDispatch_BatchOf32 — same call, batched 32× per iteration.
// Highlights the per-call delta when the dispatcher state machine is
// fully warm and the std::shared_ptr<void> allocator / Mesh allocator
// have reached steady state. The 32-batch matches the typical
// agentic-session "do these 30-ish things in sequence" shape — if a
// future regression hits the batch but not the singleton (or vice
// versa), the eval-suite's per-task latency picks it up too.
static void BM_PluginDispatch_BatchOf32(benchmark::State& state) {
  DispatchHarness h;
  souxmar::pipeline::DispatchContext ctx{
      /*capability_id=*/"mesher.noop",
      /*inputs=*/h.empty_inputs,
      /*upstream_outputs=*/h.empty_upstream,
  };
  constexpr int kBatch = 32;
  for (auto _ : state) {
    for (int i = 0; i < kBatch; ++i) {
      auto result = h.dispatcher->dispatch(ctx);
      benchmark::DoNotOptimize(result);
    }
  }
  state.SetItemsProcessed(state.iterations() * kBatch);
}
BENCHMARK(BM_PluginDispatch_BatchOf32)->Unit(benchmark::kMicrosecond);

// BM_PluginDispatch_NotFound — the negative path: capability id
// missing from the Registry. ENGINEERING_PRACTICES.md's 20 µs budget
// is a warm-call number; the not-found branch should be at least as
// fast (it short-circuits before touching the plugin vtable). A
// future regression that ever made the miss path slower than the hit
// path surfaces here as an absolute number rather than a ratio.
static void BM_PluginDispatch_NotFound(benchmark::State& state) {
  DispatchHarness h;
  souxmar::pipeline::DispatchContext ctx{
      /*capability_id=*/"mesher.does.not.exist",
      /*inputs=*/h.empty_inputs,
      /*upstream_outputs=*/h.empty_upstream,
  };
  for (auto _ : state) {
    auto result = h.dispatcher->dispatch(ctx);
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(BM_PluginDispatch_NotFound)->Unit(benchmark::kMicrosecond);
