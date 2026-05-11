// SPDX-License-Identifier: Apache-2.0
//
// Tool: compute_field
//
// Sprint 5 push 3 — activates this tool against the new `postproc.*`
// C ABI surface (postproc.h + RegistryDispatcher::dispatch_postproc).
// The tool wraps the session's current mesh + field as synthetic
// upstream stages so RegistryDispatcher resolves them by the convention
// `mesh: {from: __session_mesh__}` / `field: {from: __session_field__}`,
// dispatches the named postproc capability, and stashes the resulting
// Field on `ctx.field_handle` for downstream tools (`query_field`,
// `compute_field` again, future `export_results`).
//
// Marked ConfirmAlways per docs/AI_INTEGRATION.md — postproc plugins
// can dominate runtime + cost, and an unattended re-postproc is the
// kind of silent action we want auditable.

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

const char* location_name(souxmar::core::FieldLocation l) {
  switch (l) {
    case souxmar::core::FieldLocation::Nodal:      return "nodal";
    case souxmar::core::FieldLocation::Cell:       return "cell";
    case souxmar::core::FieldLocation::Face:       return "face";
    case souxmar::core::FieldLocation::GaussPoint: return "gauss";
  }
  return "unknown";
}

const char* kind_name(souxmar::core::FieldKind k) {
  switch (k) {
    case souxmar::core::FieldKind::Scalar: return "scalar";
    case souxmar::core::FieldKind::Vector: return "vector";
    case souxmar::core::FieldKind::Tensor: return "tensor";
  }
  return "unknown";
}

}  // namespace

Tool make_compute_field_tool() {
  Tool t;
  t.name             = "compute_field";
  t.description      =
      "Compute a derived field from the current mesh + field via a "
      "registered postproc.* plugin (e.g. von Mises from stress, "
      "magnitude from a vector field). The resulting field is stashed "
      "on the session for further tools.";
  t.category         = "Postproc";
  t.confirmation     = Confirmation::ConfirmAlways;
  t.input_schema_doc =
      "{capability_id: string,                              # e.g. 'postproc.scalar_magnitude'\n"
      " options?: {tolerance?: number, max_iterations?: number, random_seed?: number}}";
  t.output_schema_doc =
      "{capability_id, location, kind, num_components, num_time_steps}";
  t.handler = [](const pipeline::Value& inputs, ToolContext& ctx) -> ToolResult {
    const auto* cap = find_string(inputs, "capability_id");
    if (cap == nullptr) {
      return ToolResult{
          pipeline::Value::null_value(),
          "missing required 'capability_id' input",
          ToolError{"INVALID_ARGUMENT",
              "compute_field requires a string `capability_id`",
              "set capability_id to a registered postproc.* capability"}};
    }
    const std::string cap_str(cap->as_string());

    if (ctx.registry == nullptr || ctx.dispatcher == nullptr) {
      return ToolResult{
          pipeline::Value::null_value(),
          "host registry / dispatcher not wired into ToolContext",
          ToolError{"INTERNAL",
              "compute_field requires ToolContext.registry + ToolContext.dispatcher"}};
    }
    if (ctx.registry->find_postproc(cap_str) == nullptr) {
      return ToolResult{
          pipeline::Value::null_value(),
          "no postproc capability registered as '" + cap_str + "'",
          ToolError{"PLUGIN_NOT_FOUND",
              "no postproc capability registered as '" + cap_str + "'",
              "call `souxmar plugin list` to see available capabilities"}};
    }
    if (!ctx.mesh_handle) {
      return ToolResult{
          pipeline::Value::null_value(),
          "no mesh available — call `mesh` first",
          ToolError{"PRECONDITION_FAILED",
              "compute_field requires a mesh; ToolContext.mesh_handle is null",
              "invoke the `mesh` tool before `compute_field`"}};
    }
    if (!ctx.field_handle) {
      return ToolResult{
          pipeline::Value::null_value(),
          "no field available — call `solve` first",
          ToolError{"PRECONDITION_FAILED",
              "compute_field requires an input field; ToolContext.field_handle is null",
              "invoke `solve` (or a prior `compute_field`) before this call"}};
    }

    // Wrap the session mesh + field as synthetic upstreams so the
    // dispatcher's postproc path resolves both via the standard
    // {from: <stage_id>} convention.
    auto mesh_so  = std::make_shared<pipeline::StageOutput>();
    mesh_so->kind = pipeline::StageOutput::Kind::Mesh;
    mesh_so->mesh = ctx.mesh_handle;
    auto field_so  = std::make_shared<pipeline::StageOutput>();
    field_so->kind  = pipeline::StageOutput::Kind::Field;
    field_so->field = ctx.field_handle;

    std::map<std::string, std::shared_ptr<void>> upstream;
    upstream.emplace("__session_mesh__",  std::static_pointer_cast<void>(mesh_so));
    upstream.emplace("__session_field__", std::static_pointer_cast<void>(field_so));

    std::map<std::string, pipeline::Value> stage_input;
    stage_input.emplace("mesh",  pipeline::Value::stage_ref("__session_mesh__"));
    stage_input.emplace("field", pipeline::Value::stage_ref("__session_field__"));
    if (const auto* opts = inputs.find("options");
        opts != nullptr && opts->kind() == pipeline::Value::Kind::Map) {
      for (const auto& [k, v] : opts->as_map()) stage_input.emplace(k, v);
    }
    auto input_value = pipeline::Value::map(std::move(stage_input));

    pipeline::DispatchContext dctx{cap_str, input_value, upstream};
    auto dr = ctx.dispatcher->dispatch(dctx);
    if (auto* derr = std::get_if<pipeline::DispatchError>(&dr)) {
      return ToolResult{
          pipeline::Value::null_value(),
          "postproc dispatch failed",
          ToolError{"DISPATCH_FAILED", derr->message}};
    }

    auto payload = std::get<pipeline::DispatchSuccess>(dr);
    const auto* so = static_cast<const pipeline::StageOutput*>(payload.get());
    if (!so || so->kind != pipeline::StageOutput::Kind::Field || !so->field) {
      return ToolResult{
          pipeline::Value::null_value(),
          "postproc returned no field",
          ToolError{"INTERNAL",
              "postproc dispatch returned an unexpected payload kind"}};
    }
    ctx.field_handle = so->field;

    const auto location       = ctx.field_handle->location();
    const auto kind           = ctx.field_handle->kind();
    const auto components     = ctx.field_handle->components();
    const auto num_time_steps = ctx.field_handle->num_time_steps();

    std::map<std::string, pipeline::Value> out;
    out.emplace("capability_id",   pipeline::Value::string(cap_str));
    out.emplace("location",        pipeline::Value::string(location_name(location)));
    out.emplace("kind",            pipeline::Value::string(kind_name(kind)));
    out.emplace("num_components",  pipeline::Value::number(static_cast<double>(components)));
    out.emplace("num_time_steps",  pipeline::Value::number(static_cast<double>(num_time_steps)));

    std::string summary = "computed (via " + cap_str + "): " +
                          std::string(kind_name(kind)) + " field on " +
                          std::string(location_name(location)) +
                          ", " + std::to_string(components) + " components × " +
                          std::to_string(num_time_steps) + " step(s)";

    return ToolResult{
        pipeline::Value::map(std::move(out)),
        std::move(summary),
        std::nullopt};
  };
  return t;
}

}  // namespace souxmar::ai
