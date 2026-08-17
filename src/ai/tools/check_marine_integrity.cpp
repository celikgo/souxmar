// SPDX-License-Identifier: Apache-2.0
//
// Tool: check_marine_integrity
//
// Tool 24 of the ADR-0010 additive ratchet. Advisory summary of the two
// marine integrity capabilities against the session mesh:
//   `solver.marine.hull_collapse` -> collapse pressure, margin over the
//        factored design pressure, governing failure mode
//   `solver.marine.corrosion`     -> thickness loss over the service
//        life, pitting risk, galvanic risk
//
// The dispatch path is the `query_mesh_quality` path verbatim (session
// mesh wrapped as a one-off StageOutput under `__session_mesh__`,
// referenced as `mesh: {from: ...}`, handed to ToolContext.dispatcher), so
// the error surface has the same shape: INTERNAL when the host services
// are not wired, PLUGIN_NOT_FOUND when a requested capability is not
// registered, PRECONDITION_FAILED when there is no mesh,
// INVALID_ARGUMENT for a malformed input — each with a suggestion the
// model can act on.
//
// Field contracts (manufacturing capability contract 3.16 / 3.17), both
// cell-located 3-component fields with one time step:
//   "collapse_margin" [0] collapse_pressure_Pa
//                     [1] margin (collapse / factored design pressure)
//                     [2] governing_mode_code  0 interframe elastic
//                         instability / 1 membrane yield / 2 general
//                         instability / 3 sphere elastic buckling
//   "corrosion"       [0] thickness_loss_mm over the service life
//                     [1] pitting_risk  0..1
//                     [2] galvanic_risk 0..1
//
// What this is NOT: souxmar is not a classification society. The collapse
// numbers are preliminary sizing from closed-form shell formulae with
// documented knockdowns, and the corrosion rates are indicative
// literature values. Neither is a design value and neither constitutes
// approval of anything.
//
// Confirmation::Auto (the contract's `Confirmation::None` maps onto
// tool.h's no-prompt tier). Justification: read-only inspection. Both
// dispatches are bounded closed-form evaluations over one mesh,
// idempotent, no file, no network.

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
#include <string_view>
#include <utility>
#include <vector>

