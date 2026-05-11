// SPDX-License-Identifier: Apache-2.0

#include "souxmar/pipeline/dag.h"

#include <fmt/core.h>

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace souxmar::pipeline {

namespace {

void collect_into(const Value& v, std::vector<std::string>& out) {
  switch (v.kind()) {
    case Value::Kind::Stage:
      out.push_back(v.as_stage().stage_id);
      break;
    case Value::Kind::List:
      for (const auto& item : v.as_list()) collect_into(item, out);
      break;
    case Value::Kind::Map:
      for (const auto& [_, child] : v.as_map()) collect_into(child, out);
      break;
    default:
      break;
  }
}

}  // namespace

std::vector<std::string> collect_stage_refs(const Value& value) {
  std::vector<std::string> out;
  collect_into(value, out);
  return out;
}

DagResult validate(const Pipeline& pipeline) {
  std::vector<DagError> errors;

  // Index stage ids -> position. Duplicates already rejected by the parser,
  // but we re-check here so validate() is callable on a hand-built Pipeline.
  std::unordered_map<std::string, std::size_t> id_to_index;
  for (std::size_t i = 0; i < pipeline.stages.size(); ++i) {
    const auto& s = pipeline.stages[i];
    if (s.id.empty()) {
      errors.push_back({"stage id must be non-empty", ""});
      continue;
    }
    auto [it, inserted] = id_to_index.emplace(s.id, i);
    if (!inserted) {
      errors.push_back({fmt::format("duplicate stage id '{}'", s.id), s.id});
    }
  }

  // Build adjacency: edge from referenced stage -> referring stage
  // (because we want topological sort to schedule referenced stages first).
  std::vector<std::vector<std::size_t>> children(pipeline.stages.size());
  std::vector<std::size_t>              in_degree(pipeline.stages.size(), 0);

  for (std::size_t i = 0; i < pipeline.stages.size(); ++i) {
    const auto& s = pipeline.stages[i];
    const auto refs = collect_stage_refs(s.input);
    std::unordered_set<std::string> uniq_refs(refs.begin(), refs.end());
    for (const auto& ref_id : uniq_refs) {
      if (ref_id == s.id) {
        errors.push_back({fmt::format(
            "stage '{}' references itself via `from: {}`", s.id, s.id), s.id});
        continue;
      }
      auto it = id_to_index.find(ref_id);
      if (it == id_to_index.end()) {
        errors.push_back({fmt::format(
            "stage '{}' references unknown stage '{}'", s.id, ref_id), s.id});
        continue;
      }
      const auto producer = it->second;
      children[producer].push_back(i);
      ++in_degree[i];
    }
  }

  if (!errors.empty()) {
    return errors;
  }

  // Kahn's algorithm topological sort.
  std::vector<std::size_t> order;
  order.reserve(pipeline.stages.size());

  std::vector<std::size_t> ready;
  for (std::size_t i = 0; i < pipeline.stages.size(); ++i) {
    if (in_degree[i] == 0) ready.push_back(i);
  }
  // For determinism, process ready stages in original-declaration order.
  std::sort(ready.begin(), ready.end());

  while (!ready.empty()) {
    const auto node = ready.front();
    ready.erase(ready.begin());
    order.push_back(node);
    for (const auto child : children[node]) {
      if (--in_degree[child] == 0) {
        // Insert child in sorted position so the resulting order is stable.
        auto pos = std::lower_bound(ready.begin(), ready.end(), child);
        ready.insert(pos, child);
      }
    }
  }

  if (order.size() != pipeline.stages.size()) {
    // Cycle exists. Identify the stages still with non-zero in-degree.
    std::vector<std::string> cycle_members;
    for (std::size_t i = 0; i < pipeline.stages.size(); ++i) {
      if (in_degree[i] > 0) cycle_members.push_back(pipeline.stages[i].id);
    }
    errors.push_back({fmt::format(
        "pipeline contains a cycle involving stages: {}",
        fmt::join(cycle_members, ", ")), ""});
    return errors;
  }

  return order;
}

}  // namespace souxmar::pipeline
