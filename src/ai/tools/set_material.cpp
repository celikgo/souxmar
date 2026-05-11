// SPDX-License-Identifier: Apache-2.0
//
// Tool: set_material
//
// Stages a material spec against a tagged region in the session state.
// v1 is metadata-only: appends to session_state["materials"] (a List of
// Maps), letting the agent build up a material library follow-up tools
// (a future `solve` extension, FEniCSx adapter) consume. Mirrors the
// `set_bc` pattern from Sprint 4 push 3.
//
// Per docs/AI_INTEGRATION.md "set_material is BC-tier confirmation" —
// state-mutating but local. Confirmation::ConfirmOnce.

#include "souxmar/ai/tool.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace souxmar::ai {

namespace {

bool is_string(const pipeline::Value* v) {
  return v != nullptr && v->kind() == pipeline::Value::Kind::String;
}

bool is_map(const pipeline::Value* v) {
  return v != nullptr && v->kind() == pipeline::Value::Kind::Map;
}

}  // namespace

Tool make_set_material_tool() {
  Tool t;
  t.name             = "set_material";
  t.description      =
      "Attach a material specification (linear elastic / thermal / etc.) "
      "to a tagged region. The material is staged in session state and "
      "consumed by the next `solve` call.";
  t.category         = "BC";
  t.confirmation     = Confirmation::ConfirmOnce;
  t.input_schema_doc =
      "{tag: string,                                    # geometric tag the material binds to\n"
      " model: 'linear_elastic' | 'thermal' | 'rigid' | 'custom',\n"
      " properties: { <key>: number | string, ... },    # e.g. {E: 210e9, nu: 0.3, rho: 7850}\n"
      " name?: string                                   # optional human-readable label\n"
      "}";
  t.output_schema_doc =
      "{count: number,    # total materials staged this session\n"
      " latest: {...}     # the material just added\n"
      "}";
  t.handler = [](const pipeline::Value& inputs, ToolContext& ctx) -> ToolResult {
    if (inputs.kind() != pipeline::Value::Kind::Map) {
      return ToolResult{
          pipeline::Value::null_value(),
          "input must be a map",
          ToolError{"INVALID_ARGUMENT", "set_material input must be a map"}};
    }
    const auto* tag        = inputs.find("tag");
    const auto* model      = inputs.find("model");
    const auto* properties = inputs.find("properties");
    if (!is_string(tag) || !is_string(model) || !is_map(properties)) {
      return ToolResult{
          pipeline::Value::null_value(),
          "missing or wrong-typed required field",
          ToolError{"INVALID_ARGUMENT",
              "set_material requires string `tag`, string `model`, "
              "and map `properties`"}};
    }
    const std::string model_str(model->as_string());
    // Allow custom models — the agent can extend this without a tool
    // upgrade. The recognised v1 list is documented; anything else
    // routes to whatever solver knows what to do with it.
    if (model_str.empty()) {
      return ToolResult{
          pipeline::Value::null_value(),
          "`model` must not be empty",
          ToolError{"INVALID_ARGUMENT", "`model` cannot be the empty string"}};
    }

    if (ctx.session_state == nullptr) {
      return ToolResult{
          pipeline::Value::null_value(),
          "session_state not wired",
          ToolError{"INTERNAL",
              "set_material requires ToolContext.session_state to be a writable Map"}};
    }
    if (ctx.session_state->kind() != pipeline::Value::Kind::Map) {
      *ctx.session_state = pipeline::Value::map({});
    }

    std::map<std::string, pipeline::Value> material;
    for (const auto& [k, v] : inputs.as_map()) material.emplace(k, v);
    auto material_value = pipeline::Value::map(std::move(material));

    auto& session = *ctx.session_state;
    std::vector<pipeline::Value> mats;
    if (const auto* existing = session.find("materials");
        existing != nullptr && existing->kind() == pipeline::Value::Kind::List) {
      for (const auto& item : existing->as_list()) mats.push_back(item);
    }
    mats.push_back(material_value);
    const auto count = mats.size();

    std::map<std::string, pipeline::Value> session_map;
    for (const auto& [k, v] : session.as_map()) {
      if (k == "materials") continue;
      session_map.emplace(k, v);
    }
    session_map.emplace("materials", pipeline::Value::list(std::move(mats)));
    session = pipeline::Value::map(std::move(session_map));

    std::map<std::string, pipeline::Value> out;
    out.emplace("count",  pipeline::Value::number(static_cast<double>(count)));
    out.emplace("latest", material_value);

    std::string summary = model_str + " material on tag '" +
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
