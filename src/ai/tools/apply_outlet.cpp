// SPDX-License-Identifier: Apache-2.0
//
// Tool: apply_outlet
//
// Sprint 8 push 4. CFD outlet BC — pressure_outlet / outflow /
// fully_developed on a tagged surface. Sibling to apply_inlet /
// apply_wall; the entry it produces has type=='outlet'.
//
// Vocabulary notes:
//   * pressure_outlet  — Dirichlet static pressure; common for
//                        compressible/subsonic flows.
//   * outflow          — zero-gradient on velocity, pressure imposed
//                        elsewhere (the OpenFOAM `zeroGradient` family).
//   * fully_developed  — convective outflow assuming far-field has
//                        converged.

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

Tool make_apply_outlet_tool() {
  Tool t;
  t.name = "apply_outlet";
  t.description =
      "Apply a CFD outlet BC to a tagged surface: pressure_outlet (Dirichlet on "
      "static pressure), outflow (zero-gradient on velocity), or fully_developed "
      "(convective). Staged in session state; consumed by the next `solve` call.";
  t.category = "CFD";
  t.confirmation = Confirmation::ConfirmOnce;
  t.input_schema_doc =
      "{tag: string,                                                 # boundary tag\n"
      " condition: 'pressure_outlet' | 'outflow' | 'fully_developed',# default 'pressure_outlet'\n"
      " pressure?: number                                            # required when "
      "condition='pressure_outlet'\n"
      "}";
  t.output_schema_doc = "{count: number, latest: {...}}    # latest.type == 'outlet'";
  t.handler = [](const pipeline::Value& inputs, ToolContext& ctx) -> ToolResult {
    if (inputs.kind() != pipeline::Value::Kind::Map) {
      return ToolResult{pipeline::Value::null_value(),
                        "input must be a map",
                        ToolError{"INVALID_ARGUMENT", "apply_outlet input must be a map"}};
    }
    const auto* tag = find(inputs, "tag");
    if (!is_string(tag)) {
      return ToolResult{pipeline::Value::null_value(),
                        "missing string `tag`",
                        ToolError{"INVALID_ARGUMENT", "apply_outlet requires string `tag`"}};
    }
    std::string condition = "pressure_outlet";
    if (const auto* c = find(inputs, "condition")) {
      if (!is_string(c)) {
        return ToolResult{pipeline::Value::null_value(),
                          "condition must be a string",
                          ToolError{"INVALID_ARGUMENT",
                                    "`condition` must be one of "
                                    "pressure_outlet / outflow / fully_developed"}};
      }
      condition = std::string(c->as_string());
      if (condition != "pressure_outlet" && condition != "outflow"
          && condition != "fully_developed") {
        return ToolResult{pipeline::Value::null_value(),
                          "unsupported outlet condition",
                          ToolError{"INVALID_ARGUMENT",
                                    "`condition` must be one of "
                                    "pressure_outlet / outflow / fully_developed",
                                    "got: '" + condition + "'"}};
      }
    }
    // pressure_outlet requires a `pressure` number — the other two
    // conditions accept any pressure value (or none) since the solver
    // imposes velocity-side conditions instead.
    const auto* pressure = find(inputs, "pressure");
    if (condition == "pressure_outlet" && !is_number(pressure)) {
      return ToolResult{pipeline::Value::null_value(),
                        "pressure_outlet requires a numeric `pressure`",
                        ToolError{"INVALID_ARGUMENT",
                                  "apply_outlet with condition='pressure_outlet' "
                                  "requires a numeric `pressure` (Pa)",
                                  "set pressure: 0 for gauge-pressure formulations"}};
    }
    if (pressure && !is_number(pressure)) {
      return ToolResult{pipeline::Value::null_value(),
                        "pressure must be a number",
                        ToolError{"INVALID_ARGUMENT", "`pressure` must be a number when set"}};
    }

    if (ctx.session_state == nullptr) {
      return ToolResult{
          pipeline::Value::null_value(),
          "session_state not wired",
          ToolError{"INTERNAL",
                    "apply_outlet requires ToolContext.session_state to be a writable Map"}};
    }
    if (ctx.session_state->kind() != pipeline::Value::Kind::Map) {
      *ctx.session_state = pipeline::Value::map({});
    }

    std::map<std::string, pipeline::Value> bc;
    for (const auto& [k, v] : inputs.as_map())
      bc.emplace(k, v);
    bc["type"] = pipeline::Value::string("outlet");
    bc["condition"] = pipeline::Value::string(condition);
    auto bc_value = pipeline::Value::map(std::move(bc));

    auto& session = *ctx.session_state;
    std::vector<pipeline::Value> bcs;
    if (const auto* existing = session.find("boundary_conditions");
        existing != nullptr && existing->kind() == pipeline::Value::Kind::List) {
      for (const auto& item : existing->as_list())
        bcs.push_back(item);
    }
    bcs.push_back(bc_value);
    const auto count = bcs.size();

    std::map<std::string, pipeline::Value> session_map;
    for (const auto& [k, v] : session.as_map()) {
      if (k == "boundary_conditions")
        continue;
      session_map.emplace(k, v);
    }
    session_map.emplace("boundary_conditions", pipeline::Value::list(std::move(bcs)));
    session = pipeline::Value::map(std::move(session_map));

    std::map<std::string, pipeline::Value> out;
    out.emplace("count", pipeline::Value::number(static_cast<double>(count)));
    out.emplace("latest", bc_value);

    std::string summary = condition + " outlet on tag '" + std::string(tag->as_string())
                          + "' staged (" + std::to_string(count) + " total BCs)";
    return ToolResult{pipeline::Value::map(std::move(out)), std::move(summary), std::nullopt};
  };
  return t;
}

}  // namespace souxmar::ai
