// SPDX-License-Identifier: Apache-2.0
//
// Tool: apply_wall
//
// Sprint 8 push 4. CFD wall BC — records a no-slip / slip / wall-function
// condition on a tagged surface. Like apply_inlet, this is the
// CFD-vocabulary sibling of set_bc; the BC entry it produces has
// type=='wall' so downstream solvers can route correctly.
//
// `wall_function` is the canonical name in OpenFOAM-style solvers for
// the y+ wall-treatment family (nutkWallFunction et al.). The plugin
// chooses the specific kernel; this tool's vocabulary is solver-agnostic.

#include "souxmar/ai/tool.h"

#include <map>
#include <string>
#include <vector>

namespace souxmar::ai {

namespace {

const pipeline::Value* find(const pipeline::Value& v, const char* key) {
  return v.kind() == pipeline::Value::Kind::Map ? v.find(key) : nullptr;
}
bool is_string(const pipeline::Value* v) noexcept {
  return v != nullptr && v->kind() == pipeline::Value::Kind::String;
}
bool is_number(const pipeline::Value* v) noexcept {
  return v != nullptr && v->kind() == pipeline::Value::Kind::Number;
}

}  // namespace

Tool make_apply_wall_tool() {
  Tool t;
  t.name             = "apply_wall";
  t.description      =
      "Apply a CFD wall BC to a tagged surface: choose between no-slip / slip / "
      "wall-function. Optional thermal coupling (temperature) and surface roughness. "
      "Staged in session state; consumed by the next `solve` call.";
  t.category         = "CFD";
  t.confirmation     = Confirmation::ConfirmOnce;
  t.input_schema_doc =
      "{tag: string,                                               # boundary tag\n"
      " condition: 'no_slip' | 'slip' | 'wall_function',           # default 'no_slip'\n"
      " temperature?: number,                                      # optional, K — fixed-temp wall\n"
      " roughness?: number                                         # optional, m — Nikuradse equivalent\n"
      "}";
  t.output_schema_doc =
      "{count: number, latest: {...}}    # latest.type == 'wall'";
  t.handler = [](const pipeline::Value& inputs, ToolContext& ctx) -> ToolResult {
    if (inputs.kind() != pipeline::Value::Kind::Map) {
      return ToolResult{
          pipeline::Value::null_value(),
          "input must be a map",
          ToolError{"INVALID_ARGUMENT", "apply_wall input must be a map"}};
    }
    const auto* tag = find(inputs, "tag");
    if (!is_string(tag)) {
      return ToolResult{
          pipeline::Value::null_value(),
          "missing string `tag`",
          ToolError{"INVALID_ARGUMENT", "apply_wall requires string `tag`"}};
    }
    // `condition` defaults to 'no_slip' if absent. When present, must
    // be one of the three canonical names.
    std::string condition = "no_slip";
    if (const auto* c = find(inputs, "condition")) {
      if (!is_string(c)) {
        return ToolResult{pipeline::Value::null_value(),
            "condition must be a string",
            ToolError{"INVALID_ARGUMENT",
                "`condition` must be one of no_slip / slip / wall_function"}};
      }
      condition = std::string(c->as_string());
      if (condition != "no_slip" && condition != "slip" &&
          condition != "wall_function") {
        return ToolResult{pipeline::Value::null_value(),
            "unsupported wall condition",
            ToolError{"INVALID_ARGUMENT",
                "`condition` must be one of no_slip / slip / wall_function",
                "got: '" + condition + "'"}};
      }
    }
    if (const auto* tmp = find(inputs, "temperature"); tmp && !is_number(tmp)) {
      return ToolResult{pipeline::Value::null_value(),
          "temperature must be a number",
          ToolError{"INVALID_ARGUMENT",
              "`temperature` must be a number when set"}};
    }
    if (const auto* r = find(inputs, "roughness"); r && !is_number(r)) {
      return ToolResult{pipeline::Value::null_value(),
          "roughness must be a number",
          ToolError{"INVALID_ARGUMENT",
              "`roughness` must be a number when set"}};
    }

    if (ctx.session_state == nullptr) {
      return ToolResult{pipeline::Value::null_value(),
          "session_state not wired",
          ToolError{"INTERNAL",
              "apply_wall requires ToolContext.session_state to be a writable Map"}};
    }
    if (ctx.session_state->kind() != pipeline::Value::Kind::Map) {
      *ctx.session_state = pipeline::Value::map({});
    }

    std::map<std::string, pipeline::Value> bc;
    for (const auto& [k, v] : inputs.as_map()) bc.emplace(k, v);
    bc["type"]      = pipeline::Value::string("wall");
    bc["condition"] = pipeline::Value::string(condition);  // ensure populated
    auto bc_value   = pipeline::Value::map(std::move(bc));

    auto& session = *ctx.session_state;
    std::vector<pipeline::Value> bcs;
    if (const auto* existing = session.find("boundary_conditions");
        existing != nullptr && existing->kind() == pipeline::Value::Kind::List) {
      for (const auto& item : existing->as_list()) bcs.push_back(item);
    }
    bcs.push_back(bc_value);
    const auto count = bcs.size();

    std::map<std::string, pipeline::Value> session_map;
    for (const auto& [k, v] : session.as_map()) {
      if (k == "boundary_conditions") continue;
      session_map.emplace(k, v);
    }
    session_map.emplace("boundary_conditions",
                        pipeline::Value::list(std::move(bcs)));
    session = pipeline::Value::map(std::move(session_map));

    std::map<std::string, pipeline::Value> out;
    out.emplace("count",  pipeline::Value::number(static_cast<double>(count)));
    out.emplace("latest", bc_value);

    std::string summary = condition + " wall on tag '" +
                          std::string(tag->as_string()) + "' staged (" +
                          std::to_string(count) + " total BCs)";
    return ToolResult{
        pipeline::Value::map(std::move(out)),
        std::move(summary),
        std::nullopt};
  };
  return t;
}

}  // namespace souxmar::ai
