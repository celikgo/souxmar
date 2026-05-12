// SPDX-License-Identifier: Apache-2.0
//
// Parallel pipeline runner — schedules independent DAG branches across a
// thread pool. Public entry point is run_pipeline (in runner.h); this file
// provides the implementation that runs when RunOptions::max_workers > 1.

#include "souxmar/pipeline/parallel_runner.h"

#include "souxmar/pipeline/dag.h"

#include <fmt/core.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <exception>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace souxmar::pipeline {

// ============================================================================
// ReentrancyGuard
// ============================================================================

std::unique_lock<std::mutex> ReentrancyGuard::acquire(std::string_view plugin_id,
                                                      plugin::ThreadingModel threading) {
  // Reentrant plugins do not need any lock — the runner can call them
  // concurrently as many times as it has workers.
  if (threading == plugin::ThreadingModel::Reentrant) {
    return std::unique_lock<std::mutex>{};
  }

  // SingleThreaded and InternalParallel both serialize external calls; the
  // only difference is internal — InternalParallel may use threads inside
  // the plugin call. From the runner's side they look identical.
  std::mutex* per_plugin_mu = nullptr;
  {
    std::scoped_lock map_lock(map_mu_);
    auto& slot = plugin_mu_[std::string(plugin_id)];
    if (!slot)
      slot = std::make_unique<std::mutex>();
    per_plugin_mu = slot.get();
  }
  return std::unique_lock<std::mutex>(*per_plugin_mu);
}

// ============================================================================
// Helpers — DAG bookkeeping
// ============================================================================

namespace {

// Walk a Value tree collecting every distinct StageRef target id.
void collect_stage_refs(const Value& v, std::unordered_set<std::string>& out) {
  switch (v.kind()) {
    case Value::Kind::Stage:
      out.insert(v.as_stage().stage_id);
      return;
    case Value::Kind::List:
      for (const auto& item : v.as_list())
        collect_stage_refs(item, out);
      return;
    case Value::Kind::Map:
      for (const auto& [_, child] : v.as_map())
        collect_stage_refs(child, out);
      return;
    default:
      return;
  }
}

// ============================================================================
// Shared scheduler state
// ============================================================================

struct ParallelState {
  // Immutable post-setup ----------------------------------------------------
  const Pipeline* pipeline = nullptr;
  IDispatcher* dispatcher = nullptr;
  Cache* cache = nullptr;
  const RunOptions* options = nullptr;
  std::vector<std::vector<std::string>> upstream_ids;  // direct upstream stage ids per stage
  std::vector<std::vector<std::size_t>> dependents;    // downstream stage indices per stage

  // Mutable shared state — protected by mu --------------------------------
  std::mutex mu;
  std::condition_variable cv;
  std::deque<std::size_t> ready;                       // FIFO of stages whose in-degree hit 0
  std::vector<std::size_t> in_degree;                  // remaining upstream count per stage
  std::vector<std::optional<StageRunResult>> results;  // by stage index; nullopt = not started
  std::vector<ContentHash> stage_hashes;  // by stage index; written before in-degree decrement
  std::map<std::string, std::shared_ptr<void>>
      outputs;  // by stage id (for downstream upstream_outputs ctx)
  std::size_t completed_or_skipped = 0;
  std::size_t in_flight = 0;
  bool stop_requested = false;

