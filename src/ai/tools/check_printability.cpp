// SPDX-License-Identifier: Apache-2.0
//
// Tool: check_printability
//
// Tool 20 of the ADR-0010 additive ratchet. Runs the DfAM capability
// `solver.am.printability` against the session mesh and turns its
// per-cell field into an answer the model can act on: the score
// distribution, how many cells are below the acceptance threshold, and
// which limiting factor dominates.
//
// The dispatch path is the `query_mesh_quality` path verbatim — wrap
// ToolContext.mesh_handle as a one-off StageOutput under the synthetic
// upstream id `__session_mesh__`, reference it as `mesh: {from: ...}`,
// and hand the bag to ToolContext.dispatcher. Consequently the error
// surface is the same shape too: INTERNAL when the host services are not
// wired, PLUGIN_NOT_FOUND when the capability is not registered,
// PRECONDITION_FAILED when there is no mesh, INVALID_ARGUMENT for a
// malformed input — each with a suggestion the model can execute.
//
// Field contract (manufacturing capability contract 3.10): cell-located
// VECTOR field named "printability", 1 time step, components
//   [0] printability_score      0..1, 1 = trivially printable
//   [1] limiting_factor_code    0 none / 1 overhang / 2 thin wall /
//                               3 build-volume fit / 4 aspect ratio /
//                               5 corrosion allowance
//   [2] wall_thickness_proxy_m
//
// Confirmation::Auto. The contract's `Confirmation::None` maps to `Auto`,
// tool.h's no-prompt tier. Justification: read-only inspection. The
// implicit solver dispatch is bounded (one mesh, closed-form per-cell
// arithmetic), idempotent, writes no file and reaches no network — the
// same reasoning that keeps query_mesh_quality at Auto.

#include "souxmar/ai/tool.h"
#include "souxmar/core/field.h"
#include "souxmar/core/mesh.h"
#include "souxmar/pipeline/registry_dispatcher.h"
#include "souxmar/pipeline/runner.h"
#include "souxmar/pipeline/value.h"
#include "souxmar/plugin/registry.h"

#include <cstddef>
#include <cstdio>
#include <exception>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace souxmar::ai {

