// SPDX-License-Identifier: Apache-2.0
//
// Tool: propose_cfd_setup
//
// Sprint 8 push 5. Heuristic CFD planner: from a verbal goal + an
// optional list of mesh boundary tags, propose a complete BC sketch
// (a sequence of apply_inlet / apply_wall / apply_outlet calls the
// agent can then dispatch) plus a recommended solver capability.
//
// Read-only by construction: the tool does not mutate session_state or
// call any other tool; it returns a structured plan the LLM walks
// through. That keeps the planner deterministic, replayable, and easy
// to evaluate against the Sprint 7 push 4 agent eval suite.
//
// Vocabulary mapping (terse, deliberately so):
//   - regime defaults to 'incompressible'.
//   - The first tag whose name matches /in|inlet|inflow/i (or the first
//     tag tout court when no name matches) becomes the inlet; the last
//     matching /out|outlet|exit/i (or the last tag) becomes the outlet;
//     every remaining tag is a no-slip wall.
//   - Recommended solver is `solver.cfd.simple` for incompressible
//     steady flows (cfd-stub serves it in default CI; openfoam-solver
//     serves `solver.cfd.openfoam.simple` on the nightly matrix) and
//     `solver.cfd.openfoam.pimple` for compressible/transient.

#include "souxmar/ai/tool.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace souxmar::ai {

namespace {

const pipeline::Value* find(const pipeline::Value& v, const char* key) {
  return v.kind() == pipeline::Value::Kind::Map ? v.find(key) : nullptr;
}

std::string lower(std::string_view s) {
  std::string out(s);
  std::transform(
      out.begin(), out.end(), out.begin(), [](unsigned char c) { return std::tolower(c); });
  return out;
}

bool contains(const std::string& haystack, std::string_view needle) {
  return haystack.find(needle) != std::string::npos;
}

bool looks_like_inlet(const std::string& tag_lower) {
  return contains(tag_lower, "inlet") || contains(tag_lower, "inflow") || tag_lower == "in";
}

bool looks_like_outlet(const std::string& tag_lower) {
  return contains(tag_lower, "outlet") || contains(tag_lower, "outflow")
         || contains(tag_lower, "exit") || tag_lower == "out";
}

}  // namespace

