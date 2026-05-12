// SPDX-License-Identifier: Apache-2.0
//
// Pipeline — the in-memory representation of a parsed `*.souxmar.yaml`.
//
// Each Stage is a typed call into the plugin registry plus its inputs.
// Cross-stage wiring happens through StageRef nodes inside the inputs tree
// (see value.h). Pipeline itself is immutable post-parse; the parser, DAG
// validator, runner, and cache all consume it as a const reference.

#pragma once

#include "souxmar/pipeline/value.h"

#include <cstdint>
#include <string>
#include <vector>

namespace souxmar::pipeline {

struct Stage {
  std::string id;      // unique within the pipeline; used by StageRef
  std::string plugin;  // capability id, e.g. "mesher.tetra.native"
  Value input;         // a Map; per-plugin contract decides keys
};

struct Pipeline {
  std::int32_t version = 1;
  std::vector<Stage> stages;
};

}  // namespace souxmar::pipeline
