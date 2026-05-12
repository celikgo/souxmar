// SPDX-License-Identifier: Apache-2.0
//
// Tool: read_geometry_summary
//
// Returns counts (vertices/edges/faces/solids), bounding box, and named
// tag list for the current geometry. v1 reads from either an inline
// "geometry" input or, if absent, the session_state["geometry"] map a
// previous tool deposited. Sprint 6 swaps the v1 inline path for a real
// OpenCASCADE-backed read against ToolContext.geometry_handle.

#include "souxmar/ai/tool.h"
#include "souxmar/pipeline/value.h"

#include <map>
#include <string>
#include <vector>

namespace souxmar::ai {

namespace {

// Pull a Map from either the call inputs or the session_state.
// Returns nullptr if neither source has a usable map under `key`.
const pipeline::Value* geometry_map(const pipeline::Value& inputs,
                                    const pipeline::Value* session_state) {
  if (const auto* v = inputs.find("geometry");
      v != nullptr && v->kind() == pipeline::Value::Kind::Map) {
    return v;
  }
  if (session_state != nullptr && session_state->kind() == pipeline::Value::Kind::Map) {
    if (const auto* v = session_state->find("geometry");
        v != nullptr && v->kind() == pipeline::Value::Kind::Map) {
      return v;
    }
  }
  return nullptr;
}

}  // namespace

Tool make_read_geometry_summary_tool() {
  Tool t;
  t.name = "read_geometry_summary";
  t.description =
      "Inspect the current project's geometry. Returns vertex / edge / face / solid "
      "counts, the axis-aligned bounding box, and the list of named tags.";
  t.category = "Read";
  t.confirmation = Confirmation::Auto;
  t.input_schema_doc =
      "{geometry?: {num_vertices, num_edges, num_faces, num_solids, bbox: [x0,y0,z0,x1,y1,z1], "
      "tags?: [name]}}\n"
      "If `geometry` is absent the tool reads from session_state['geometry'].";
  t.output_schema_doc = "{num_vertices, num_edges, num_faces, num_solids, bbox, tags: [...] }";
  t.handler = [](const pipeline::Value& inputs, ToolContext& ctx) -> ToolResult {
    const auto* g = geometry_map(inputs, ctx.session_state);
    if (g == nullptr) {
      return ToolResult{pipeline::Value::null_value(),
                        "no geometry available",
                        ToolError{"NOT_AVAILABLE",
                                  "no geometry in inputs or session_state",
                                  "load a geometry first (Sprint 6: `reader.cad.step`) "
                                  "or pass an inline `geometry:` argument"}};
    }

    // Build a fresh Map with the documented output keys; missing fields
    // surface as Null rather than being silently dropped — the agent
    // sees exactly what was and wasn't known.
    std::map<std::string, pipeline::Value> out;
    for (const auto* key : {"num_vertices", "num_edges", "num_faces", "num_solids", "bbox"}) {
      if (const auto* v = g->find(key); v != nullptr) {
        out.emplace(key, *v);
      } else {
        out.emplace(key, pipeline::Value::null_value());
      }
    }
    if (const auto* tags = g->find("tags"); tags != nullptr) {
      out.emplace("tags", *tags);
    } else {
      out.emplace("tags", pipeline::Value::list({}));
    }

    // Compose a short summary the chat panel can render inline.
    auto count_or_q = [&](const char* key) -> std::string {
      const auto& v = out[key];
      return v.kind() == pipeline::Value::Kind::Number
                 ? std::to_string(static_cast<long long>(v.as_number()))
                 : std::string{"?"};
    };
    std::string summary = "geometry: " + count_or_q("num_vertices") + " v / "
                          + count_or_q("num_edges") + " e / " + count_or_q("num_faces") + " f / "
                          + count_or_q("num_solids") + " s";

    return ToolResult{pipeline::Value::map(std::move(out)), std::move(summary), std::nullopt};
  };
  return t;
}

}  // namespace souxmar::ai
