// SPDX-License-Identifier: Apache-2.0
//
// Tool: export_results
//
// Run a registered `writer.*` capability against the session mesh +
// (optional) field, writing the artefact to disk. This is the "save
// my analysis" surface for the agent.
//
// Synthetic-upstream pattern matches compute_field / query_mesh_quality
// — we wrap the session's Mesh and Field shared_ptrs as one-off
// StageOutputs and dispatch through RegistryDispatcher, so the writer
// resolves the inputs via the standard `mesh: {from: ...}` /
// `field: {from: ...}` convention.
//
// Confirmation::ConfirmAlways — writers have observable side-effects
// (files appearing on disk) and routinely run external tooling
// (ParaView, gnuplot) opening them. Auditable as a separate event.

#include "souxmar/ai/tool.h"
#include "souxmar/core/field.h"
#include "souxmar/core/mesh.h"
#include "souxmar/pipeline/registry_dispatcher.h"
#include "souxmar/pipeline/runner.h"
#include "souxmar/pipeline/value.h"
#include "souxmar/plugin/registry.h"

#include <filesystem>
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

Tool make_export_results_tool() {
  Tool t;
  t.name = "export_results";
  t.description =
      "Write the current mesh + (optional) field to disk via a "
      "registered writer.* plugin (e.g. writer.vtu for ParaView). "
      "Path may be relative; the writer resolves it. Required when "
      "the user asks to 'save the results' or 'open in ParaView'.";
  t.category = "Export";
  t.confirmation = Confirmation::ConfirmAlways;
  t.input_schema_doc =
      "{capability_id: string,    # e.g. 'writer.vtu' / 'writer.text-summary'\n"
      " path: string,             # output file path (relative resolves to CWD)\n"
      " include_field?: bool}     # default true; ignored if session has no field";
  t.output_schema_doc = "{capability_id, path, has_field}";
  t.handler = [](const pipeline::Value& inputs, ToolContext& ctx) -> ToolResult {
    const auto* cap_v = find_string(inputs, "capability_id");
    const auto* path_v = find_string(inputs, "path");
    if (!cap_v) {
      return ToolResult{pipeline::Value::null_value(),
                        "missing required `capability_id`",
                        ToolError{"INVALID_ARGUMENT",
                                  "export_results requires string `capability_id` "
                                  "(e.g. 'writer.vtu')"}};
    }
    if (!path_v) {
      return ToolResult{pipeline::Value::null_value(),
                        "missing required `path`",
                        ToolError{"INVALID_ARGUMENT", "export_results requires string `path`"}};
    }
    const std::string cap_str(cap_v->as_string());
    const std::string path_str(path_v->as_string());

    if (ctx.registry == nullptr || ctx.dispatcher == nullptr) {
      return ToolResult{
          pipeline::Value::null_value(),
          "host registry / dispatcher not wired into ToolContext",
          ToolError{"INTERNAL",
                    "export_results requires ToolContext.registry + ToolContext.dispatcher"}};
    }
    if (ctx.registry->find_writer(cap_str) == nullptr) {
      return ToolResult{pipeline::Value::null_value(),
                        "no writer capability registered as '" + cap_str + "'",
                        ToolError{"PLUGIN_NOT_FOUND",
                                  "no writer capability registered as '" + cap_str + "'",
                                  "call `list_plugins` (or `souxmar plugin list`) to see "
                                  "available writers"}};
    }
    if (!ctx.mesh_handle) {
      return ToolResult{pipeline::Value::null_value(),
                        "no mesh available — call `mesh` first",
                        ToolError{"PRECONDITION_FAILED",
                                  "export_results requires a mesh; ToolContext.mesh_handle is null",
                                  "invoke the `mesh` tool before `export_results`"}};
    }

    bool include_field = true;
    if (const auto* incl = inputs.find("include_field");
        incl != nullptr && incl->kind() == pipeline::Value::Kind::Bool) {
      include_field = incl->as_bool();
    }
    const bool has_field = include_field && static_cast<bool>(ctx.field_handle);

    auto mesh_so = std::make_shared<pipeline::StageOutput>();
    mesh_so->kind = pipeline::StageOutput::Kind::Mesh;
    mesh_so->mesh = ctx.mesh_handle;

    std::map<std::string, std::shared_ptr<void>> upstream;
    upstream.emplace("__session_mesh__", std::static_pointer_cast<void>(mesh_so));

    std::map<std::string, pipeline::Value> stage_input;
    stage_input.emplace("mesh", pipeline::Value::stage_ref("__session_mesh__"));
    stage_input.emplace("path", pipeline::Value::string(path_str));

    if (has_field) {
      auto field_so = std::make_shared<pipeline::StageOutput>();
      field_so->kind = pipeline::StageOutput::Kind::Field;
      field_so->field = ctx.field_handle;
      upstream.emplace("__session_field__", std::static_pointer_cast<void>(field_so));
      stage_input.emplace("field", pipeline::Value::stage_ref("__session_field__"));
    }
    // Pass any remaining input keys through to the writer (format
    // knobs etc.). We skip the keys we synthesised so the writer sees
    // them in their stage-ref form, not their raw form.
    if (inputs.kind() == pipeline::Value::Kind::Map) {
      for (const auto& [k, v] : inputs.as_map()) {
        if (k == "capability_id" || k == "path" || k == "include_field" || k == "mesh"
            || k == "field")
          continue;
        stage_input.emplace(k, v);
      }
    }
    auto input_value = pipeline::Value::map(std::move(stage_input));

    pipeline::DispatchContext dctx{cap_str, input_value, upstream};
    auto dr = ctx.dispatcher->dispatch(dctx);
    if (auto* derr = std::get_if<pipeline::DispatchError>(&dr)) {
      return ToolResult{pipeline::Value::null_value(),
                        "writer dispatch failed",
                        ToolError{"DISPATCH_FAILED", derr->message}};
    }

    std::map<std::string, pipeline::Value> out;
    out.emplace("capability_id", pipeline::Value::string(cap_str));
    out.emplace("path", pipeline::Value::string(path_str));
    out.emplace("has_field", pipeline::Value::boolean(has_field));

    std::string summary =
        "wrote " + cap_str + " → " + path_str + (has_field ? " (with field)" : " (mesh only)");
    return ToolResult{pipeline::Value::map(std::move(out)), std::move(summary), std::nullopt};
  };
  return t;
}

}  // namespace souxmar::ai
