// SPDX-License-Identifier: Apache-2.0
//
// Tool: list_plugins
//
// Read-only inventory of every capability currently registered with the
// host. Used by the agent before calling `mesh` / `solve` /
// `compute_field` / `export_results` to know what's available.
//
// Per docs/AI_INTEGRATION.md confirmation tier: Auto — pure read.
//
// Output groups capabilities by their declared namespace
// (mesher / solver / writer / postproc / ...) so the chat UI and the
// model can both consume the same shape.

#include "souxmar/ai/tool.h"
#include "souxmar/plugin/manifest.h"
#include "souxmar/plugin/registry.h"

#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace souxmar::ai {

namespace {

std::string_view kind_token(plugin::CapabilityKind k) noexcept {
  switch (k) {
    case plugin::CapabilityKind::Mesher:
      return "mesher";
    case plugin::CapabilityKind::Solver:
      return "solver";
    case plugin::CapabilityKind::Writer:
      return "writer";
    case plugin::CapabilityKind::Postproc:
      return "postproc";
    case plugin::CapabilityKind::Reader:
      return "reader";
  }
  return "unknown";
}

std::string_view threading_token(plugin::ThreadingModel m) noexcept {
  return plugin::to_string(m);
}

}  // namespace

Tool make_list_plugins_tool() {
  Tool t;
  t.name = "list_plugins";
  t.description =
      "Enumerate every capability registered with the host registry. "
      "Returns the capability id, kind, owning plugin id, advertised "
      "ABI version, and declared threading model. Use this before "
      "calling `mesh` / `solve` / `compute_field` / `export_results` "
      "to find a capability the host actually has loaded.";
  t.category = "Read";
  t.confirmation = Confirmation::Auto;
  t.input_schema_doc =
      "{namespace?: string}   # optional filter: 'mesher' | 'writer' | 'solver' | 'postproc'";
  t.output_schema_doc =
      "{capabilities: [{id, kind, plugin_id, abi_version, threading}, ...],\n"
      " count_total, count_by_kind: {<kind>: number, ...}}";
  t.handler = [](const pipeline::Value& inputs, ToolContext& ctx) -> ToolResult {
    if (ctx.registry == nullptr) {
      return ToolResult{pipeline::Value::null_value(),
                        "host registry not wired into ToolContext",
                        ToolError{"INTERNAL", "list_plugins requires ToolContext.registry"}};
    }

    std::string filter_ns;
    if (inputs.kind() == pipeline::Value::Kind::Map) {
      if (const auto* f = inputs.find("namespace");
          f != nullptr && f->kind() == pipeline::Value::Kind::String) {
        filter_ns = std::string(f->as_string());
      }
    }

    const auto ids = filter_ns.empty() ? ctx.registry->list_capabilities()
                                       : ctx.registry->list_capabilities_in_namespace(filter_ns);

    std::vector<pipeline::Value> rows;
    std::map<std::string, std::size_t> by_kind;
    rows.reserve(ids.size());
    for (const auto& id : ids) {
      const auto* entry = ctx.registry->find(id);
      if (!entry)
        continue;  // race-free here (we hold the registry handle)
      const auto kind = std::string(kind_token(entry->kind));
      ++by_kind[kind];

      std::map<std::string, pipeline::Value> row;
      row.emplace("id", pipeline::Value::string(entry->id));
      row.emplace("kind", pipeline::Value::string(kind));
      row.emplace("plugin_id", pipeline::Value::string(entry->plugin_id));
      row.emplace("abi_version", pipeline::Value::number(static_cast<double>(entry->abi_version)));
      row.emplace("threading",
                  pipeline::Value::string(std::string(threading_token(entry->threading))));
      rows.push_back(pipeline::Value::map(std::move(row)));
    }

    std::map<std::string, pipeline::Value> kind_counts;
    for (const auto& [k, n] : by_kind) {
      kind_counts.emplace(k, pipeline::Value::number(static_cast<double>(n)));
    }

    std::map<std::string, pipeline::Value> out;
    out.emplace("capabilities", pipeline::Value::list(std::move(rows)));
    out.emplace("count_total", pipeline::Value::number(static_cast<double>(ids.size())));
    out.emplace("count_by_kind", pipeline::Value::map(std::move(kind_counts)));
    if (!filter_ns.empty()) {
      out.emplace("filter_namespace", pipeline::Value::string(filter_ns));
    }

    std::string summary;
    if (filter_ns.empty()) {
      summary = "registry: " + std::to_string(ids.size()) + " capabilities total";
    } else {
      summary = "namespace " + filter_ns + ": " + std::to_string(ids.size()) + " capabilities";
    }
    return ToolResult{pipeline::Value::map(std::move(out)), std::move(summary), std::nullopt};
  };
  return t;
}

}  // namespace souxmar::ai
