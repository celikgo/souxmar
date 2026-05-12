// SPDX-License-Identifier: Apache-2.0
//
// DAG validator + topological sort.
//
// A pipeline is well-formed iff:
//   1. Every StageRef references a stage that exists.
//   2. The directed graph induced by StageRef edges is acyclic.
//   3. Every stage id is unique (already enforced by the parser).
//
// `validate` returns a list of all errors found (not just the first), so
// authors get to fix multiple problems per round-trip.

#pragma once

#include "souxmar/pipeline/pipeline.h"

#include <string>
#include <variant>
#include <vector>

namespace souxmar::pipeline {

struct DagError {
  std::string message;
  std::string stage_id;  // empty if the error is pipeline-level
};

// On success returns the topological order of stage indices into
// pipeline.stages. On failure returns the (non-empty) error list.
using DagResult = std::variant<std::vector<std::size_t>, std::vector<DagError>>;

[[nodiscard]] DagResult validate(const Pipeline& pipeline);

// Helper exposed for tests / tooling. Returns the set of stage ids referenced
// (transitively) by `value` via StageRef.
[[nodiscard]] std::vector<std::string> collect_stage_refs(const Value& value);

}  // namespace souxmar::pipeline
