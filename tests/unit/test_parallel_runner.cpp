// SPDX-License-Identifier: Apache-2.0
//
// Parallel runner tests. These exercise the scheduler in isolation via a
// MockDispatcher whose dispatch() can sleep + observes concurrent entry,
// so we can prove:
//   * independent stages execute in parallel,
//   * dependent stages execute in topological order,
//   * SingleThreaded plugins serialize across stages even when the runner
//     has multiple workers,
//   * Reentrant plugins overlap freely,
//   * stop_on_first_failure marks downstream stages Skipped,
//   * max_workers=1 still produces a valid run (sequential fallback is
//     transparent to callers).
//
// We don't test cache hit/miss or disk_backing here — those paths are
// shared with the sequential runner and covered by test_pipeline_cache /
// test_pipeline_runner / the integration suite.

#include "souxmar/pipeline/parallel_runner.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "souxmar/pipeline/parser.h"
#include "souxmar/plugin/manifest.h"

using namespace souxmar::pipeline;
using souxmar::plugin::ThreadingModel;
using namespace std::chrono_literals;

namespace {

// Concurrency-aware mock dispatcher. Every dispatch records the capability
// it received plus a snapshot of how many other dispatches were currently
// executing — that's how we observe concurrency from the test side.
class MockDispatcher : public IDispatcher {
 public:
  struct Call {
    std::string capability_id;
    int         concurrent_at_entry;   // value of in_flight at the moment we entered
    int         concurrent_at_exit;    // value of in_flight just before we returned
    std::chrono::milliseconds duration;
  };

  // Per-capability behaviour knobs.
  std::map<std::string, std::chrono::milliseconds> sleep_for;     // sleep inside dispatch
  std::map<std::string, ThreadingModel>            threading_for; // returned via plugin_threading
  std::map<std::string, std::string>               plugin_id_for; // returned via plugin_id
  std::map<std::string, bool>                      fail_for;      // make this capability fail

  std::vector<Call> calls;
  std::mutex        calls_mu;

  std::atomic<int>  in_flight{0};
  std::atomic<int>  peak_concurrency{0};

  std::string plugin_id(std::string_view cap) override {
    auto it = plugin_id_for.find(std::string(cap));
    if (it != plugin_id_for.end()) return it->second;
    return std::string(cap);
  }

  ThreadingModel plugin_threading(std::string_view cap) override {
    auto it = threading_for.find(std::string(cap));
    if (it != threading_for.end()) return it->second;
    return ThreadingModel::Reentrant;  // tests default to reentrant unless they say otherwise
  }

