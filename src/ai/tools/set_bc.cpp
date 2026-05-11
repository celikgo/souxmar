// SPDX-License-Identifier: Apache-2.0
//
// Tool: set_bc
//
// Records a boundary condition against a tagged entity in the session
// state. v1 is metadata-only: it appends to session_state["boundary_conditions"]
// (a List of Maps), letting the agent build up a BC bag that follow-up
// tools (e.g. `solve`) consume. Sprint 6 promotes this to a typed
// per-project BC model with validation against the loaded geometry.

#include "souxmar/ai/tool.h"

#include <map>
#include <string>
#include <vector>

namespace souxmar::ai {

namespace {

bool is_string(const pipeline::Value* v) {
  return v != nullptr && v->kind() == pipeline::Value::Kind::String;
}

bool is_number_or_list(const pipeline::Value* v) {
  return v != nullptr && (v->kind() == pipeline::Value::Kind::Number ||
                          v->kind() == pipeline::Value::Kind::List);
}

}  // namespace

Tool make_set_bc_tool() {
  Tool t;
  t.name             = "set_bc";
  t.description      =
      "Attach a boundary condition (Dirichlet / Neumann / Robin) to a tagged "
      "entity. The BC is staged in session state and consumed by the next "
      "`solve` call.";
  t.category         = "BC";
  // BCs are state-mutating but local; the user already initiated this
  // tool call. Match docs/AI_INTEGRATION.md's "confirm-once" tier so
  // re-prompts don't punish a model iterating on a BC sweep.
  t.confirmation     = Confirmation::ConfirmOnce;
  t.input_schema_doc =
      "{tag: string,                                       # geometric tag the BC binds to\n"
      " type: 'dirichlet' | 'neumann' | 'robin',\n"
      " value: number | [number, number, number],          # scalar or vector\n"
      " field?: string                                     # 'displacement' | 'temperature' | ...\n"
      "}";
  t.output_schema_doc =
      "{count: number,    # total BCs staged this session\n"
      " latest: {...}     # the BC just added\n"
      "}";
  t.handler = [](const pipeline::Value& inputs, ToolContext& ctx) -> ToolResult {
    if (inputs.kind() != pipeline::Value::Kind::Map) {
      return ToolResult{
          pipeline::Value::null_value(),
          "input must be a map",
          ToolError{"INVALID_ARGUMENT", "set_bc input must be a map"}};
    }
    const auto* tag   = inputs.find("tag");
    const auto* type  = inputs.find("type");
    const auto* value = inputs.find("value");
    if (!is_string(tag) || !is_string(type) || !is_number_or_list(value)) {
      return ToolResult{
          pipeline::Value::null_value(),
          "missing or wrong-typed required field",
          ToolError{"INVALID_ARGUMENT",
              "set_bc requires string `tag`, string `type`, and "
              "number-or-list `value`"}};
    }
    const std::string type_str = std::string(type->as_string());
    if (type_str != "dirichlet" && type_str != "neumann" && type_str != "robin") {
      return ToolResult{
          pipeline::Value::null_value(),
          "unsupported BC type",
          ToolError{"INVALID_ARGUMENT",
              "`type` must be one of dirichlet / neumann / robin",
              "got: '" + type_str + "'"}};
    }

    if (ctx.session_state == nullptr) {
      return ToolResult{
          pipeline::Value::null_value(),
          "session_state not wired",
          ToolError{"INTERNAL",
              "set_bc requires ToolContext.session_state to be a writable Map",
              "wrap your invocation with a non-null session_state Value of "
              "kind Map"}};
    }
    if (ctx.session_state->kind() != pipeline::Value::Kind::Map) {
      *ctx.session_state = pipeline::Value::map({});
    }

    // Build the BC entry. Copy through every recognised field plus
    // `field` (optional). Unknown fields are passed through so future
    // solver plugins can introduce keys without a schema bump.
    std::map<std::string, pipeline::Value> bc;
    for (const auto& [k, v] : inputs.as_map()) bc.emplace(k, v);
    auto bc_value = pipeline::Value::map(std::move(bc));

    // Append to session_state["boundary_conditions"].
    auto& session = *ctx.session_state;
    std::vector<pipeline::Value> bcs;
    if (const auto* existing = session.find("boundary_conditions");
        existing != nullptr && existing->kind() == pipeline::Value::Kind::List) {
      for (const auto& item : existing->as_list()) bcs.push_back(item);
    }
    bcs.push_back(bc_value);
    const auto count = bcs.size();

    // Reassemble the session map with the updated BC list.
    std::map<std::string, pipeline::Value> session_map;
    for (const auto& [k, v] : session.as_map()) {
      if (k == "boundary_conditions") continue;
      session_map.emplace(k, v);
    }
    session_map.emplace("boundary_conditions", pipeline::Value::list(std::move(bcs)));
    session = pipeline::Value::map(std::move(session_map));

    std::map<std::string, pipeline::Value> out;
    out.emplace("count",  pipeline::Value::number(static_cast<double>(count)));
    out.emplace("latest", bc_value);

    std::string summary = type_str + " BC on tag '" +
                          std::string(tag->as_string()) + "' staged (" +
                          std::to_string(count) + " total)";

    return ToolResult{
        pipeline::Value::map(std::move(out)),
        std::move(summary),
        std::nullopt};
  };
  return t;
}

}  // namespace souxmar::ai
