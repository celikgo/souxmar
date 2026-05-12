// SPDX-License-Identifier: Apache-2.0
//
// Tool: apply_pipeline_diff
//
// Apply a structured list of edits to a pipeline draft. The tool walks
// the `ops` array in order and produces a new pipeline spec; the spec
// is then re-emitted as YAML and round-tripped through the parser, so
// the returned pipeline is guaranteed to load at `souxmar run` time.
//
// Like `propose_pipeline`, this is read-only — it produces a draft.
// Committing the draft to disk is the matching `write_pipeline` tool
// (future, gated ConfirmAlways). Confirmation::ConfirmOnce here keeps
// iterative editing fluid without bypassing user awareness.
//
// v1 op vocabulary:
//   { op: 'add',       stage: {id, plugin, input?}, after?: <id>, before?: <id> }
//   { op: 'remove',    id: <id> }
//   { op: 'set_input', id: <id>, key: <string>, value: <Value> }
//   { op: 'replace',   id: <id>, stage: {id, plugin, input?} }
//
// If a `from: <id>` reference becomes dangling after the diff, the
// re-parse step trips a `dangling reference` validation error and the
// tool returns INVALID_ARGUMENT — the agent then knows to fix the diff
// (typically by re-adding the missing upstream first).

#include "souxmar/ai/tool.h"
#include "souxmar/pipeline/parser.h"
#include "souxmar/pipeline/pipeline.h"
#include "souxmar/pipeline/value.h"

#include <algorithm>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace souxmar::ai {

namespace {

namespace pl = souxmar::pipeline;

// Read base.stages as a vector<Value::Map>. Caller has already
// verified `base` is a Map containing a List `stages`.
std::vector<pl::Value> stages_of(const pl::Value& base) {
  std::vector<pl::Value> out;
  if (const auto* s = base.find("stages"); s && s->kind() == pl::Value::Kind::List) {
    for (const auto& it : s->as_list())
      out.push_back(it);
  }
  return out;
}

std::string_view stage_id_view(const pl::Value& stage) {
  const auto* id = stage.find("id");
  return (id && id->kind() == pl::Value::Kind::String) ? id->as_string() : std::string_view{};
}

std::size_t index_of_stage(const std::vector<pl::Value>& stages, std::string_view id) {
  for (std::size_t i = 0; i < stages.size(); ++i) {
    if (stage_id_view(stages[i]) == id)
      return i;
  }
  return stages.size();  // not-found sentinel
}

pl::Value rebuild_pipeline(const pl::Value& base, std::vector<pl::Value> stages) {
  std::map<std::string, pl::Value> top;
  for (const auto& [k, v] : base.as_map()) {
    if (k == "stages")
      continue;
    top.emplace(k, v);
  }
  top.emplace("stages", pl::Value::list(std::move(stages)));
  return pl::Value::map(std::move(top));
}

// Apply one op. Returns true on success; on failure populates `err`.
bool apply_one(std::vector<pl::Value>& stages, const pl::Value& op, std::string& err) {
  if (op.kind() != pl::Value::Kind::Map) {
    err = "op must be a map";
    return false;
  }
  const auto* kind = op.find("op");
  if (!kind || kind->kind() != pl::Value::Kind::String) {
    err = "op missing required string `op`";
    return false;
  }
  const std::string kind_str(kind->as_string());

  if (kind_str == "add") {
    const auto* stage = op.find("stage");
    if (!stage || stage->kind() != pl::Value::Kind::Map) {
      err = "add op requires map `stage`";
      return false;
    }
    const auto id = stage_id_view(*stage);
    if (id.empty()) {
      err = "add op `stage` must carry a string `id`";
      return false;
    }
    if (index_of_stage(stages, id) != stages.size()) {
      err = "add op: stage '" + std::string(id) + "' already exists";
      return false;
    }
    // Determine insertion index — default end-of-list.
    std::size_t insert_at = stages.size();
    if (const auto* after = op.find("after");
        after != nullptr && after->kind() == pl::Value::Kind::String) {
      const auto j = index_of_stage(stages, after->as_string());
      if (j == stages.size()) {
        err = "add op: `after` references unknown stage '" + std::string(after->as_string()) + "'";
        return false;
      }
      insert_at = j + 1;
    } else if (const auto* before = op.find("before");
               before != nullptr && before->kind() == pl::Value::Kind::String) {
      const auto j = index_of_stage(stages, before->as_string());
      if (j == stages.size()) {
        err =
            "add op: `before` references unknown stage '" + std::string(before->as_string()) + "'";
        return false;
      }
      insert_at = j;
    }
    stages.insert(stages.begin() + static_cast<std::ptrdiff_t>(insert_at), *stage);
    return true;
  }

  if (kind_str == "remove") {
    const auto* idp = op.find("id");
    if (!idp || idp->kind() != pl::Value::Kind::String) {
      err = "remove op requires string `id`";
      return false;
    }
    const auto j = index_of_stage(stages, idp->as_string());
    if (j == stages.size()) {
      err = "remove op: unknown stage '" + std::string(idp->as_string()) + "'";
      return false;
    }
    stages.erase(stages.begin() + static_cast<std::ptrdiff_t>(j));
    return true;
  }

  if (kind_str == "set_input") {
    const auto* idp = op.find("id");
    const auto* keyp = op.find("key");
    const auto* valp = op.find("value");
    if (!idp || idp->kind() != pl::Value::Kind::String || !keyp
        || keyp->kind() != pl::Value::Kind::String || !valp) {
      err = "set_input op requires string `id`, string `key`, and any `value`";
      return false;
    }
    const auto j = index_of_stage(stages, idp->as_string());
    if (j == stages.size()) {
      err = "set_input op: unknown stage '" + std::string(idp->as_string()) + "'";
      return false;
    }
    std::map<std::string, pl::Value> stage_map;
    for (const auto& [k, v] : stages[j].as_map())
      stage_map.emplace(k, v);

    std::map<std::string, pl::Value> input_map;
    if (auto it = stage_map.find("input");
        it != stage_map.end() && it->second.kind() == pl::Value::Kind::Map) {
      for (const auto& [k, v] : it->second.as_map())
        input_map.emplace(k, v);
    }
    input_map[std::string(keyp->as_string())] = *valp;

    stage_map["input"] = pl::Value::map(std::move(input_map));
    stages[j] = pl::Value::map(std::move(stage_map));
    return true;
  }

  if (kind_str == "replace") {
    const auto* idp = op.find("id");
    const auto* stage = op.find("stage");
    if (!idp || idp->kind() != pl::Value::Kind::String || !stage
        || stage->kind() != pl::Value::Kind::Map) {
      err = "replace op requires string `id` and map `stage`";
      return false;
    }
    const auto j = index_of_stage(stages, idp->as_string());
    if (j == stages.size()) {
      err = "replace op: unknown stage '" + std::string(idp->as_string()) + "'";
      return false;
    }
    stages[j] = *stage;
    return true;
  }

  err = "unknown op kind '" + kind_str + "' (expected add / remove / set_input / replace)";
  return false;
}

}  // namespace

