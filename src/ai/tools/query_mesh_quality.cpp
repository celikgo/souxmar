// SPDX-License-Identifier: Apache-2.0
//
// Tool: query_mesh_quality
//
// Sprint 6 push 1. Runs `postproc.mesh_quality` against the session
// mesh (or accepts a precomputed mesh-quality Field via the session's
// field handle), then summarises the result through
// souxmar::core::quality::summarise — per-metric min/max/mean plus
// threshold-flagged counts (inverted cells, slivers, extreme aspect).
//
// Confirmation::Auto — it's a read-only inspection. The implicit
// postproc dispatch is bounded (single mesh) and idempotent.

#include "souxmar/ai/tool.h"

#include "souxmar/core/field.h"
#include "souxmar/core/mesh.h"
#include "souxmar/core/mesh_quality.h"
#include "souxmar/pipeline/registry_dispatcher.h"
#include "souxmar/pipeline/runner.h"
#include "souxmar/pipeline/value.h"
#include "souxmar/plugin/registry.h"

#include <cstddef>
#include <cstdio>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace souxmar::ai {

namespace {

constexpr const char* kCapabilityId = "postproc.mesh_quality";

// Try to reuse ctx.field_handle when it already carries a 3-component
// Cell-located Field — that's the shape `postproc.mesh_quality` emits,
// so an agent can pre-stash it (or chain after a prior call) and avoid
// a re-dispatch. Returns nullptr if the handle isn't usable here.
const core::Field* reuse_existing_quality_field(const ToolContext& ctx) noexcept {
  const auto* f = ctx.field_handle.get();
  if (!f) return nullptr;
  if (f->location() != core::FieldLocation::Cell) return nullptr;
  if (f->kind()     != core::FieldKind::Vector)   return nullptr;
  if (f->components() < core::quality::kNumMetrics) return nullptr;
  if (f->num_time_steps() != 1) return nullptr;
  return f;
}

pipeline::Value stats_to_value(const core::quality::Stats& s) {
  std::map<std::string, pipeline::Value> m;
  if (s.finite_count > 0) {
    m.emplace("min",  pipeline::Value::number(s.min));
    m.emplace("max",  pipeline::Value::number(s.max));
    m.emplace("mean", pipeline::Value::number(s.mean));
  }
  m.emplace("finite", pipeline::Value::number(static_cast<double>(s.finite_count)));
  m.emplace("total",  pipeline::Value::number(static_cast<double>(s.total_count)));
  return pipeline::Value::map(std::move(m));
}

std::optional<std::shared_ptr<core::Field>>
dispatch_quality(ToolContext& ctx, ToolError& err) {
  if (ctx.registry == nullptr || ctx.dispatcher == nullptr) {
    err = {"INTERNAL",
           "query_mesh_quality requires ToolContext.registry + ToolContext.dispatcher"};
    return std::nullopt;
  }
  if (ctx.registry->find_postproc(kCapabilityId) == nullptr) {
    err = {"PLUGIN_NOT_FOUND",
           std::string("no postproc capability registered as '") + kCapabilityId + "'",
           "build / install the in-tree mesh-quality plugin, "
           "or list discovered plugins with `souxmar plugin list`"};
    return std::nullopt;
  }
  if (!ctx.mesh_handle) {
    err = {"PRECONDITION_FAILED",
           "query_mesh_quality requires a mesh; ToolContext.mesh_handle is null",
           "invoke the `mesh` tool before `query_mesh_quality`"};
    return std::nullopt;
  }

  auto mesh_so  = std::make_shared<pipeline::StageOutput>();
  mesh_so->kind = pipeline::StageOutput::Kind::Mesh;
  mesh_so->mesh = ctx.mesh_handle;

  std::map<std::string, std::shared_ptr<void>> upstream;
  upstream.emplace("__session_mesh__", std::static_pointer_cast<void>(mesh_so));

  std::map<std::string, pipeline::Value> stage_input;
  stage_input.emplace("mesh",  pipeline::Value::stage_ref("__session_mesh__"));
  auto input_value = pipeline::Value::map(std::move(stage_input));

  pipeline::DispatchContext dctx{kCapabilityId, input_value, upstream};
  auto dr = ctx.dispatcher->dispatch(dctx);
  if (auto* derr = std::get_if<pipeline::DispatchError>(&dr)) {
    err = {"DISPATCH_FAILED", derr->message};
    return std::nullopt;
  }
  auto payload = std::get<pipeline::DispatchSuccess>(dr);
  const auto* so = static_cast<const pipeline::StageOutput*>(payload.get());
  if (!so || so->kind != pipeline::StageOutput::Kind::Field || !so->field) {
    err = {"INTERNAL",
           "postproc.mesh_quality returned an unexpected payload kind"};
    return std::nullopt;
  }
  return so->field;
}

}  // namespace

