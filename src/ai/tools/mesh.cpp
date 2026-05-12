// SPDX-License-Identifier: Apache-2.0
//
// Tool: mesh
//
// Dispatches a registered `mesher.*` capability through ToolContext.dispatcher
// and stores the resulting mesh in ctx.mesh_handle for downstream tools
// (`solve`, future `export_results`) to consume. v1 ignores upstream
// geometry (matching the placeholder hello-mesher); Sprint 6 wires real
// geometry through a Geometry handle slot.

#include "souxmar/core/mesh.h"

#include "souxmar/ai/tool.h"
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

// Read a required string field; populate `out_error` on failure.
const std::string* required_string(const pipeline::Value& inputs,
                                   const char* key,
                                   ToolResult& out_error) {
  const auto* v = inputs.find(key);
  if (v == nullptr || v->kind() != pipeline::Value::Kind::String) {
    out_error =
        ToolResult{pipeline::Value::null_value(),
                   std::string{"missing required '"} + key + "' input",
                   ToolError{"INVALID_ARGUMENT",
                             std::string{"input must include a string '"} + key + "' field"}};
    return nullptr;
  }
  // We don't actually need to copy the data — but we want a pointer to a
  // null-terminated std::string for the caller's downstream consumption.
  // The Value owns its underlying string for the lifetime of inputs.
  thread_local std::string scratch;
  scratch = std::string(v->as_string());
  return &scratch;
}

}  // namespace

Tool make_mesh_tool() {
  Tool t;
  t.name = "mesh";
  t.description =
      "Run a registered mesher.* plugin against the current geometry. Returns "
      "a mesh summary (node / cell counts, bounding box). The resulting mesh "
      "handle is stashed on the session for subsequent solve / export tools.";
  t.category = "Mesh";
  // Mesh runs can be expensive but the manifest's per-tool runtime
  // budget governs the prompt threshold (Sprint 5). For v1 we mark
  // Auto — the orchestrator's `souxmar agent invoke` is explicitly
  // user-initiated.
  t.confirmation = Confirmation::Auto;
  t.input_schema_doc =
      "{capability_id: string,                  # e.g. 'mesher.tetra.hello'\n"
      " options?: {target_size?: number, element_order?: number, optimize?: bool}}";
  t.output_schema_doc = "{capability_id, num_nodes, num_cells}";
  t.handler = [](const pipeline::Value& inputs, ToolContext& ctx) -> ToolResult {
    ToolResult err;
    const auto* cap = required_string(inputs, "capability_id", err);
    if (cap == nullptr)
      return err;

    if (ctx.registry == nullptr || ctx.dispatcher == nullptr) {
      return ToolResult{
          pipeline::Value::null_value(),
          "host registry / dispatcher not wired into ToolContext",
          ToolError{"INTERNAL",
                    "the `mesh` tool requires ToolContext.registry + ToolContext.dispatcher",
                    "construct the ToolContext via the CLI's `agent invoke` shim or "
                    "wire them manually for embedded callers"}};
    }
    if (ctx.registry->find_mesher(*cap) == nullptr) {
      return ToolResult{pipeline::Value::null_value(),
                        "no mesher capability registered as '" + *cap + "'",
                        ToolError{"PLUGIN_NOT_FOUND",
                                  "no mesher capability registered as '" + *cap + "'",
                                  "call `souxmar plugin list` to see available capabilities"}};
    }

    // Build a Map-valued input bag mirroring what RegistryDispatcher's
    // mesher dispatch path inspects: `target_size`, `optimize`,
    // `element_order`. Anything else the user passed under `options`
    // rides along unchanged (future plugins may inspect more keys).
    std::map<std::string, pipeline::Value> stage_input;
    if (const auto* opts = inputs.find("options");
        opts != nullptr && opts->kind() == pipeline::Value::Kind::Map) {
      for (const auto& [k, v] : opts->as_map())
        stage_input.emplace(k, v);
    }
    auto input_value = pipeline::Value::map(std::move(stage_input));

    std::map<std::string, std::shared_ptr<void>> empty_upstream;
    pipeline::DispatchContext dctx{*cap, input_value, empty_upstream};
    auto dr = ctx.dispatcher->dispatch(dctx);
    if (auto* derr = std::get_if<pipeline::DispatchError>(&dr)) {
      return ToolResult{pipeline::Value::null_value(),
                        "mesher dispatch failed",
                        ToolError{"DISPATCH_FAILED",
                                  derr->message,
                                  "inspect the plugin's logs or rerun with `souxmar plugin list` "
                                  "to confirm the capability is registered"}};
    }

    auto payload = std::get<pipeline::DispatchSuccess>(dr);
    const auto* so = static_cast<const pipeline::StageOutput*>(payload.get());
    if (!so || so->kind != pipeline::StageOutput::Kind::Mesh || !so->mesh) {
      return ToolResult{pipeline::Value::null_value(),
                        "mesher returned no mesh",
                        ToolError{"INTERNAL",
                                  "mesher dispatch returned an unexpected payload kind",
                                  "this indicates a plugin bug — the capability is registered but "
                                  "did not honor the mesher vtable contract"}};
    }

    // Hand the Mesh to the session so `solve` can pick it up.
    ctx.mesh_handle = so->mesh;

    const auto num_nodes = ctx.mesh_handle->num_nodes();
    const auto num_cells = ctx.mesh_handle->num_cells();

    std::map<std::string, pipeline::Value> out;
    out.emplace("capability_id", pipeline::Value::string(*cap));
    out.emplace("num_nodes", pipeline::Value::number(static_cast<double>(num_nodes)));
    out.emplace("num_cells", pipeline::Value::number(static_cast<double>(num_cells)));

    std::string summary = "mesh: " + std::to_string(num_nodes) + " nodes, "
                          + std::to_string(num_cells) + " cells (via " + *cap + ")";

    return ToolResult{pipeline::Value::map(std::move(out)), std::move(summary), std::nullopt};
  };
  return t;
}

}  // namespace souxmar::ai
