// SPDX-License-Identifier: Apache-2.0
//
// Tool: solve
//
// Dispatches a registered `solver.*` capability against the session's
// current mesh (set by a prior `mesh` tool call) plus any staged BCs /
// materials. Returns a Field summary; stores the Field handle in
// ctx.field_handle for downstream tools.
//
// Per docs/AI_INTEGRATION.md, `solve` is marked ConfirmAlways — it can
// dominate runtime + cost, and an unattended re-solve is the kind of
// silent action we want auditable.

#include "souxmar/ai/tool.h"
#include "souxmar/core/field.h"
#include "souxmar/core/mesh.h"
#include "souxmar/pipeline/registry_dispatcher.h"
#include "souxmar/pipeline/runner.h"
#include "souxmar/pipeline/value.h"
#include "souxmar/plugin/registry.h"

#include <map>
#include <memory>
#include <string>
#include <utility>

namespace souxmar::ai {

namespace {

const pipeline::Value* find_string(const pipeline::Value& v, const char* k) {
  const auto* r = v.find(k);
  return (r && r->kind() == pipeline::Value::Kind::String) ? r : nullptr;
}

}  // namespace

Tool make_solve_tool() {
  Tool t;
  t.name = "solve";
  t.description =
      "Run a registered solver.* plugin against the current mesh (set by a "
      "prior `mesh` call) plus any boundary conditions staged via `set_bc`. "
      "Returns a Field summary; the field handle is stashed on the session.";
  t.category = "Solve";
  // Solves are the expensive, network-using, side-effecting call. Match
  // docs/AI_INTEGRATION.md's stricter tier — every solve prompts so the
  // user sees the runtime / cost estimate before commit.
  t.confirmation = Confirmation::ConfirmAlways;
  t.input_schema_doc =
      "{capability_id: string,                            # e.g. 'solver.elasticity.linear'\n"
      " options?: {tolerance?: number, max_iterations?: number, random_seed?: number}}";
  t.output_schema_doc = "{capability_id, layout, kind, num_components}";
  t.handler = [](const pipeline::Value& inputs, ToolContext& ctx) -> ToolResult {
    const auto* cap = find_string(inputs, "capability_id");
    if (cap == nullptr) {
      return ToolResult{pipeline::Value::null_value(),
                        "missing required 'capability_id' input",
                        ToolError{"INVALID_ARGUMENT",
                                  "solve requires a string `capability_id`",
                                  "set capability_id to a registered solver.* capability"}};
    }
    const std::string cap_str(cap->as_string());

    if (ctx.registry == nullptr || ctx.dispatcher == nullptr) {
      return ToolResult{
          pipeline::Value::null_value(),
          "host registry / dispatcher not wired into ToolContext",
          ToolError{"INTERNAL",
                    "the `solve` tool requires ToolContext.registry + ToolContext.dispatcher"}};
    }
    if (ctx.registry->find_solver(cap_str) == nullptr) {
      return ToolResult{pipeline::Value::null_value(),
                        "no solver capability registered as '" + cap_str + "'",
                        ToolError{"PLUGIN_NOT_FOUND",
                                  "no solver capability registered as '" + cap_str + "'",
                                  "call `souxmar plugin list` to see available capabilities"}};
    }
    if (!ctx.mesh_handle) {
      return ToolResult{pipeline::Value::null_value(),
                        "no mesh available — call `mesh` first",
                        ToolError{"PRECONDITION_FAILED",
                                  "solve requires a mesh; ToolContext.mesh_handle is null",
                                  "invoke the `mesh` tool before `solve` so the session "
                                  "carries a mesh handle"}};
    }

    // Build the stage input bag: a Map containing `mesh: {from: '__session__'}`
    // so RegistryDispatcher's solver path picks up the upstream mesh handle.
    // We also forward any caller-supplied options.
    std::map<std::string, pipeline::Value> stage_input;
    stage_input.emplace("mesh", pipeline::Value::stage_ref("__session_mesh__"));
    if (const auto* bcs =
            ctx.session_state ? ctx.session_state->find("boundary_conditions") : nullptr;
        bcs != nullptr) {
      stage_input.emplace("boundary_conditions", *bcs);
    }
    if (const auto* opts = inputs.find("options");
        opts != nullptr && opts->kind() == pipeline::Value::Kind::Map) {
      for (const auto& [k, v] : opts->as_map())
        stage_input.emplace(k, v);
    }
    auto input_value = pipeline::Value::map(std::move(stage_input));

    // Stage the session's mesh under the synthetic upstream id so the
    // RegistryDispatcher resolves the `mesh: { from: __session_mesh__ }`
    // reference. We wrap the Mesh into a StageOutput with the dispatcher's
    // expected discriminated layout.
    auto stage_output = std::make_shared<pipeline::StageOutput>();
    stage_output->kind = pipeline::StageOutput::Kind::Mesh;
    stage_output->mesh = ctx.mesh_handle;
    std::map<std::string, std::shared_ptr<void>> upstream;
    upstream.emplace("__session_mesh__", std::static_pointer_cast<void>(stage_output));

    pipeline::DispatchContext dctx{cap_str, input_value, upstream};
    auto dr = ctx.dispatcher->dispatch(dctx);
    if (auto* derr = std::get_if<pipeline::DispatchError>(&dr)) {
      return ToolResult{pipeline::Value::null_value(),
                        "solver dispatch failed",
                        ToolError{"DISPATCH_FAILED", derr->message}};
    }

    auto payload = std::get<pipeline::DispatchSuccess>(dr);
    const auto* so = static_cast<const pipeline::StageOutput*>(payload.get());
    if (!so || so->kind != pipeline::StageOutput::Kind::Field || !so->field) {
      return ToolResult{
          pipeline::Value::null_value(),
          "solver returned no field",
          ToolError{"INTERNAL", "solver dispatch returned an unexpected payload kind"}};
    }
    ctx.field_handle = so->field;

    // Summarise the field. core::Field exposes location (Nodal/Cell/...)
    // and kind (Scalar/Vector/Tensor) via accessors — we report them as
    // strings so the agent's plain-text reasoning chain reads naturally.
    const auto location = ctx.field_handle->location();
    const auto kind = ctx.field_handle->kind();
    const auto components = ctx.field_handle->components();

    auto location_str = [&]() -> const char* {
      switch (location) {
        case core::FieldLocation::Nodal:
          return "nodal";
        case core::FieldLocation::Cell:
          return "cell";
        case core::FieldLocation::Face:
          return "face";
        case core::FieldLocation::GaussPoint:
          return "gauss";
      }
      return "unknown";
    }();
    auto kind_str = [&]() -> const char* {
      switch (kind) {
        case core::FieldKind::Scalar:
          return "scalar";
        case core::FieldKind::Vector:
          return "vector";
        case core::FieldKind::Tensor:
          return "tensor";
      }
      return "unknown";
    }();

    std::map<std::string, pipeline::Value> out;
    out.emplace("capability_id", pipeline::Value::string(cap_str));
    out.emplace("location", pipeline::Value::string(location_str));
    out.emplace("kind", pipeline::Value::string(kind_str));
    out.emplace("num_components", pipeline::Value::number(static_cast<double>(components)));

    std::string summary = "solved (via " + cap_str + "): " + std::string(kind_str) + " field on "
                          + std::string(location_str) + ", " + std::to_string(components)
                          + " components";

    return ToolResult{pipeline::Value::map(std::move(out)), std::move(summary), std::nullopt};
  };
  return t;
}

}  // namespace souxmar::ai