namespace souxmar::ai {

namespace {

constexpr const char* kCollapseCapability = "solver.marine.hull_collapse";
constexpr const char* kCorrosionCapability = "solver.marine.corrosion";
constexpr std::size_t kComponents = 3;

// Documented advisory thresholds. They gate the `ok` flag and the
// advisory strings; they are not code requirements.
constexpr double kMinAcceptableMargin = 1.0;  // collapse / factored design pressure
constexpr double kRiskAdvisoryLevel = 0.5;    // pitting / galvanic risk, 0..1

constexpr const char* kGoverningModeNames[] = {
    "interframe_elastic_instability",
    "membrane_yield",
    "general_instability",
    "sphere_elastic_buckling",
};
constexpr std::size_t kNumGoverningModes =
    sizeof(kGoverningModeNames) / sizeof(kGoverningModeNames[0]);

// Tool-local keys: everything else in the input map is forwarded to the
// plugins verbatim. The capability contract states that unknown keys are
// ignored, so one forwarded bag can serve both solvers.
bool is_tool_local(std::string_view key) {
  return key == "checks" || key == "mesh" || key == "field";
}

const pipeline::Value* find(const pipeline::Value& v, const char* key) {
  return v.kind() == pipeline::Value::Kind::Map ? v.find(key) : nullptr;
}

ToolResult invalid(const std::string& summary, const std::string& msg, const std::string& hint) {
  return ToolResult{
      pipeline::Value::null_value(), summary, ToolError{"INVALID_ARGUMENT", msg, hint}};
}

struct Stats {
  double min = 0.0;
  double max = 0.0;
  double mean = 0.0;
  bool any = false;
};

pipeline::Value stats_value(const Stats& s) {
  std::map<std::string, pipeline::Value> m;
  if (s.any) {
    m.emplace("min", pipeline::Value::number(s.min));
    m.emplace("max", pipeline::Value::number(s.max));
    m.emplace("mean", pipeline::Value::number(s.mean));
  }
  return pipeline::Value::map(std::move(m));
}

// Component-wise reduction over a cell field, accumulated in index order
// (determinism gate: no reordering, no parallel reduction).
void reduce_components(const core::Field& f, Stats out[kComponents]) {
  const auto data = f.data();
  const auto comps = static_cast<std::size_t>(f.components());
  const auto n = f.count();
  double sums[kComponents] = {0.0, 0.0, 0.0};
  std::size_t counted = 0;
  for (std::size_t c = 0; c < n; ++c) {
    const std::size_t base = c * comps;
    if (base + (kComponents - 1) >= data.size())
      break;
    for (std::size_t k = 0; k < kComponents; ++k) {
      const double v = data[base + k];
      if (counted == 0) {
        out[k].min = v;
        out[k].max = v;
      } else {
        out[k].min = v < out[k].min ? v : out[k].min;
        out[k].max = v > out[k].max ? v : out[k].max;
      }
      sums[k] += v;
    }
    ++counted;
  }
  if (counted > 0) {
    const double inv = 1.0 / static_cast<double>(counted);
    for (std::size_t k = 0; k < kComponents; ++k) {
      out[k].mean = sums[k] * inv;
      out[k].any = true;
    }
  }
}

// One dispatch, shaped exactly like query_mesh_quality's.
struct DispatchOutcome {
  std::shared_ptr<core::Field> field;
  ToolError error{};
  bool failed = false;
};

DispatchOutcome dispatch_check(ToolContext& ctx,
                               const char* capability_id,
                               const std::map<std::string, pipeline::Value>& forwarded) {
  DispatchOutcome outcome;
  auto mesh_so = std::make_shared<pipeline::StageOutput>();
  mesh_so->kind = pipeline::StageOutput::Kind::Mesh;
  mesh_so->mesh = ctx.mesh_handle;
  std::map<std::string, std::shared_ptr<void>> upstream;
  upstream.emplace("__session_mesh__", std::static_pointer_cast<void>(mesh_so));

  std::map<std::string, pipeline::Value> stage_input = forwarded;
  stage_input.insert_or_assign("mesh", pipeline::Value::stage_ref("__session_mesh__"));
  auto input_value = pipeline::Value::map(std::move(stage_input));

  pipeline::DispatchContext dctx{capability_id, input_value, upstream};
  auto dr = ctx.dispatcher->dispatch(dctx);
  if (auto* derr = std::get_if<pipeline::DispatchError>(&dr)) {
    outcome.failed = true;
    outcome.error = {"DISPATCH_FAILED",
                     std::string(capability_id) + " dispatch failed: " + derr->message};
    return outcome;
  }
  auto payload = std::get<pipeline::DispatchSuccess>(dr);
  const auto* so = static_cast<const pipeline::StageOutput*>(payload.get());
  if (so == nullptr || so->kind != pipeline::StageOutput::Kind::Field || !so->field) {
    outcome.failed = true;
    outcome.error = {"INTERNAL",
                     std::string(capability_id) + " returned an unexpected payload kind"};
    return outcome;
  }
  if (so->field->components() < kComponents || so->field->count() == 0) {
    outcome.failed = true;
    outcome.error = {"INTERNAL",
                     std::string(capability_id)
                         + " returned a field with fewer than 3 components or no cells",
                     "the capability contract specifies a 3-component cell field"};
    return outcome;
  }
  outcome.field = so->field;
  return outcome;
}

ToolResult run(const pipeline::Value& inputs, ToolContext& ctx) {
  if (inputs.kind() != pipeline::Value::Kind::Map && inputs.kind() != pipeline::Value::Kind::Null) {
    return invalid("input must be a map",
                   "check_marine_integrity input must be a map (or null for all defaults)",
                   "{checks: ['collapse', 'corrosion'], design_depth: 300, alloy: '316L'}");
  }

  // ---- which checks ----
  bool want_collapse = true;
  bool want_corrosion = true;
  if (const auto* cv = find(inputs, "checks")) {
    if (cv->kind() != pipeline::Value::Kind::List || cv->as_list().empty()) {
      return invalid("checks must be a non-empty list",
                     "`checks` must be a non-empty list of 'collapse' / 'corrosion'",
                     "omit it to run both");
    }
    want_collapse = false;
    want_corrosion = false;
    for (const auto& entry : cv->as_list()) {
      if (entry.kind() != pipeline::Value::Kind::String) {
        return invalid("checks entries must be strings",
                       "every entry of `checks` must be a string",
                       "{checks: ['corrosion']}");
      }
      const auto name = entry.as_string();
      if (name == "collapse") {
        want_collapse = true;
      } else if (name == "corrosion") {
        want_corrosion = true;
      } else {
        return invalid("unknown check",
                       "`checks` entries must be 'collapse' or 'corrosion'",
                       "got: '" + std::string(name) + "'");
      }
    }
  }

  // ---- forwarded plugin inputs ----
  std::map<std::string, pipeline::Value> forwarded;
  if (inputs.kind() == pipeline::Value::Kind::Map) {
    for (const auto& [k, v] : inputs.as_map()) {
      if (is_tool_local(k))
        continue;
      switch (v.kind()) {
        case pipeline::Value::Kind::Number:
        case pipeline::Value::Kind::String:
        case pipeline::Value::Kind::Bool:
        case pipeline::Value::Kind::List:
          forwarded.emplace(k, v);
          break;
        default:
          return invalid("input '" + k + "' has an unusable kind",
                         "`" + k + "` must be a number, string, bool or list — maps and stage "
                                   "references are not forwarded to the marine solvers",
                         "pass scalar plugin inputs directly, e.g. {thickness: 0.012}");
      }
    }
  }

  // ---- host services + capability availability + mesh ----
  if (ctx.registry == nullptr || ctx.dispatcher == nullptr) {
    return ToolResult{
        pipeline::Value::null_value(),
        "host registry / dispatcher not wired into ToolContext",
        ToolError{"INTERNAL",
                  "check_marine_integrity requires ToolContext.registry + ToolContext.dispatcher",
                  "run the tool through the CLI / desktop agent session, which wires both"}};
  }
  std::vector<std::string> missing;
  if (want_collapse && ctx.registry->find_solver(kCollapseCapability) == nullptr)
    missing.emplace_back(kCollapseCapability);
  if (want_corrosion && ctx.registry->find_solver(kCorrosionCapability) == nullptr)
    missing.emplace_back(kCorrosionCapability);
  if (!missing.empty()) {
    std::string joined;
    for (const auto& m : missing) {
      if (!joined.empty())
        joined += ", ";
      joined += m;
    }
    return ToolResult{
        pipeline::Value::null_value(),
        "missing marine capability: " + joined,
        ToolError{"PLUGIN_NOT_FOUND",
                  "no solver capability registered as: " + joined,
                  "build / install the in-tree marine plugin, narrow `checks` to the "
                  "capabilities you have, or call `list_plugins` to see what is available"}};
  }
  if (!ctx.mesh_handle) {
    return ToolResult{
        pipeline::Value::null_value(),
        "no mesh available — call `mesh` first",
        ToolError{"PRECONDITION_FAILED",
                  "check_marine_integrity requires a mesh; ToolContext.mesh_handle is null",
                  "invoke the `mesh` tool (e.g. capability mesher.am.layered) before "
                  "check_marine_integrity"}};
  }

  std::vector<std::string> advisories;
  bool ok = true;

  std::map<std::string, pipeline::Value> out;
  std::shared_ptr<core::Field> last_field;

  // ---- collapse ----
  double collapse_pressure = 0.0;
  double margin = 0.0;
  std::string governing_mode = "n/a";
  if (want_collapse) {
    auto res = dispatch_check(ctx, kCollapseCapability, forwarded);
    if (res.failed)
      return ToolResult{pipeline::Value::null_value(), res.error.message, res.error};
    Stats s[kComponents];
    reduce_components(*res.field, s);
    // The triple is analytical, so it is identical on every cell; report
    // the mean (== the value) and surface the spread only if there is one.
    collapse_pressure = s[0].mean;
    margin = s[1].mean;
    const double code = s[2].mean;
    if (code >= 0.0 && code < static_cast<double>(kNumGoverningModes))
      governing_mode = kGoverningModeNames[static_cast<std::size_t>(code)];
    else
      governing_mode = "unrecognised_code";

    std::map<std::string, pipeline::Value> collapse;
    collapse.emplace("collapse_pressure_pa", pipeline::Value::number(collapse_pressure));
    collapse.emplace("collapse_pressure_mpa", pipeline::Value::number(collapse_pressure / 1.0e6));
    collapse.emplace("margin", pipeline::Value::number(margin));
    collapse.emplace("margin_stats", stats_value(s[1]));
    collapse.emplace("governing_mode", pipeline::Value::string(governing_mode));
    collapse.emplace("governing_mode_code", pipeline::Value::number(code));
    collapse.emplace("adequate", pipeline::Value::boolean(margin >= kMinAcceptableMargin));
    collapse.emplace("num_cells",
                     pipeline::Value::number(static_cast<double>(res.field->count())));
    out.emplace("collapse", pipeline::Value::map(std::move(collapse)));

    if (!(margin >= kMinAcceptableMargin)) {
      ok = false;
      advisories.emplace_back(
          "collapse margin " + std::to_string(margin)
          + " is below 1.0 against the factored design pressure, governing mode "
          + governing_mode
          + " — increase thickness, shorten the unsupported length, or reduce design depth");
    }
    last_field = res.field;
  }

  // ---- corrosion ----
  double max_thickness_loss = 0.0;
  double max_pitting = 0.0;
  double max_galvanic = 0.0;
  if (want_corrosion) {
    auto res = dispatch_check(ctx, kCorrosionCapability, forwarded);
    if (res.failed)
      return ToolResult{pipeline::Value::null_value(), res.error.message, res.error};
    Stats s[kComponents];
    reduce_components(*res.field, s);
    max_thickness_loss = s[0].max;
    max_pitting = s[1].max;
    max_galvanic = s[2].max;

    std::map<std::string, pipeline::Value> corrosion;
    corrosion.emplace("thickness_loss_mm", stats_value(s[0]));
    corrosion.emplace("pitting_risk", stats_value(s[1]));
    corrosion.emplace("galvanic_risk", stats_value(s[2]));
    corrosion.emplace("max_thickness_loss_mm", pipeline::Value::number(max_thickness_loss));
    corrosion.emplace("max_pitting_risk", pipeline::Value::number(max_pitting));
    corrosion.emplace("max_galvanic_risk", pipeline::Value::number(max_galvanic));
    corrosion.emplace("num_cells",
                      pipeline::Value::number(static_cast<double>(res.field->count())));
    out.emplace("corrosion", pipeline::Value::map(std::move(corrosion)));

    if (max_pitting > kRiskAdvisoryLevel) {
      ok = false;
      advisories.emplace_back(
          "pitting risk exceeds 0.5 — raise PREN (more Cr / Mo / N), lower the service "
          "temperature, or specify a surface finish better than as-built");
    }
    if (max_galvanic > kRiskAdvisoryLevel) {
      ok = false;
      advisories.emplace_back(
          "galvanic risk exceeds 0.5 — isolate the couple, reduce the cathode/anode area "
          "ratio, or add cathodic protection");
    }
    if (max_thickness_loss > 0.0) {
      advisories.emplace_back("budget a corrosion allowance of at least "
                              + std::to_string(max_thickness_loss)
                              + " mm over the service life and re-run check_printability with "
                                "corrosion_allowance set");
    }
    // `writer.marine.qualification_report`'s canonical field is the
    // corrosion field, so leave that one on the session when we ran it.
    last_field = res.field;
  }

  if (last_field)
    ctx.field_handle = last_field;

  std::vector<pipeline::Value> advisory_values;
  advisory_values.reserve(advisories.size());
  for (const auto& a : advisories)
    advisory_values.push_back(pipeline::Value::string(a));

  std::vector<pipeline::Value> checks_run;
  if (want_collapse)
    checks_run.push_back(pipeline::Value::string("collapse"));
  if (want_corrosion)
    checks_run.push_back(pipeline::Value::string("corrosion"));

  out.emplace("checks_run", pipeline::Value::list(std::move(checks_run)));
  out.emplace("ok", pipeline::Value::boolean(ok));
  out.emplace("advisories", pipeline::Value::list(std::move(advisory_values)));
  out.emplace("thresholds",
              pipeline::Value::map({{"min_margin", pipeline::Value::number(kMinAcceptableMargin)},
                                    {"risk_advisory_level",
                                     pipeline::Value::number(kRiskAdvisoryLevel)}}));
  out.emplace("notes",
              pipeline::Value::string(
                  "Advisory only: souxmar is not a classification society. Collapse figures "
                  "are closed-form preliminary sizing with documented imperfection and "
                  "AM-anisotropy knockdowns; corrosion rates are indicative literature "
                  "values, not design values."));

  char buf[416];
  if (want_collapse && want_corrosion) {
    std::snprintf(buf,
                  sizeof(buf),
                  "marine integrity: collapse %.4g MPa margin %.3f (%s); corrosion loss "
                  "%.3f mm, pitting %.2f, galvanic %.2f  [%s, %zu advisory(ies)]",
                  collapse_pressure / 1.0e6,
                  margin,
                  governing_mode.c_str(),
                  max_thickness_loss,
                  max_pitting,
                  max_galvanic,
                  ok ? "ok" : "action needed",
                  advisories.size());
  } else if (want_collapse) {
    std::snprintf(buf,
                  sizeof(buf),
                  "marine integrity: collapse %.4g MPa margin %.3f (%s)  [%s, %zu advisory(ies)]",
                  collapse_pressure / 1.0e6,
                  margin,
                  governing_mode.c_str(),
                  ok ? "ok" : "action needed",
                  advisories.size());
  } else {
    std::snprintf(buf,
                  sizeof(buf),
                  "marine integrity: corrosion loss %.3f mm, pitting %.2f, galvanic %.2f  "
                  "[%s, %zu advisory(ies)]",
                  max_thickness_loss,
                  max_pitting,
                  max_galvanic,
                  ok ? "ok" : "action needed",
                  advisories.size());
  }

  return ToolResult{pipeline::Value::map(std::move(out)), std::string{buf}, std::nullopt};
}

}  // namespace

Tool make_check_marine_integrity_tool() {
  Tool t;
  t.name = "check_marine_integrity";
  t.description =
      "Advisory marine integrity summary for the current mesh: runs "
      "`solver.marine.hull_collapse` (collapse pressure, margin over the factored design "
      "pressure, governing mode) and `solver.marine.corrosion` (thickness loss over the "
      "service life, pitting risk, galvanic risk), then returns the numbers plus concrete "
      "advisories. Narrow with `checks`. Advisory only — not a classification-society "
      "calculation.";
  t.category = "Field";
  t.confirmation = Confirmation::Auto;
  t.input_schema_doc =
      "{checks?: ['collapse' | 'corrosion', ...],   # default both\n"
      " # every other key is forwarded verbatim to both solvers (unknown keys are ignored):\n"
      " hull_type?, diameter?, thickness?, unsupported_length?, youngs_modulus?,\n"
      " poisson_ratio?, yield_strength?, design_depth?, seawater_density?, gravity?,\n"
      " imperfection_knockdown?, am_anisotropy_knockdown?, safety_factor?,\n"
      " alloy?, chromium_pct?, molybdenum_pct?, nitrogen_pct?, mating_alloy?,\n"
      " area_ratio_cathode_anode?, seawater_temperature?, salinity_psu?, flow_velocity?,\n"
      " oxygen_mg_per_l?, service_life_years?, cathodic_protection?, coating_efficiency?,\n"
      " as_built_surface?\n"
      "}";
  t.output_schema_doc =
      "{checks_run: [string], ok: bool, advisories: [string],\n"
      " collapse?: {collapse_pressure_pa, collapse_pressure_mpa, margin, margin_stats,\n"
      "             governing_mode, governing_mode_code, adequate, num_cells},\n"
      " corrosion?: {thickness_loss_mm: {min,max,mean}, pitting_risk: {...},\n"
      "              galvanic_risk: {...}, max_thickness_loss_mm, max_pitting_risk,\n"
      "              max_galvanic_risk, num_cells},\n"
      " thresholds: {min_margin, risk_advisory_level}, notes: string}";
  t.handler = [](const pipeline::Value& inputs, ToolContext& ctx) -> ToolResult {
    try {
      return run(inputs, ctx);
    } catch (const std::exception& e) {
      return ToolResult{pipeline::Value::null_value(),
                        "check_marine_integrity failed internally",
                        ToolError{"INTERNAL", std::string("check_marine_integrity: ") + e.what()}};
    } catch (...) {
      return ToolResult{pipeline::Value::null_value(),
                        "check_marine_integrity failed internally",
                        ToolError{"INTERNAL", "check_marine_integrity: unknown exception"}};
    }
  };
  return t;
}

}  // namespace souxmar::ai