  DispatchResult dispatch(const DispatchContext& ctx) override {
    const int entered = in_flight.fetch_add(1) + 1;
    int peak = peak_concurrency.load();
    while (entered > peak && !peak_concurrency.compare_exchange_weak(peak, entered)) {}

    auto sleep_it = sleep_for.find(std::string(ctx.capability_id));
    auto duration = sleep_it != sleep_for.end() ? sleep_it->second : 10ms;
    std::this_thread::sleep_for(duration);

    Call c{std::string(ctx.capability_id), entered, in_flight.load(), duration};
    {
      std::scoped_lock lk(calls_mu);
      calls.push_back(std::move(c));
    }

    in_flight.fetch_sub(1);

    auto fail_it = fail_for.find(std::string(ctx.capability_id));
    if (fail_it != fail_for.end() && fail_it->second) {
      return DispatchError{"deliberate test failure for " + std::string(ctx.capability_id)};
    }
    return std::make_shared<std::string>(std::string(ctx.capability_id));
  }
};

Pipeline parse_or_die(std::string_view src) {
  auto r = parse_pipeline(src);
  if (auto* e = std::get_if<ParseError>(&r)) {
    ADD_FAILURE() << "parse failed: " << e->message;
    return {};
  }
  return std::get<Pipeline>(std::move(r));
}

// ---- Tests ---------------------------------------------------------------

TEST(ParallelRunner, IndependentStagesRunConcurrently) {
  // Three stages with no inter-dependencies. With max_workers=4 they all
  // start before any of them finishes, so peak_concurrency must be 3.
  auto p = parse_or_die(R"yaml(
version: 1
stages:
  - {id: a, plugin: cap.a}
  - {id: b, plugin: cap.b}
  - {id: c, plugin: cap.c}
)yaml");

  MockDispatcher d;
  d.sleep_for = {{"cap.a", 80ms}, {"cap.b", 80ms}, {"cap.c", 80ms}};
  Cache cache;
  RunOptions opts;
  opts.use_cache   = false;
  opts.max_workers = 4;

  auto r = run_pipeline_parallel(p, d, cache, opts);
  ASSERT_EQ(r.status, RunResult::Status::Success);
  EXPECT_EQ(r.stage_results.size(), 3u);
  EXPECT_EQ(d.peak_concurrency.load(), 3)
      << "expected all three independent stages to run in parallel; "
      << "peak observed = " << d.peak_concurrency.load();
}

TEST(ParallelRunner, DependentStagesStillSerialize) {
  // a → b → c chain. Even with workers, each must wait its turn.
  auto p = parse_or_die(R"yaml(
version: 1
stages:
  - {id: a, plugin: cap.a}
  - {id: b, plugin: cap.b, input: {prev: {from: a}}}
  - {id: c, plugin: cap.c, input: {prev: {from: b}}}
)yaml");

  MockDispatcher d;
  d.sleep_for = {{"cap.a", 30ms}, {"cap.b", 30ms}, {"cap.c", 30ms}};
  Cache cache;
  RunOptions opts;
  opts.use_cache   = false;
  opts.max_workers = 4;

  auto r = run_pipeline_parallel(p, d, cache, opts);
  ASSERT_EQ(r.status, RunResult::Status::Success);
  EXPECT_EQ(d.peak_concurrency.load(), 1)
      << "linear-dependency chain should never overlap; "
      << "peak observed = " << d.peak_concurrency.load();

  // stage_results comes back in declaration order regardless of completion order.
  ASSERT_EQ(r.stage_results.size(), 3u);
  EXPECT_EQ(r.stage_results[0].stage_id, "a");
  EXPECT_EQ(r.stage_results[1].stage_id, "b");
  EXPECT_EQ(r.stage_results[2].stage_id, "c");
}

TEST(ParallelRunner, SingleThreadedPluginSerializesAcrossStages) {
  // Two stages backed by capabilities from the SAME plugin id, which
  // declares SingleThreaded threading. They have no input dependency, so
  // a Reentrant plugin would overlap them — but SingleThreaded must not.
  auto p = parse_or_die(R"yaml(
version: 1
stages:
  - {id: a, plugin: cap.a}
  - {id: b, plugin: cap.b}
)yaml");

  MockDispatcher d;
  d.sleep_for     = {{"cap.a", 80ms}, {"cap.b", 80ms}};
  d.plugin_id_for = {{"cap.a", "shared-plugin"}, {"cap.b", "shared-plugin"}};
  d.threading_for = {{"cap.a", ThreadingModel::SingleThreaded},
                     {"cap.b", ThreadingModel::SingleThreaded}};
  Cache cache;
  RunOptions opts;
  opts.use_cache   = false;
  opts.max_workers = 4;

  auto r = run_pipeline_parallel(p, d, cache, opts);
  ASSERT_EQ(r.status, RunResult::Status::Success);
  EXPECT_EQ(d.peak_concurrency.load(), 1)
      << "two stages of a SingleThreaded plugin must serialize; "
      << "peak observed = " << d.peak_concurrency.load();
}

TEST(ParallelRunner, DifferentPluginsOverlapEvenIfBothSingleThreaded) {
  // Two stages from DIFFERENT plugins, each declaring SingleThreaded.
  // The reentrancy guard locks per-plugin, so they should still overlap.
  auto p = parse_or_die(R"yaml(
version: 1
stages:
  - {id: a, plugin: cap.a}
  - {id: b, plugin: cap.b}
)yaml");

  MockDispatcher d;
  d.sleep_for     = {{"cap.a", 80ms}, {"cap.b", 80ms}};
  d.plugin_id_for = {{"cap.a", "plugin-A"}, {"cap.b", "plugin-B"}};
  d.threading_for = {{"cap.a", ThreadingModel::SingleThreaded},
                     {"cap.b", ThreadingModel::SingleThreaded}};
  Cache cache;
  RunOptions opts;
  opts.use_cache   = false;
  opts.max_workers = 4;

  auto r = run_pipeline_parallel(p, d, cache, opts);
  ASSERT_EQ(r.status, RunResult::Status::Success);
  EXPECT_EQ(d.peak_concurrency.load(), 2)
      << "stages from different plugins must overlap even when both "
      << "declare single-threaded; peak observed = " << d.peak_concurrency.load();
}

TEST(ParallelRunner, StopOnFirstFailureMarksDownstreamSkipped) {
  // a fails; b depends on a (so it gets Skipped); c is independent and
  // may either complete (if it started before a's failure registered) OR
  // be skipped (if not yet picked up). We assert b is Skipped.
  auto p = parse_or_die(R"yaml(
version: 1
stages:
  - {id: a, plugin: cap.a}
  - {id: b, plugin: cap.b, input: {prev: {from: a}}}
  - {id: c, plugin: cap.c}
)yaml");

  MockDispatcher d;
  d.sleep_for = {{"cap.a", 10ms}, {"cap.b", 10ms}, {"cap.c", 10ms}};
  d.fail_for  = {{"cap.a", true}};
  Cache cache;
  RunOptions opts;
  opts.use_cache              = false;
  opts.stop_on_first_failure  = true;
  opts.max_workers            = 2;

  auto r = run_pipeline_parallel(p, d, cache, opts);
  EXPECT_EQ(r.status, RunResult::Status::StageFailed);
  ASSERT_EQ(r.stage_results.size(), 3u);

  // Stage results are emitted in declaration order.
  EXPECT_EQ(r.stage_results[0].stage_id, "a");
  EXPECT_EQ(r.stage_results[0].status,   StageRunResult::Status::Failed);

  EXPECT_EQ(r.stage_results[1].stage_id, "b");
  EXPECT_EQ(r.stage_results[1].status,   StageRunResult::Status::Skipped)
      << "b depends on the failed stage a — must be Skipped";
}

TEST(ParallelRunner, MaxWorkersOneStillProducesValidResult) {
  // Sanity: max_workers=1 dispatches into the parallel impl too (since
  // run_pipeline only branches on > 1, but the tests call the parallel
  // impl directly). This exercises the scheduler with a single worker.
  auto p = parse_or_die(R"yaml(
version: 1
stages:
  - {id: a, plugin: cap.a}
  - {id: b, plugin: cap.b, input: {prev: {from: a}}}
)yaml");

  MockDispatcher d;
  d.sleep_for = {{"cap.a", 5ms}, {"cap.b", 5ms}};
  Cache cache;
  RunOptions opts;
  opts.use_cache   = false;
  opts.max_workers = 1;

  auto r = run_pipeline_parallel(p, d, cache, opts);
  ASSERT_EQ(r.status, RunResult::Status::Success);
  EXPECT_EQ(d.peak_concurrency.load(), 1);
  EXPECT_EQ(r.stage_results.size(), 2u);
}

TEST(ParallelRunner, ReentrancyGuardIsNoOpForReentrant) {
  // Direct unit test of the guard: Reentrant returns an empty lock.
  ReentrancyGuard g;
  auto lk = g.acquire("p", ThreadingModel::Reentrant);
  EXPECT_FALSE(lk.owns_lock());
}

TEST(ParallelRunner, ReentrancyGuardSerializesSingleThreaded) {
  ReentrancyGuard g;
  auto lk1 = g.acquire("p", ThreadingModel::SingleThreaded);
  EXPECT_TRUE(lk1.owns_lock());

  // Try to acquire on another thread — it must block until lk1 is released.
  std::atomic<bool> second_acquired{false};
  std::thread t([&] {
    auto lk2 = g.acquire("p", ThreadingModel::SingleThreaded);
    second_acquired = true;
  });
  std::this_thread::sleep_for(50ms);
  EXPECT_FALSE(second_acquired.load())
      << "second SingleThreaded acquire on the same plugin should block";

  lk1.unlock();
  t.join();
  EXPECT_TRUE(second_acquired.load());
}

TEST(ParallelRunner, ValidationFailureReportedSameAsSequential) {
  // Missing-upstream-reference rejection at validation time, no dispatch.
  auto p = parse_or_die(R"yaml(
version: 1
stages:
  - {id: a, plugin: cap.a, input: {prev: {from: missing}}}
)yaml");

  MockDispatcher d;
  Cache cache;
  RunOptions opts;
  opts.use_cache   = false;
  opts.max_workers = 4;

  auto r = run_pipeline_parallel(p, d, cache, opts);
  EXPECT_EQ(r.status, RunResult::Status::ValidationFailed);
  EXPECT_FALSE(r.validation_errors.empty());

  std::scoped_lock lk(d.calls_mu);
  EXPECT_TRUE(d.calls.empty());
}

}  // namespace