namespace {

constexpr const char* kCapabilityId = "solver.am.printability";
constexpr const char* kFieldName = "printability";
constexpr std::size_t kComponents = 3;

// Limiting-factor code -> name. Index is the code; the order is frozen
// by the capability contract, so a plain array is the right shape.
constexpr const char* kLimitingFactorNames[] = {
    "none", "overhang", "thin_wall", "build_volume_fit", "aspect_ratio", "corrosion_allowance",
};
constexpr std::size_t kNumLimitingFactors =
    sizeof(kLimitingFactorNames) / sizeof(kLimitingFactorNames[0]);

// Inputs forwarded verbatim to the solver. Anything else in the input
// map is tool-local (or ignored) and never reaches the plugin.
constexpr const char* kForwardedNumbers[] = {
    "overhang_threshold_deg", "min_wall_thickness", "corrosion_allowance", "layer_height",
};
constexpr const char* kForwardedVec3[] = {"build_direction", "machine_build_volume"};

const pipeline::Value* find(const pipeline::Value& v, const char* key) {
  return v.kind() == pipeline::Value::Kind::Map ? v.find(key) : nullptr;
}

ToolResult invalid(const std::string& summary, const std::string& msg, const std::string& hint) {
  return ToolResult{
      pipeline::Value::null_value(), summary, ToolError{"INVALID_ARGUMENT", msg, hint}};
}

// A session field is reusable only when its *name* matches — a bare
// 3-component cell field could equally be melt_pool / overhang /
// buildtime, and silently mis-reading those would be worse than
// re-dispatching. The caller must also have supplied no solver
// overrides; see the `has_overrides` gate below.
const core::Field* reuse_session_field(const ToolContext& ctx) noexcept {
  const auto* f = ctx.field_handle.get();
  if (f == nullptr)
    return nullptr;
  if (f->name() != kFieldName)
    return nullptr;
  if (f->location() != core::FieldLocation::Cell)
    return nullptr;
  if (f->components() < kComponents || f->num_time_steps() != 1)
    return nullptr;
  return f;
}

pipeline::Value stats(double min_v, double max_v, double mean_v, bool any) {
  std::map<std::string, pipeline::Value> m;
  if (any) {
    m.emplace("min", pipeline::Value::number(min_v));
    m.emplace("max", pipeline::Value::number(max_v));
    m.emplace("mean", pipeline::Value::number(mean_v));
  }
  return pipeline::Value::map(std::move(m));
}

ToolResult run(const pipeline::Value& inputs, ToolContext& ctx) {
  if (inputs.kind() != pipeline::Value::Kind::Map && inputs.kind() != pipeline::Value::Kind::Null) {
    return invalid("input must be a map",
                   "check_printability input must be a map (or null for all defaults)",
                   "{overhang_threshold_deg: 45, min_wall_thickness: 0.0005}");
  }

  // ---- tool-local acceptance threshold ----
  double score_threshold = 0.5;  // documented heuristic midpoint
  if (const auto* st = find(inputs, "score_threshold")) {
    if (st->kind() != pipeline::Value::Kind::Number) {
      return invalid("score_threshold must be a number",
                     "`score_threshold` must be a number in [0, 1] when set",
                     "0.5 is the default acceptance line");
    }
    score_threshold = st->as_number();
    if (score_threshold < 0.0 || score_threshold > 1.0) {
      return invalid("score_threshold out of range",
                     "`score_threshold` must be in [0, 1]",
                     "printability_score is normalised: 1 = trivially printable");
    }
  }

  // ---- validate + collect the forwarded solver inputs ----
  std::map<std::string, pipeline::Value> stage_input;
  for (const auto* key : kForwardedNumbers) {
    const auto* v = find(inputs, key);
    if (v == nullptr)
      continue;
    if (v->kind() != pipeline::Value::Kind::Number) {
      return invalid(std::string(key) + " must be a number",
                     std::string("`") + key + "` must be a number when set",
                     "all lengths are metres, all angles degrees");
    }
    stage_input.emplace(key, *v);
  }
  for (const auto* key : kForwardedVec3) {
    const auto* v = find(inputs, key);
    if (v == nullptr)
      continue;
    if (v->kind() != pipeline::Value::Kind::List || v->as_list().size() != 3) {
      return invalid(std::string(key) + " must be 3 numbers",
                     std::string("`") + key + "` must be a list of three numbers when set",
                     "e.g. build_direction: [0, 0, 1]");
    }
    for (const auto& c : v->as_list()) {
      if (c.kind() != pipeline::Value::Kind::Number) {
        return invalid(std::string(key) + " must be 3 numbers",
                       std::string("every entry of `") + key + "` must be a number",
                       "e.g. machine_build_volume: [0.25, 0.25, 0.3]");
      }
    }
    stage_input.emplace(key, *v);
  }

  // ---- resolve the field: reuse or dispatch ----
  // Reuse only when the caller supplied no solver inputs. With an
  // override in hand, handing back a field computed under the *previous*
  // parameters would answer a question nobody asked.
  const bool has_overrides = !stage_input.empty();
  const core::Field* field = has_overrides ? nullptr : reuse_session_field(ctx);
  std::shared_ptr<core::Field> dispatched;
  std::string source = "session_field";

  if (field == nullptr) {
    if (ctx.registry == nullptr || ctx.dispatcher == nullptr) {
      return ToolResult{
          pipeline::Value::null_value(),
          "host registry / dispatcher not wired into ToolContext",
          ToolError{"INTERNAL",
                    "check_printability requires ToolContext.registry + ToolContext.dispatcher",
                    "run the tool through the CLI / desktop agent session, which wires both"}};
    }
    if (ctx.registry->find_solver(kCapabilityId) == nullptr) {
      return ToolResult{pipeline::Value::null_value(),
                        std::string("no solver capability registered as '") + kCapabilityId + "'",
                        ToolError{"PLUGIN_NOT_FOUND",
                                  std::string("no solver capability registered as '")
                                      + kCapabilityId + "'",
                                  "build / install the in-tree am-manufacturability plugin, or "
                                  "call `list_plugins` to see what is available"}};
    }
    if (!ctx.mesh_handle) {
      return ToolResult{
          pipeline::Value::null_value(),
          "no mesh available — call `mesh` first",
          ToolError{"PRECONDITION_FAILED",
                    "check_printability requires a mesh; ToolContext.mesh_handle is null",
                    "invoke the `mesh` tool (e.g. capability mesher.am.layered) before "
                    "check_printability"}};
    }

    auto mesh_so = std::make_shared<pipeline::StageOutput>();
    mesh_so->kind = pipeline::StageOutput::Kind::Mesh;
    mesh_so->mesh = ctx.mesh_handle;
    std::map<std::string, std::shared_ptr<void>> upstream;
    upstream.emplace("__session_mesh__", std::static_pointer_cast<void>(mesh_so));

    stage_input.emplace("mesh", pipeline::Value::stage_ref("__session_mesh__"));
    auto input_value = pipeline::Value::map(std::move(stage_input));

    pipeline::DispatchContext dctx{kCapabilityId, input_value, upstream};
    auto dr = ctx.dispatcher->dispatch(dctx);
    if (auto* derr = std::get_if<pipeline::DispatchError>(&dr)) {
      return ToolResult{pipeline::Value::null_value(),
                        "printability dispatch failed",
                        ToolError{"DISPATCH_FAILED", derr->message}};
    }
    auto payload = std::get<pipeline::DispatchSuccess>(dr);
    const auto* so = static_cast<const pipeline::StageOutput*>(payload.get());
    if (so == nullptr || so->kind != pipeline::StageOutput::Kind::Field || !so->field) {
      return ToolResult{
          pipeline::Value::null_value(),
          "printability solver returned no field",
          ToolError{"INTERNAL",
                    std::string(kCapabilityId) + " returned an unexpected payload kind"}};
    }
    dispatched = so->field;
    field = dispatched.get();
    if (field->components() < kComponents || field->count() == 0) {
      return ToolResult{
          pipeline::Value::null_value(),
          "printability field has the wrong shape",
          ToolError{"INTERNAL",
                    std::string(kCapabilityId)
                        + " returned a field with fewer than 3 components or no cells",
                    "the capability contract specifies a 3-component cell field "
                    "[score, limiting_factor_code, wall_thickness_proxy_m]"}};
    }
    // Stash it so a follow-up tool (or a second call) skips the dispatch.
    ctx.field_handle = dispatched;
    source = "dispatched";
  }

  // ---- reduce, in index order (determinism gate) ----
  const auto data = field->data();
  const auto num_cells = field->count();
  const auto comps = static_cast<std::size_t>(field->components());

  double score_min = 0.0;
  double score_max = 0.0;
  double score_sum = 0.0;
  double wall_min = 0.0;
  double wall_max = 0.0;
  double wall_sum = 0.0;
  std::size_t below = 0;
  std::vector<std::size_t> factor_counts(kNumLimitingFactors, 0);
  std::size_t unknown_factor_cells = 0;

  for (std::size_t c = 0; c < num_cells; ++c) {
    const std::size_t base = c * comps;
    if (base + 2 >= data.size())
      break;
    const double score = data[base + 0];
    const double code = data[base + 1];
    const double wall = data[base + 2];
    if (c == 0) {
      score_min = score_max = score;
      wall_min = wall_max = wall;
    } else {
      score_min = score < score_min ? score : score_min;
      score_max = score > score_max ? score : score_max;
      wall_min = wall < wall_min ? wall : wall_min;
      wall_max = wall > wall_max ? wall : wall_max;
    }
    score_sum += score;
    wall_sum += wall;
    if (score < score_threshold)
      ++below;
    // Codes arrive as doubles; a non-integral / out-of-range code is a
    // plugin bug, counted rather than silently folded into "none".
    if (code >= 0.0 && code < static_cast<double>(kNumLimitingFactors)) {
      ++factor_counts[static_cast<std::size_t>(code)];
    } else {
      ++unknown_factor_cells;
    }
  }

  const double inv_n = num_cells > 0 ? 1.0 / static_cast<double>(num_cells) : 0.0;

  std::map<std::string, pipeline::Value> factors;
  for (std::size_t i = 0; i < kNumLimitingFactors; ++i) {
    factors.emplace(kLimitingFactorNames[i],
                    pipeline::Value::number(static_cast<double>(factor_counts[i])));
  }
  if (unknown_factor_cells > 0) {
    factors.emplace("unrecognised_code",
                    pipeline::Value::number(static_cast<double>(unknown_factor_cells)));
  }

  // Dominant blocker: the highest-count factor other than "none". Ties
  // break on the lower code so the answer is reproducible.
  std::size_t dominant = 0;
  std::size_t dominant_count = 0;
  for (std::size_t i = 1; i < kNumLimitingFactors; ++i) {
    if (factor_counts[i] > dominant_count) {
      dominant = i;
      dominant_count = factor_counts[i];
    }
  }
  const char* dominant_name = dominant_count > 0 ? kLimitingFactorNames[dominant] : "none";

  std::map<std::string, pipeline::Value> out;
  out.emplace("capability_id", pipeline::Value::string(kCapabilityId));
  out.emplace("source", pipeline::Value::string(source));
  out.emplace("num_cells", pipeline::Value::number(static_cast<double>(num_cells)));
  out.emplace("score", stats(score_min, score_max, score_sum * inv_n, num_cells > 0));
  out.emplace("wall_thickness_proxy_m", stats(wall_min, wall_max, wall_sum * inv_n, num_cells > 0));
  out.emplace("score_threshold", pipeline::Value::number(score_threshold));
  out.emplace("cells_below_threshold", pipeline::Value::number(static_cast<double>(below)));
  out.emplace("limiting_factors", pipeline::Value::map(std::move(factors)));
  out.emplace("dominant_limiting_factor", pipeline::Value::string(dominant_name));
  out.emplace("printable", pipeline::Value::boolean(below == 0));
  out.emplace("notes",
              pipeline::Value::string(
                  "printability_score is a documented weighted heuristic (overhang, wall "
                  "thickness, build-volume fit, aspect ratio, corrosion allowance), not a "
                  "guarantee that the part will build."));

  char buf[384];
  std::snprintf(buf,
                sizeof(buf),
                "printability (%zu cells, %s): score %.3f..%.3f mean %.3f  "
                "[%zu cell(s) below %.2f, dominant blocker: %s]",
                num_cells,
                source.c_str(),
                score_min,
                score_max,
                score_sum * inv_n,
                below,
                score_threshold,
                dominant_name);

  return ToolResult{pipeline::Value::map(std::move(out)), std::string{buf}, std::nullopt};
}

}  // namespace