  // Reentrancy guard is its own thread-safe object; lives outside `mu`.
  ReentrancyGuard guard;
};

// Build the per-stage upstream hash list from the indices already populated
// in state.stage_hashes. Reads stage_hashes without locking — see invariant
// note in the worker loop below.
std::vector<std::pair<std::string, ContentHash>> upstream_hashes_for(
    const ParallelState& state,
    const std::vector<std::string>& upstream_ids,
    const std::unordered_map<std::string, std::size_t>& id_to_index) {
  std::vector<std::pair<std::string, ContentHash>> out;
  out.reserve(upstream_ids.size());
  for (const auto& id : upstream_ids) {
    auto it = id_to_index.find(id);
    if (it == id_to_index.end())
      continue;
    out.emplace_back(id, state.stage_hashes[it->second]);
  }
  return out;
}

// One worker iteration on a known-ready stage. Mutates shared state at the
// end (results, outputs, in_degree, ready, completed_or_skipped, cv).
void process_stage(ParallelState& state,
                   std::size_t idx,
                   const std::unordered_map<std::string, std::size_t>& id_to_index) {
  const Stage& stage = state.pipeline->stages[idx];
  StageRunResult sr{stage.id, StageRunResult::Status::Executed, ContentHash{}, {}};

  // Build cache-context string + hash of this stage's inputs. The
  // upstream_hashes vector is read from state.stage_hashes, where each
  // upstream's hash was written BEFORE its in-degree decrement, so by the
  // time we reach this stage every upstream we'll consult is visible.
  auto version = state.dispatcher->plugin_version(stage.plugin);
  auto context = version.empty() ? stage.plugin : fmt::format("{}@{}", stage.plugin, version);
  const auto upstream_hashes = upstream_hashes_for(state, state.upstream_ids[idx], id_to_index);
  sr.content_hash = hash_inputs(context, stage.input, upstream_hashes);

  // Cache lookup — in-memory first, then opt-in disk cache. Cache itself
  // is internally thread-safe; we don't need to hold state.mu.
  if (state.options->use_cache) {
    if (auto cached = state.cache->get(sr.content_hash); cached) {
      sr.status = StageRunResult::Status::Cached;
      // Publish + decrement dependents under state.mu.
      std::scoped_lock lk(state.mu);
      state.stage_hashes[idx] = sr.content_hash;
      state.outputs.emplace(stage.id, std::move(cached));
      state.results[idx] = std::move(sr);
      state.completed_or_skipped += 1;
      state.in_flight -= 1;
      for (auto dep : state.dependents[idx]) {
        if (--state.in_degree[dep] == 0)
          state.ready.push_back(dep);
      }
      state.cv.notify_all();
      return;
    }
    if (state.options->disk_backing && state.options->disk_backing->cache
        && state.options->disk_backing->deserialize) {
      if (auto blob = state.options->disk_backing->cache->get_bytes(sr.content_hash)) {
        if (auto rehydrated = state.options->disk_backing->deserialize(*blob); rehydrated) {
          state.cache->put(sr.content_hash, rehydrated);
          sr.status = StageRunResult::Status::Cached;
          std::scoped_lock lk(state.mu);
          state.stage_hashes[idx] = sr.content_hash;
          state.outputs.emplace(stage.id, std::move(rehydrated));
          state.results[idx] = std::move(sr);
          state.completed_or_skipped += 1;
          state.in_flight -= 1;
          for (auto dep : state.dependents[idx]) {
            if (--state.in_degree[dep] == 0)
              state.ready.push_back(dep);
          }
          state.cv.notify_all();
          return;
        }
      }
    }
  }

  // Snapshot the upstream outputs map under the lock — DispatchContext
  // wants a const reference, and while the underlying map could be
  // appended to by other workers concurrently, the snapshot is what this
  // stage saw at dispatch time. We take a copy: maps of shared_ptr are
  // cheap relative to the stage cost.
  std::map<std::string, std::shared_ptr<void>> upstream_snapshot;
  {
    std::scoped_lock lk(state.mu);
    upstream_snapshot = state.outputs;
  }

  // Reentrancy guard: serialize per-plugin if the manifest declared so.
  const auto plugin_id = state.dispatcher->plugin_id(stage.plugin);
  const auto threading = state.dispatcher->plugin_threading(stage.plugin);
  auto plugin_lock = state.guard.acquire(plugin_id, threading);

  // Dispatch.
  DispatchContext ctx{stage.plugin, stage.input, upstream_snapshot};
  DispatchResult dr;
  try {
    dr = state.dispatcher->dispatch(ctx);
  } catch (const std::exception& e) {
    dr = DispatchError{fmt::format("dispatcher threw: {}", e.what())};
  } catch (...) {
    dr = DispatchError{"dispatcher threw an unknown exception"};
  }
  plugin_lock = std::unique_lock<std::mutex>{};  // release before taking state.mu

  if (auto* err = std::get_if<DispatchError>(&dr)) {
    sr.status = StageRunResult::Status::Failed;
    sr.error = *err;
    std::scoped_lock lk(state.mu);
    state.stage_hashes[idx] = sr.content_hash;
    state.results[idx] = std::move(sr);
    state.completed_or_skipped += 1;
    state.in_flight -= 1;
    if (state.options->stop_on_first_failure) {
      state.stop_requested = true;
    }
    // Decrement dependents' in-degree even on failure — they would
    // otherwise stay in_flight=0 forever; the runner marks them Skipped at
    // the post-loop sweep.
    for (auto dep : state.dependents[idx]) {
      if (--state.in_degree[dep] == 0)
        state.ready.push_back(dep);
    }
    state.cv.notify_all();
    return;
  }

  auto payload = std::move(std::get<DispatchSuccess>(dr));
  if (state.options->use_cache && payload) {
    state.cache->put(sr.content_hash, payload);
    if (state.options->disk_backing && state.options->disk_backing->cache
        && state.options->disk_backing->serialize) {
      if (auto blob = state.options->disk_backing->serialize(payload); blob) {
        (void)state.options->disk_backing->cache->put_bytes(sr.content_hash, *blob);
      }
    }
  }

  std::scoped_lock lk(state.mu);
  state.stage_hashes[idx] = sr.content_hash;
  state.outputs.emplace(stage.id, std::move(payload));
  state.results[idx] = std::move(sr);
  state.completed_or_skipped += 1;
  state.in_flight -= 1;
  for (auto dep : state.dependents[idx]) {
    if (--state.in_degree[dep] == 0)
      state.ready.push_back(dep);
  }
  state.cv.notify_all();
}

}  // namespace

// ============================================================================
// Public entry point
// ============================================================================

