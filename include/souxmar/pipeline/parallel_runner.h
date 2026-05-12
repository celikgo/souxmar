// SPDX-License-Identifier: Apache-2.0
//
// Parallel pipeline runner — schedules independent DAG branches across a
// thread pool. Same Pipeline / IDispatcher / Cache / DiskCache contract as
// the sequential runner; the only knob is RunOptions::max_workers.
//
// `run_pipeline` (declared in runner.h) is the public entry point: it
// calls into this implementation when max_workers > 1 and falls back to
// the sequential path otherwise.
//
// The reentrancy guard below honors plugin manifest declarations:
//   * Reentrant         — capability calls overlap freely.
//   * SingleThreaded    — only one stage of the owning plugin runs at a time.
//   * InternalParallel  — same external constraint as SingleThreaded; the
//                         plugin may use its own threads internally.
//
// Lock granularity is per-plugin (not per-capability): a plugin declaring
// "single-threaded" promises serial dispatch across every capability it
// registers, which matches the manifest's threading model field.

#pragma once

#include "souxmar/pipeline/runner.h"
#include "souxmar/plugin/manifest.h"

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace souxmar::pipeline {

// ReentrancyGuard — per-plugin mutex pool. acquire() returns a unique_lock
// that owns the plugin's mutex iff the model requires serialization, and
// is empty (no-op) for Reentrant plugins.
//
// Thread-safe: the internal map is protected by its own mutex, so multiple
// workers can call acquire() concurrently without external synchronization.
class ReentrancyGuard {
 public:
  ReentrancyGuard() = default;
  ~ReentrancyGuard() = default;

  ReentrancyGuard(const ReentrancyGuard&) = delete;
  ReentrancyGuard& operator=(const ReentrancyGuard&) = delete;

  // Block until the caller may dispatch into the named plugin under the
  // given threading model. Returned lock holds the per-plugin mutex (for
  // SingleThreaded / InternalParallel) or is unlocked / empty (for
  // Reentrant). Drop the lock to release the plugin to the next worker.
  [[nodiscard]] std::unique_lock<std::mutex> acquire(std::string_view plugin_id,
                                                     plugin::ThreadingModel threading);

 private:
  std::mutex map_mu_;
  std::unordered_map<std::string, std::unique_ptr<std::mutex>> plugin_mu_;
};

// Parallel runner implementation. Public for unit tests; user code calls
// run_pipeline (in runner.h) which dispatches here when max_workers > 1.
[[nodiscard]] RunResult run_pipeline_parallel(const Pipeline& pipeline,
                                              IDispatcher& dispatcher,
                                              Cache& cache,
                                              const RunOptions& options);

}  // namespace souxmar::pipeline
