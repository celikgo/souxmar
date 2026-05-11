// SPDX-License-Identifier: Apache-2.0
//
// Tool: apply_inlet
//
// Sprint 8 push 4. CFD-aware sibling of `set_bc` — records an inlet
// boundary condition on a tagged surface with the schema CFD solvers
// (cfd-stub, openfoam-solver) expect, instead of leaving the agent to
// hand-craft a generic Dirichlet bag.
//
// The general-purpose `set_bc` tool still exists (FEM flows use it).
// This tool's value is that it carries CFD vocabulary: `velocity` (vector
// or magnitude), optional `pressure`, optional `turbulence_intensity`,
// optional `hydraulic_diameter`. Downstream the cfd-stub and OpenFOAM
// adapters look for entries whose `type == "inlet"`, so the LLM doesn't
// have to encode the inlet/wall/outlet trichotomy as free-form strings.

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

// Velocity may be a scalar magnitude (the solver assumes inlet-normal)
// or a 3-vector. Both are accepted; the solver consumes whichever
// shape the BC entry carries.
bool is_velocity(const pipeline::Value* v) noexcept {
  if (!v) return false;
  if (v->kind() == pipeline::Value::Kind::Number) return true;
  if (v->kind() != pipeline::Value::Kind::List)   return false;
  const auto& l = v->as_list();
  if (l.size() != 3) return false;
  for (const auto& c : l) {
    if (c.kind() != pipeline::Value::Kind::Number) return false;
  }
  return true;
}

}  // namespace

Tool make_apply_inlet_tool() {
  Tool t;
  t.name             = "apply_inlet";
  t.description      =
      "Apply a CFD inlet BC to a tagged surface: velocity (scalar magnitude or 3-vector), "
      "optional static pressure, optional turbulence intensity / hydraulic diameter. "
      "The BC is staged in session state and consumed by the next `solve` call.";
  t.category         = "CFD";
  t.confirmation     = Confirmation::ConfirmOnce;
  t.input_schema_doc =
      "{tag: string,                                       # boundary tag the BC binds to\n"
      " velocity: number | [number, number, number],       # magnitude (inlet-normal) or vector\n"
      " pressure?: number,                                 # optional static pressure (Pa)\n"
      " turbulence_intensity?: number,                     # I in [0,1], optional (default 0.05)\n"
      " hydraulic_diameter?: number                        # optional, m\n"
      "}";
  t.output_schema_doc =
      "{count: number,    # total BCs staged this session (across set_bc + apply_*)\n"
      " latest: {...}     # the inlet BC just added; type=='inlet'\n"
      "}";
  t.handler = [](const pipeline::Value& inputs, ToolContext& ctx) -> ToolResult {
    if (inputs.kind() != pipeline::Value::Kind::Map) {
      return ToolResult{
          pipeline::Value::null_value(),
          "input must be a map",
          ToolError{"INVALID_ARGUMENT", "apply_inlet input must be a map"}};
    }
    const auto* tag      = find(inputs, "tag");
    const auto* velocity = find(inputs, "velocity");
    if (!is_string(tag) || !is_velocity(velocity)) {
      return ToolResult{
          pipeline::Value::null_value(),
          "missing or wrong-typed required field",
          ToolError{"INVALID_ARGUMENT",
              "apply_inlet requires string `tag` and number-or-3-vector `velocity`",
              "{tag: 'inlet', velocity: 1.5} or {tag: 'inlet', velocity: [1,0,0]}"}};
    }
    // Cross-validate optional numeric fields when present.
    if (const auto* p  = find(inputs, "pressure");             p  && !is_number(p)) {
      return ToolResult{pipeline::Value::null_value(), "pressure must be a number",
          ToolError{"INVALID_ARGUMENT", "`pressure` must be a number when set"}};
    }
    if (const auto* ti = find(inputs, "turbulence_intensity"); ti && !is_number(ti)) {
      return ToolResult{pipeline::Value::null_value(),
          "turbulence_intensity must be a number",
          ToolError{"INVALID_ARGUMENT",
              "`turbulence_intensity` must be a number when set"}};
    }
    if (const auto* hd = find(inputs, "hydraulic_diameter");   hd && !is_number(hd)) {
      return ToolResult{pipeline::Value::null_value(),
          "hydraulic_diameter must be a number",
          ToolError{"INVALID_ARGUMENT",
              "`hydraulic_diameter` must be a number when set"}};
    }

    if (ctx.session_state == nullptr) {
      return ToolResult{
          pipeline::Value::null_value(),
          "session_state not wired",
          ToolError{"INTERNAL",
              "apply_inlet requires ToolContext.session_state to be a writable Map",
              "wrap your invocation with a non-null session_state Value of kind Map"}};
    }
    if (ctx.session_state->kind() != pipeline::Value::Kind::Map) {
      *ctx.session_state = pipeline::Value::map({});
    }

    // Build the BC entry: copy every recognised field, force `type` to "inlet".
    std::map<std::string, pipeline::Value> bc;
    for (const auto& [k, v] : inputs.as_map()) bc.emplace(k, v);
    bc["type"] = pipeline::Value::string("inlet");
    auto bc_value = pipeline::Value::map(std::move(bc));

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

    std::string summary = "inlet on tag '" + std::string(tag->as_string()) +
                          "' staged (" + std::to_string(count) + " total BCs)";
    return ToolResult{
        pipeline::Value::map(std::move(out)),
        std::move(summary),
        std::nullopt};
  };
  return t;
}

}  // namespace souxmar::ai