RunResult run_pipeline_parallel(const Pipeline& pipeline,
                                IDispatcher& dispatcher,
                                Cache& cache,
                                const RunOptions& options) {
  RunResult result;

  // 1. Validate + topo-sort. Same contract + error shape as sequential.
  auto dag = validate(pipeline);
  if (auto* errors = std::get_if<std::vector<DagError>>(&dag)) {
    result.status = RunResult::Status::ValidationFailed;
    result.validation_errors.reserve(errors->size());
    for (const auto& e : *errors) {
      result.validation_errors.push_back(
          e.stage_id.empty() ? e.message : fmt::format("[{}] {}", e.stage_id, e.message));
    }
    return result;
  }
  // The topological order is informational here — the parallel scheduler
  // walks the DAG by in-degree, not by linear topo position. We still
  // honor declaration order in stage_results by pre-allocating slots.
  (void)std::get<std::vector<std::size_t>>(dag);

  const std::size_t n = pipeline.stages.size();
  if (n == 0) {
    result.status = RunResult::Status::Success;
    return result;
  }

  // 2. Build id→index, upstream-id sets, dependents, in_degree.
  std::unordered_map<std::string, std::size_t> id_to_index;
  id_to_index.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    id_to_index.emplace(pipeline.stages[i].id, i);
  }

  ParallelState state;
  state.pipeline = &pipeline;
  state.dispatcher = &dispatcher;
  state.cache = &cache;
  state.options = &options;
  state.upstream_ids.assign(n, {});
  state.dependents.assign(n, {});
  state.in_degree.assign(n, 0);
  state.results.assign(n, std::nullopt);
  state.stage_hashes.assign(n, ContentHash{});

  for (std::size_t i = 0; i < n; ++i) {
    std::unordered_set<std::string> refs;
    collect_stage_refs(pipeline.stages[i].input, refs);
    state.upstream_ids[i].reserve(refs.size());
    for (const auto& ref : refs) {
      auto it = id_to_index.find(ref);
      if (it == id_to_index.end())
        continue;  // dag.cpp validation already caught these
      state.upstream_ids[i].push_back(ref);
      state.dependents[it->second].push_back(i);
      state.in_degree[i] += 1;
    }
  }

  // 3. Seed the ready queue with stages of zero in-degree.
  for (std::size_t i = 0; i < n; ++i) {
    if (state.in_degree[i] == 0)
      state.ready.push_back(i);
  }

  // 4. Spawn worker threads.
  const std::size_t hardware = std::max<std::size_t>(1u, std::thread::hardware_concurrency());
  const std::size_t requested = options.max_workers > 0 ? options.max_workers : hardware;
  const std::size_t workers = std::min(requested, n);

  std::vector<std::thread> pool;
  pool.reserve(workers);
  for (std::size_t w = 0; w < workers; ++w) {
    pool.emplace_back([&state, &id_to_index] {
      while (true) {
        std::size_t idx = 0;
        {
          std::unique_lock lk(state.mu);
          state.cv.wait(lk, [&] {
            return !state.ready.empty() || state.completed_or_skipped == state.results.size()
                   || (state.stop_requested && state.in_flight == 0);
          });
          // Termination conditions: all done, OR stop is requested AND no
          // worker is currently running a stage (so picking new work would
          // be wrong).
          if (state.completed_or_skipped == state.results.size())
            return;
          if (state.stop_requested && state.in_flight == 0)
            return;
          if (state.ready.empty())
            continue;
          if (state.stop_requested) {
            // Stop requested while we still have work in flight: drain the
            // ready queue as Skipped instead of dispatching.
            idx = state.ready.front();
            state.ready.pop_front();
            state.results[idx] = StageRunResult{state.pipeline->stages[idx].id,
                                                StageRunResult::Status::Skipped,
                                                ContentHash{},
                                                std::nullopt};
            state.completed_or_skipped += 1;
            // No upstream dispatch happened, but downstream in-degree
            // still needs decrementing so we eventually terminate.
            for (auto dep : state.dependents[idx]) {
              if (--state.in_degree[dep] == 0)
                state.ready.push_back(dep);
            }
            state.cv.notify_all();
            continue;
          }
          idx = state.ready.front();
          state.ready.pop_front();
          state.in_flight += 1;
        }
        process_stage(state, idx, id_to_index);
      }
    });
  }
  for (auto& t : pool)
    t.join();

  // 5. Sweep: any stage that never started (because it was downstream of a
  //    failure that triggered stop_on_first_failure) gets a Skipped marker.
  for (std::size_t i = 0; i < n; ++i) {
    if (!state.results[i]) {
      state.results[i] = StageRunResult{
          pipeline.stages[i].id, StageRunResult::Status::Skipped, ContentHash{}, std::nullopt};
    }
  }

  // 6. Pack into the public RunResult. Stage results are emitted in
  //    declaration order — same as the sequential runner produces for the
  //    common 1-thread case, so downstream consumers see a stable shape.
  bool encountered_failure = false;
  result.stage_results.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    if (state.results[i]->status == StageRunResult::Status::Failed) {
      encountered_failure = true;
    }
    result.stage_results.push_back(std::move(*state.results[i]));
  }
  result.outputs = std::move(state.outputs);
  result.status = encountered_failure ? RunResult::Status::StageFailed : RunResult::Status::Success;
  return result;
}

}  // namespace souxmar::pipeline
