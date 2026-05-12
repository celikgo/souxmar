// SPDX-License-Identifier: Apache-2.0

#include "souxmar/pipeline/runner.h"

#include "souxmar/pipeline/dag.h"
#include "souxmar/pipeline/parallel_runner.h"

#include <fmt/core.h>

#include <algorithm>

namespace souxmar::pipeline {

RunResult run_pipeline(const Pipeline& pipeline,
                       IDispatcher& dispatcher,
                       Cache& cache,
                       const RunOptions& options) {
  // Dispatch into the parallel scheduler whenever the caller asked for
  // more than one worker. The parallel implementation handles cache,
  // disk_backing, and stop_on_first_failure with the same contract as
  // the sequential path below, so callers see a single API.
  if (options.max_workers > 1) {
    return run_pipeline_parallel(pipeline, dispatcher, cache, options);
  }

  RunResult result;

  // 1. Validate + topologically sort.
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

  const auto order = std::get<std::vector<std::size_t>>(std::move(dag));

  // Track each stage's content hash so downstream hashes can fold it in.
  std::vector<std::pair<std::string, ContentHash>> upstream_hashes;
  upstream_hashes.reserve(pipeline.stages.size());

  bool encountered_failure = false;

  for (const auto idx : order) {
    const auto& stage = pipeline.stages[idx];
    StageRunResult sr{stage.id, StageRunResult::Status::Executed, ContentHash{}, {}};

    if (encountered_failure && options.stop_on_first_failure) {
      sr.status = StageRunResult::Status::Skipped;
      result.stage_results.push_back(std::move(sr));
      continue;
    }

    // Build the cache-context string: capability id + plugin version.
    auto version = dispatcher.plugin_version(stage.plugin);
    auto context = version.empty() ? stage.plugin : fmt::format("{}@{}", stage.plugin, version);

    sr.content_hash = hash_inputs(context, stage.input, upstream_hashes);

    // Cache lookup — in-memory first, then opt-in disk cache.
    if (options.use_cache) {
      if (auto cached = cache.get(sr.content_hash)) {
        sr.status = StageRunResult::Status::Cached;
        result.outputs.emplace(stage.id, std::move(cached));
        upstream_hashes.emplace_back(stage.id, sr.content_hash);
        result.stage_results.push_back(std::move(sr));
        continue;
      }
      if (options.disk_backing && options.disk_backing->cache
          && options.disk_backing->deserialize) {
        if (auto blob = options.disk_backing->cache->get_bytes(sr.content_hash)) {
          if (auto rehydrated = options.disk_backing->deserialize(*blob); rehydrated) {
            // Re-warm the in-memory cache so subsequent stages and reruns
            // pay the disk-read cost only once per process.
            cache.put(sr.content_hash, rehydrated);
            sr.status = StageRunResult::Status::Cached;
            result.outputs.emplace(stage.id, std::move(rehydrated));
            upstream_hashes.emplace_back(stage.id, sr.content_hash);
            result.stage_results.push_back(std::move(sr));
            continue;
          }
        }
      }
    }

    // Dispatch.
    DispatchContext ctx{stage.plugin, stage.input, result.outputs};
    auto dr = dispatcher.dispatch(ctx);
    if (auto* err = std::get_if<DispatchError>(&dr)) {
      sr.status = StageRunResult::Status::Failed;
      sr.error = *err;
      encountered_failure = true;
    } else {
      auto payload = std::move(std::get<DispatchSuccess>(dr));
      if (options.use_cache && payload) {
        cache.put(sr.content_hash, payload);
        // Best-effort persist: if the dispatcher knows how to serialize this
        // payload type, write it through. A failed put is non-fatal — the
        // in-memory cache still holds the value for this run.
        if (options.disk_backing && options.disk_backing->cache
            && options.disk_backing->serialize) {
          if (auto blob = options.disk_backing->serialize(payload); blob) {
            (void)options.disk_backing->cache->put_bytes(sr.content_hash, *blob);
          }
        }
      }
      result.outputs.emplace(stage.id, std::move(payload));
    }

    upstream_hashes.emplace_back(stage.id, sr.content_hash);
    result.stage_results.push_back(std::move(sr));
  }

  result.status = encountered_failure ? RunResult::Status::StageFailed : RunResult::Status::Success;
  return result;
}

}  // namespace souxmar::pipeline
