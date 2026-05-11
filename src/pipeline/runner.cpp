// SPDX-License-Identifier: Apache-2.0

#include "souxmar/pipeline/runner.h"

#include <fmt/core.h>

#include <algorithm>

#include "souxmar/pipeline/dag.h"

namespace souxmar::pipeline {

RunResult run_pipeline(const Pipeline&    pipeline,
                       IDispatcher&       dispatcher,
                       Cache&             cache,
                       const RunOptions&  options) {
  RunResult result;

  // 1. Validate + topologically sort.
  auto dag = validate(pipeline);
  if (auto* errors = std::get_if<std::vector<DagError>>(&dag)) {
    result.status = RunResult::Status::ValidationFailed;
    result.validation_errors.reserve(errors->size());
    for (const auto& e : *errors) {
      result.validation_errors.push_back(
          e.stage_id.empty() ? e.message
                             : fmt::format("[{}] {}", e.stage_id, e.message));
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
    auto context = version.empty()
                       ? stage.plugin
                       : fmt::format("{}@{}", stage.plugin, version);

    sr.content_hash = hash_inputs(context, stage.input, upstream_hashes);

    // Cache lookup.
    if (options.use_cache) {
      if (auto cached = cache.get(sr.content_hash)) {
        sr.status = StageRunResult::Status::Cached;
        result.outputs.emplace(stage.id, std::move(cached));
        upstream_hashes.emplace_back(stage.id, sr.content_hash);
        result.stage_results.push_back(std::move(sr));
        continue;
      }
    }

    // Dispatch.
    DispatchContext ctx{stage.plugin, stage.input, result.outputs};
    auto dr = dispatcher.dispatch(ctx);
    if (auto* err = std::get_if<DispatchError>(&dr)) {
      sr.status = StageRunResult::Status::Failed;
      sr.error  = *err;
      encountered_failure = true;
    } else {
      auto payload = std::move(std::get<DispatchSuccess>(dr));
      // Cache the successful output.
      if (options.use_cache && payload) {
        cache.put(sr.content_hash, payload);
      }
      result.outputs.emplace(stage.id, std::move(payload));
    }

    upstream_hashes.emplace_back(stage.id, sr.content_hash);
    result.stage_results.push_back(std::move(sr));
  }

  result.status = encountered_failure ? RunResult::Status::StageFailed
                                       : RunResult::Status::Success;
  return result;
}

}  // namespace souxmar::pipeline