Tool make_apply_pipeline_diff_tool() {
  Tool t;
  t.name = "apply_pipeline_diff";
  t.description =
      "Apply a list of structured edits (add / remove / set_input / "
      "replace) to a pipeline draft. The result is re-emitted as YAML "
      "and round-tripped through the parser, so the returned pipeline "
      "is guaranteed to load at `souxmar run` time. Read-only: produces "
      "a draft, never writes to disk.";
  t.category = "Pipeline";
  t.confirmation = Confirmation::ConfirmOnce;
  t.input_schema_doc =
      "{base: <pipeline-spec>,                                       # the same shape "
      "propose_pipeline takes\n"
      " ops:  [\n"
      "   {op: 'add',       stage: {id, plugin, input?}, after?: <id>, before?: <id>},\n"
      "   {op: 'remove',    id: <id>},\n"
      "   {op: 'set_input', id: <id>, key: <string>, value: <Value>},\n"
      "   {op: 'replace',   id: <id>, stage: {id, plugin, input?}},\n"
      "   ...\n"
      " ]}";
  t.output_schema_doc =
      "{yaml: string,\n"
      " parsed_stages: number,\n"
      " ops_applied: number}";
  t.handler = [](const pl::Value& inputs, ToolContext& /*ctx*/) -> ToolResult {
    if (inputs.kind() != pl::Value::Kind::Map) {
      return ToolResult{
          pl::Value::null_value(),
          "input must be a map",
          ToolError{"INVALID_ARGUMENT", "apply_pipeline_diff input must be {base, ops: [...]}"}};
    }
    const auto* base = inputs.find("base");
    const auto* ops = inputs.find("ops");
    if (!base || base->kind() != pl::Value::Kind::Map) {
      return ToolResult{
          pl::Value::null_value(),
          "missing or wrong-typed `base`",
          ToolError{"INVALID_ARGUMENT", "apply_pipeline_diff requires a map `base` pipeline spec"}};
    }
    if (!ops || ops->kind() != pl::Value::Kind::List) {
      return ToolResult{pl::Value::null_value(),
                        "missing or wrong-typed `ops`",
                        ToolError{"INVALID_ARGUMENT",
                                  "apply_pipeline_diff requires a list `ops` of edit operations"}};
    }

    auto stages = stages_of(*base);
    std::size_t applied = 0;
    for (const auto& op : ops->as_list()) {
      std::string op_err;
      if (!apply_one(stages, op, op_err)) {
        return ToolResult{pl::Value::null_value(),
                          "diff failed at op " + std::to_string(applied) + ": " + op_err,
                          ToolError{"INVALID_ARGUMENT",
                                    op_err,
                                    "fix the op and retry; the pipeline is unchanged on failure"}};
      }
      ++applied;
    }

    auto new_pipeline = rebuild_pipeline(*base, std::move(stages));
    const auto yaml = pl::emit_value_yaml(new_pipeline);

    auto parse_result = pl::parse_pipeline(yaml);
    if (auto* err = std::get_if<pl::ParseError>(&parse_result)) {
      std::ostringstream msg;
      msg << err->message;
      if (err->line)
        msg << " (line " << *err->line << ")";
      if (err->column)
        msg << ", column " << *err->column;
      return ToolResult{pl::Value::null_value(),
                        "diffed pipeline failed validation",
                        ToolError{"INVALID_ARGUMENT",
                                  msg.str(),
                                  "the ops applied cleanly but the result no longer parses as a "
                                  "valid pipeline — typically a dangling {from: <id>} reference "
                                  "after a remove. Re-add the missing upstream and retry."}};
    }
    const auto& parsed = std::get<pl::Pipeline>(parse_result);

    std::map<std::string, pl::Value> out;
    out.emplace("yaml", pl::Value::string(yaml));
    out.emplace("parsed_stages", pl::Value::number(static_cast<double>(parsed.stages.size())));
    out.emplace("version", pl::Value::number(static_cast<double>(parsed.version)));
    out.emplace("ops_applied", pl::Value::number(static_cast<double>(applied)));

    std::string summary = "applied " + std::to_string(applied) + " op(s); "
                          + std::to_string(parsed.stages.size()) + " stage(s) in result";
    return ToolResult{pl::Value::map(std::move(out)), std::move(summary), std::nullopt};
  };
  return t;
}

}  // namespace souxmar::ai
