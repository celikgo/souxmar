// SPDX-License-Identifier: Apache-2.0
//
// Pipeline runner — sequential execution against a pluggable IDispatcher.
//
// The runner is the orchestrator's heart but knows nothing about plugins,
// the C ABI, or specific capability vtables. All of that is behind
// IDispatcher. Sprint 3 push 1 ships the runner with a mock dispatcher
// for tests; Sprint 3 push 2 ships the real RegistryDispatcher that goes
// through the C ABI to loaded plugins.

#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "souxmar/pipeline/cache.h"
#include "souxmar/pipeline/pipeline.h"
#include "souxmar/pipeline/value.h"

namespace souxmar::pipeline {

struct DispatchError {
  std::string message;
};

// What an IDispatcher receives for a single stage.
struct DispatchContext {
  std::string_view                                              capability_id;
  const Value&                                                  inputs;          // the Stage's input tree (with StageRefs intact)
  const std::map<std::string, std::shared_ptr<void>>&           upstream_outputs; // by upstream stage id
};

// What an IDispatcher returns. shared_ptr<void> with appropriate deleter is
// the universal payload type — it can wrap a Mesh, a Geometry, a Field, a
// file path, anything. Type-routing happens inside the dispatcher based on
// the capability namespace.
using DispatchSuccess = std::shared_ptr<void>;
using DispatchResult  = std::variant<DispatchSuccess, DispatchError>;

class IDispatcher {
 public:
  virtual ~IDispatcher() = default;
  virtual DispatchResult dispatch(const DispatchContext& ctx) = 0;

  // Optional: provide a "plugin version" string used in cache-key context.
  // Default is empty (cache will treat "unknown version" as a single bucket).
  virtual std::string plugin_version(std::string_view /*capability_id*/) {
    return {};
  }
};

// Per-stage outcome from a run.
struct StageRunResult {
  enum class Status { Cached, Executed, Failed, Skipped };

  std::string                       stage_id;
  Status                            status;
  ContentHash                       content_hash;
  std::optional<DispatchError>      error;
};

// Aggregate run result.
struct RunResult {
  enum class Status { Success, ValidationFailed, StageFailed };

  Status                                          status;
  std::vector<std::string>                        validation_errors;  // populated when status == ValidationFailed
  std::vector<StageRunResult>                     stage_results;
  std::map<std::string, std::shared_ptr<void>>    outputs;            // by stage id (executed and cached stages)
};

// Run options.
struct RunOptions {
  bool   use_cache    = true;
  bool   stop_on_first_failure = true;
};

// Sequential runner. Walks pipeline.stages in topological order, dispatching
// each through the supplied IDispatcher and threading outputs into downstream
// stages.
[[nodiscard]] RunResult
run_pipeline(const Pipeline&    pipeline,
             IDispatcher&       dispatcher,
             Cache&             cache,
             const RunOptions&  options = {});

}  // namespace souxmar::pipeline