Tool make_check_printability_tool() {
  Tool t;
  t.name = "check_printability";
  t.description =
      "Run the DfAM printability check (`solver.am.printability`) against the current mesh "
      "and summarise the blockers: score distribution, how many cells fall below the "
      "acceptance threshold, and which limiting factor dominates (overhang / thin wall / "
      "build-volume fit / aspect ratio / corrosion allowance). Read-only.";
  t.category = "Mesh";
  t.confirmation = Confirmation::Auto;
  t.input_schema_doc =
      "{score_threshold?: number,            # 0..1 acceptance line, default 0.5 (tool-local)\n"
      " build_direction?: [x, y, z],         # forwarded to the solver, default [0, 0, 1]\n"
      " overhang_threshold_deg?: number,     # default 45\n"
      " min_wall_thickness?: number,         # m, default 5e-4\n"
      " machine_build_volume?: [x, y, z],    # m, default [0.25, 0.25, 0.3]\n"
      " corrosion_allowance?: number,        # m, default 0\n"
      " layer_height?: number                # m, default 1e-3\n"
      "}";
  t.output_schema_doc =
      "{capability_id, source: 'dispatched' | 'session_field', num_cells,\n"
      " score: {min, max, mean}, wall_thickness_proxy_m: {min, max, mean},\n"
      " score_threshold, cells_below_threshold,\n"
      " limiting_factors: {none, overhang, thin_wall, build_volume_fit, aspect_ratio,\n"
      "                    corrosion_allowance},\n"
      " dominant_limiting_factor: string, printable: bool, notes: string}";
  t.handler = [](const pipeline::Value& inputs, ToolContext& ctx) -> ToolResult {
    try {
      return run(inputs, ctx);
    } catch (const std::exception& e) {
      return ToolResult{pipeline::Value::null_value(),
                        "check_printability failed internally",
                        ToolError{"INTERNAL", std::string("check_printability: ") + e.what()}};
    } catch (...) {
      return ToolResult{pipeline::Value::null_value(),
                        "check_printability failed internally",
                        ToolError{"INTERNAL", "check_printability: unknown exception"}};
    }
  };
  return t;
}

}  // namespace souxmar::ai