Tool make_propose_cfd_setup_tool() {
  Tool t;
  t.name = "propose_cfd_setup";
  t.description =
      "Propose a CFD setup from a verbal goal + optional list of mesh boundary tags. "
      "Returns a sequence of apply_inlet / apply_wall / apply_outlet calls the agent "
      "can dispatch, plus a recommended solver capability. Read-only; does not mutate "
      "session state. Output is a sketch — refine before solving.";
  t.category = "CFD";
  t.confirmation = Confirmation::Auto;  // pure planner; no side effects
  t.input_schema_doc =
      "{goal: string,                                                     # one-line CFD intent\n"
      " tags?: [string],                                                  # boundary tags from the "
      "mesh\n"
      " regime?: 'incompressible' | 'compressible' | 'multiphase',        # default "
      "'incompressible'\n"
      " target_velocity?: number                                          # inlet magnitude, "
      "default 1.0 m/s\n"
      "}";
  t.output_schema_doc =
      "{plan: [{tool: string, input: <Map>}, ...],   # each step is dispatch-ready\n"
      " recommended_solver: string,                  # capability id (e.g. solver.cfd.simple)\n"
      " notes: string                                # ≤3-line rationale; UI surface\n"
      "}";
  t.handler = [](const pipeline::Value& inputs, ToolContext& /*ctx*/) -> ToolResult {
    if (inputs.kind() != pipeline::Value::Kind::Map) {
      return ToolResult{pipeline::Value::null_value(),
                        "input must be a map",
                        ToolError{"INVALID_ARGUMENT",
                                  "propose_cfd_setup input must be a map with `goal` and optional "
                                  "`tags`/`regime`/`target_velocity`"}};
    }
    const auto* goal = find(inputs, "goal");
    if (!goal || goal->kind() != pipeline::Value::Kind::String) {
      return ToolResult{
          pipeline::Value::null_value(),
          "missing string `goal`",
          ToolError{"INVALID_ARGUMENT",
                    "propose_cfd_setup requires a string `goal` describing the CFD intent"}};
    }

    // Optional regime.
    std::string regime = "incompressible";
    if (const auto* r = find(inputs, "regime")) {
      if (r->kind() != pipeline::Value::Kind::String) {
        return ToolResult{
            pipeline::Value::null_value(),
            "regime must be a string",
            ToolError{"INVALID_ARGUMENT",
                      "`regime` must be one of incompressible / compressible / multiphase"}};
      }
      regime = std::string(r->as_string());
      if (regime != "incompressible" && regime != "compressible" && regime != "multiphase") {
        return ToolResult{
            pipeline::Value::null_value(),
            "unsupported regime",
            ToolError{"INVALID_ARGUMENT",
                      "`regime` must be one of incompressible / compressible / multiphase",
                      "got: '" + regime + "'"}};
      }
    }

    // Inlet velocity magnitude (m/s).
    double target_velocity = 1.0;
    if (const auto* v = find(inputs, "target_velocity")) {
      if (v->kind() != pipeline::Value::Kind::Number) {
        return ToolResult{
            pipeline::Value::null_value(),
            "target_velocity must be a number",
            ToolError{"INVALID_ARGUMENT", "`target_velocity` must be a number when set"}};
      }
      target_velocity = v->as_number();
      if (target_velocity <= 0.0) {
        return ToolResult{
            pipeline::Value::null_value(),
            "target_velocity must be positive",
            ToolError{"INVALID_ARGUMENT",
                      "`target_velocity` must be > 0",
                      "for reverse flow set the inlet vector explicitly via apply_inlet"}};
      }
    }

    // Optional tag list. Without tags, the planner sketches a generic
    // ['inlet','walls','outlet'] trio; with tags, it picks the matching
    // first/last.
    std::vector<std::string> tags;
    if (const auto* t = find(inputs, "tags")) {
      if (t->kind() != pipeline::Value::Kind::List) {
        return ToolResult{
            pipeline::Value::null_value(),
            "tags must be a list",
            ToolError{"INVALID_ARGUMENT", "`tags` must be a list of strings when set"}};
      }
      for (const auto& s : t->as_list()) {
        if (s.kind() != pipeline::Value::Kind::String) {
          return ToolResult{
              pipeline::Value::null_value(),
              "tags must be strings",
              ToolError{"INVALID_ARGUMENT", "every entry of `tags` must be a string"}};
        }
        tags.emplace_back(s.as_string());
      }
    }

    std::string inlet_tag;
    std::string outlet_tag;
    std::vector<std::string> wall_tags;
    if (tags.empty()) {
      inlet_tag = "inlet";
      outlet_tag = "outlet";
      wall_tags = {"walls"};
    } else {
      // First pass: find explicit inlet / outlet by name.
      for (const auto& tag : tags) {
        const auto l = lower(tag);
        if (inlet_tag.empty() && looks_like_inlet(l))
          inlet_tag = tag;
        else if (outlet_tag.empty() && looks_like_outlet(l))
          outlet_tag = tag;
      }
      // Fallbacks: first unclaimed = inlet, last unclaimed = outlet, rest = walls.
      for (const auto& tag : tags) {
        if (tag == inlet_tag || tag == outlet_tag)
          continue;
        if (inlet_tag.empty()) {
          inlet_tag = tag;
          continue;
        }
        if (outlet_tag.empty()) {
          outlet_tag = tag;
          continue;
        }
        wall_tags.push_back(tag);
      }
      // If outlet still empty but we had ≥2 tags, demote the last wall.
      if (outlet_tag.empty() && !wall_tags.empty()) {
        outlet_tag = wall_tags.back();
        wall_tags.pop_back();
      }
      if (wall_tags.empty())
        wall_tags = {"walls"};
    }

    // Build the dispatch plan.
    std::vector<pipeline::Value> plan;
    {
      std::map<std::string, pipeline::Value> step;
      std::map<std::string, pipeline::Value> in;
      in.emplace("tag", pipeline::Value::string(inlet_tag));
      in.emplace("velocity", pipeline::Value::number(target_velocity));
      step.emplace("tool", pipeline::Value::string("apply_inlet"));
      step.emplace("input", pipeline::Value::map(std::move(in)));
      plan.push_back(pipeline::Value::map(std::move(step)));
    }
    for (const auto& wt : wall_tags) {
      std::map<std::string, pipeline::Value> step;
      std::map<std::string, pipeline::Value> in;
      in.emplace("tag", pipeline::Value::string(wt));
      in.emplace("condition", pipeline::Value::string("no_slip"));
      step.emplace("tool", pipeline::Value::string("apply_wall"));
      step.emplace("input", pipeline::Value::map(std::move(in)));
      plan.push_back(pipeline::Value::map(std::move(step)));
    }
    if (!outlet_tag.empty()) {
      std::map<std::string, pipeline::Value> step;
      std::map<std::string, pipeline::Value> in;
      in.emplace("tag", pipeline::Value::string(outlet_tag));
      in.emplace("condition", pipeline::Value::string("pressure_outlet"));
      in.emplace("pressure", pipeline::Value::number(0.0));  // gauge
      step.emplace("tool", pipeline::Value::string("apply_outlet"));
      step.emplace("input", pipeline::Value::map(std::move(in)));
      plan.push_back(pipeline::Value::map(std::move(step)));
    }

    // Solver recommendation.
    std::string solver;
    std::string solver_rationale;
    if (regime == "incompressible") {
      solver = "solver.cfd.simple";
      solver_rationale =
          "steady incompressible — cfd-stub serves this in default CI; "
          "openfoam-solver serves solver.cfd.openfoam.simple on the nightly matrix.";
    } else if (regime == "compressible") {
      solver = "solver.cfd.openfoam.pimple";
      solver_rationale =
          "compressible flow lives on the opt-in OpenFOAM matrix; no always-on stub.";
    } else {  // multiphase
      solver = "solver.cfd.openfoam.inter";
      solver_rationale = "multiphase (VOF) requires interFoam — opt-in OpenFOAM nightly only.";
    }

    std::map<std::string, pipeline::Value> out;
    out.emplace("plan", pipeline::Value::list(std::move(plan)));
    out.emplace("recommended_solver", pipeline::Value::string(solver));
    out.emplace("notes", pipeline::Value::string(solver_rationale));

    std::string summary =
        "proposed " + std::to_string(wall_tags.size() + 2) + "-BC " + regime + " setup → " + solver;
    return ToolResult{pipeline::Value::map(std::move(out)), std::move(summary), std::nullopt};
  };
  return t;
}

}  // namespace souxmar::ai