Tool make_query_mesh_quality_tool() {
  Tool t;
  t.name             = "query_mesh_quality";
  t.description      =
      "Summarise mesh quality (signed volume, edge ratio, minimum "
      "dihedral angle) across the current mesh. Runs the "
      "`postproc.mesh_quality` plugin against ToolContext.mesh_handle "
      "and reports per-metric min/max/mean plus the count of cells "
      "flagged by simple thresholds (inverted, sliver, extreme aspect).";
  t.category         = "Read";
  t.confirmation     = Confirmation::Auto;
  t.input_schema_doc =
      "{}    # v1 takes no input; future revisions accept "
      "{thresholds: {sliver_deg, extreme_aspect, ...}} to override defaults.";
  t.output_schema_doc =
      "{metrics: {<metric_name>: {min, max, mean, finite, total}},\n"
      " flags:   {cells_inverted, cells_sliver_dihedral, cells_extreme_aspect, cells_unsupported},\n"
      " num_cells, source}";
  t.handler = [](const pipeline::Value& /*inputs*/, ToolContext& ctx) -> ToolResult {
    // 1. Try to reuse a quality field already on the session.
    const core::Field* qfield = reuse_existing_quality_field(ctx);
    std::shared_ptr<core::Field> dispatched;  // keeps ownership when we dispatched
    std::string source = "session_field";

    if (qfield == nullptr) {
      ToolError err{};
      auto fopt = dispatch_quality(ctx, err);
      if (!fopt) {
        return ToolResult{
            pipeline::Value::null_value(),
            err.message,
            err};
      }
      dispatched = *std::move(fopt);
      qfield     = dispatched.get();
      source     = "dispatched";
      // Stash the freshly-computed quality field on the session so a
      // follow-up tool can reuse it without re-dispatching.
      ctx.field_handle = dispatched;
    }

    const auto data       = qfield->data();
    const auto num_cells  = qfield->count();
    const auto report     = core::quality::summarise(data, num_cells);

    std::map<std::string, pipeline::Value> metrics;
    for (std::size_t m = 0; m < core::quality::kNumMetrics; ++m) {
      metrics.emplace(
          core::quality::metric_name(static_cast<core::quality::Metric>(m)),
          stats_to_value(report.per_metric[m]));
    }

    std::map<std::string, pipeline::Value> flags;
    flags.emplace("cells_inverted",
                  pipeline::Value::number(static_cast<double>(report.cells_inverted)));
    flags.emplace("cells_sliver_dihedral",
                  pipeline::Value::number(static_cast<double>(report.cells_sliver_dihedral)));
    flags.emplace("cells_extreme_aspect",
                  pipeline::Value::number(static_cast<double>(report.cells_extreme_aspect)));
    flags.emplace("cells_unsupported",
                  pipeline::Value::number(static_cast<double>(report.cells_unsupported)));

    std::map<std::string, pipeline::Value> out;
    out.emplace("metrics",   pipeline::Value::map(std::move(metrics)));
    out.emplace("flags",     pipeline::Value::map(std::move(flags)));
    out.emplace("num_cells", pipeline::Value::number(static_cast<double>(num_cells)));
    out.emplace("source",    pipeline::Value::string(source));

    const auto& vol = report.per_metric[
        static_cast<std::size_t>(core::quality::Metric::SignedVolume)];
    const auto& er  = report.per_metric[
        static_cast<std::size_t>(core::quality::Metric::EdgeRatio)];
    const auto& dih = report.per_metric[
        static_cast<std::size_t>(core::quality::Metric::MinDihedralDeg)];

    char buf[384];
    std::snprintf(buf, sizeof(buf),
                  "mesh quality (%zu cells): "
                  "volume %.4g..%.4g  edge_ratio %.3g..%.3g  min_dihedral %.2f..%.2f deg  "
                  "[%zu inverted, %zu sliver, %zu extreme_aspect, %zu unsupported]",
                  num_cells,
                  vol.min, vol.max, er.min, er.max, dih.min, dih.max,
                  report.cells_inverted, report.cells_sliver_dihedral,
                  report.cells_extreme_aspect, report.cells_unsupported);

    return ToolResult{
        pipeline::Value::map(std::move(out)),
        std::string{buf},
        std::nullopt};
  };
  return t;
}

}  // namespace souxmar::ai
