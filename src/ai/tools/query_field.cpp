// SPDX-License-Identifier: Apache-2.0
//
// Tool: query_field
//
// Reads the session's current field (set by a prior `solve` call) and
// returns aggregate statistics: min / max / mean / count, plus the
// field's location + kind labels. v1 aggregates flat across all stored
// values; tag-filtered aggregation requires the C++ Mesh accessors to
// answer "what cells have this tag", which is a Sprint 6 extension.

#include "souxmar/ai/tool.h"
#include "souxmar/core/field.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <string>

namespace souxmar::ai {

namespace {

const char* location_name(souxmar::core::FieldLocation l) {
  switch (l) {
    case souxmar::core::FieldLocation::Nodal:
      return "nodal";
    case souxmar::core::FieldLocation::Cell:
      return "cell";
    case souxmar::core::FieldLocation::Face:
      return "face";
    case souxmar::core::FieldLocation::GaussPoint:
      return "gauss";
  }
  return "unknown";
}

const char* kind_name(souxmar::core::FieldKind k) {
  switch (k) {
    case souxmar::core::FieldKind::Scalar:
      return "scalar";
    case souxmar::core::FieldKind::Vector:
      return "vector";
    case souxmar::core::FieldKind::Tensor:
      return "tensor";
  }
  return "unknown";
}

}  // namespace

Tool make_query_field_tool() {
  Tool t;
  t.name = "query_field";
  t.description =
      "Return min / max / mean over the current field (set by a prior "
      "`solve` call). Reports the field's location + kind metadata so "
      "the agent can reason about magnitude (scalar) vs. component "
      "(vector / tensor) interpretation.";
  t.category = "Read";
  t.confirmation = Confirmation::Auto;
  t.input_schema_doc =
      "{}    # v1 has no input; future revisions accept {tag: string} "
      "for tag-filtered aggregation over cell-located fields.";
  t.output_schema_doc = "{min, max, mean, count, location, kind, num_components}";
  t.handler = [](const pipeline::Value& /*inputs*/, ToolContext& ctx) -> ToolResult {
    if (!ctx.field_handle) {
      return ToolResult{pipeline::Value::null_value(),
                        "no field available — call `solve` first",
                        ToolError{"PRECONDITION_FAILED",
                                  "query_field requires a field; ToolContext.field_handle is null",
                                  "invoke the `solve` tool before `query_field` so the session "
                                  "carries a field handle"}};
    }
    const auto data = ctx.field_handle->data();
    if (data.empty()) {
      return ToolResult{pipeline::Value::null_value(),
                        "field is empty",
                        ToolError{"NOT_AVAILABLE", "the current field has zero stored values"}};
    }

    double min_v = std::numeric_limits<double>::infinity();
    double max_v = -std::numeric_limits<double>::infinity();
    double sum = 0.0;
    std::size_t finite_count = 0;
    for (const double v : data) {
      // Drop NaNs out of the aggregation so a single bad cell doesn't
      // wipe out the summary. We surface the count separately so the
      // caller can tell when this happened.
      if (!std::isfinite(v))
        continue;
      min_v = std::min(min_v, v);
      max_v = std::max(max_v, v);
      sum += v;
      ++finite_count;
    }
    if (finite_count == 0) {
      return ToolResult{pipeline::Value::null_value(),
                        "field contains only non-finite values",
                        ToolError{"NOT_AVAILABLE",
                                  "every value in the current field was NaN or infinite",
                                  "inspect the solver's options + boundary conditions — this is "
                                  "usually an ill-posed problem rather than a query_field issue"}};
    }
    const double mean = sum / static_cast<double>(finite_count);

    std::map<std::string, pipeline::Value> out;
    out.emplace("min", pipeline::Value::number(min_v));
    out.emplace("max", pipeline::Value::number(max_v));
    out.emplace("mean", pipeline::Value::number(mean));
    out.emplace("count", pipeline::Value::number(static_cast<double>(finite_count)));
    out.emplace("total", pipeline::Value::number(static_cast<double>(data.size())));
    out.emplace("location", pipeline::Value::string(location_name(ctx.field_handle->location())));
    out.emplace("kind", pipeline::Value::string(kind_name(ctx.field_handle->kind())));
    out.emplace("num_components",
                pipeline::Value::number(static_cast<double>(ctx.field_handle->components())));

    // Build a one-line summary the chat UI can render: "field: min=<>, max=<>, mean=<>".
    char buf[256];
    std::snprintf(buf,
                  sizeof(buf),
                  "field: min=%.6g  max=%.6g  mean=%.6g  (%zu finite of %zu)",
                  min_v,
                  max_v,
                  mean,
                  finite_count,
                  data.size());
    return ToolResult{pipeline::Value::map(std::move(out)), std::string{buf}, std::nullopt};
  };
  return t;
}

}  // namespace souxmar::ai
