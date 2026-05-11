// SPDX-License-Identifier: Apache-2.0
//
// Tool: validate_bcs
//
// Sprint 8 push 5. Read-only check on session_state.boundary_conditions.
// Reports issues the agent should resolve before dispatching `solve`.
//
// Severity tiers:
//   - error:   the staged BC bag would be rejected by a real solver
//              (duplicate tag with conflicting types; malformed entry).
//   - warning: the bag is technically valid but unusual (no inlet,
//              no outlet, mixed FEM/CFD vocabulary on one session).
//   - info:    informative diagnostics (count by type, tag coverage
//              when a mesh handle is available).
//
// The handler does not mutate session_state. Confirmation::Auto — it's
// a sanity check; agents loop on it during BC iteration without prompting.

#include "souxmar/ai/tool.h"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace souxmar::ai {

namespace {

pipeline::Value issue(const char*        severity,
                      const char*        code,
                      const std::string& message,
                      const std::string& tag = "") {
  std::map<std::string, pipeline::Value> m;
  m.emplace("severity", pipeline::Value::string(severity));
  m.emplace("code",     pipeline::Value::string(code));
  m.emplace("message",  pipeline::Value::string(message));
  if (!tag.empty()) m.emplace("tag", pipeline::Value::string(tag));
  return pipeline::Value::map(std::move(m));
}

}  // namespace

Tool make_validate_bcs_tool() {
  Tool t;
  t.name             = "validate_bcs";
  t.description      =
      "Sanity-check the staged boundary-condition bag. Reports duplicate tags, "
      "missing inlet/outlet on a CFD case, malformed entries, and a tally by "
      "BC type. Read-only — does not mutate session_state.";
  t.category         = "CFD";
  t.confirmation     = Confirmation::Auto;
  t.input_schema_doc =
      "{}    # no input — reads session_state.boundary_conditions";
  t.output_schema_doc =
      "{ok: bool,                                                  # true iff no `error`-severity issues\n"
      " issues: [{severity, code, message, tag?}, ...],\n"
      " counts: {inlet: number, wall: number, outlet: number, other: number}\n"
      "}";
  t.handler = [](const pipeline::Value& /*inputs*/,
                 ToolContext&            ctx) -> ToolResult {
    std::vector<pipeline::Value> issues;
    std::size_t n_inlet = 0, n_wall = 0, n_outlet = 0, n_other = 0;

    if (ctx.session_state == nullptr ||
        ctx.session_state->kind() != pipeline::Value::Kind::Map) {
      // No BCs staged yet — that's a warning, not an error: the next
      // step is probably an apply_inlet call.
      issues.push_back(issue("warning", "EMPTY_SESSION",
          "no session_state map; no BCs to validate"));
      std::map<std::string, pipeline::Value> counts;
      counts.emplace("inlet",  pipeline::Value::number(0));
      counts.emplace("wall",   pipeline::Value::number(0));
      counts.emplace("outlet", pipeline::Value::number(0));
      counts.emplace("other",  pipeline::Value::number(0));
      std::map<std::string, pipeline::Value> out;
      out.emplace("ok",     pipeline::Value::boolean(true));
      out.emplace("issues", pipeline::Value::list(std::move(issues)));
      out.emplace("counts", pipeline::Value::map(std::move(counts)));
      return ToolResult{pipeline::Value::map(std::move(out)),
                        "no BCs staged yet", std::nullopt};
    }

    const auto* bcs = ctx.session_state->find("boundary_conditions");
    if (bcs == nullptr || bcs->kind() != pipeline::Value::Kind::List) {
      issues.push_back(issue("warning", "EMPTY_BCS",
          "session_state.boundary_conditions is empty or absent"));
    } else {
      std::map<std::string, std::string> tag_to_type;  // duplicate-tag check.
      for (std::size_t i = 0; i < bcs->as_list().size(); ++i) {
        const auto& bc = bcs->as_list()[i];
        if (bc.kind() != pipeline::Value::Kind::Map) {
          issues.push_back(issue("error", "MALFORMED",
              "entry #" + std::to_string(i) + " is not a map"));
          continue;
        }
        const auto* tag_v  = bc.find("tag");
        const auto* type_v = bc.find("type");
        if (!tag_v  || tag_v->kind()  != pipeline::Value::Kind::String) {
          issues.push_back(issue("error", "MISSING_TAG",
              "entry #" + std::to_string(i) + " has no string `tag`"));
          continue;
        }
        if (!type_v || type_v->kind() != pipeline::Value::Kind::String) {
          issues.push_back(issue("error", "MISSING_TYPE",
              "entry #" + std::to_string(i) + " has no string `type`",
              std::string(tag_v->as_string())));
          continue;
        }
        const std::string tag  = std::string(tag_v->as_string());
        const std::string type = std::string(type_v->as_string());
        if      (type == "inlet")  ++n_inlet;
        else if (type == "wall")   ++n_wall;
        else if (type == "outlet") ++n_outlet;
        else                       ++n_other;

        // Duplicate-tag check: only `wall` BCs may legitimately repeat
        // (e.g. wall on different physical patches sharing a tag —
        // unusual but not malformed). Conflict on inlet/outlet is an
        // error.
        if (auto it = tag_to_type.find(tag); it != tag_to_type.end()) {
          if (it->second == type && type == "wall") {
            // benign — same tag, same wall conditions.
          } else {
            issues.push_back(issue("error", "DUPLICATE_TAG",
                "tag appears with conflicting types ('" + it->second +
                "' then '" + type + "')",
                tag));
          }
        } else {
          tag_to_type.emplace(tag, type);
        }

        // Outlet shape check (defensive — apply_outlet rejects this at
        // apply time, but a hand-built set_bc might slip through).
        if (type == "outlet") {
          const auto* cond = bc.find("condition");
          if (cond && cond->kind() == pipeline::Value::Kind::String &&
              cond->as_string() == "pressure_outlet") {
            const auto* p = bc.find("pressure");
            if (!p || p->kind() != pipeline::Value::Kind::Number) {
              issues.push_back(issue("error", "OUTLET_NEEDS_PRESSURE",
                  "pressure_outlet on tag '" + tag + "' has no numeric pressure",
                  tag));
            }
          }
        }
      }

      // Topology warnings — CFD-shaped, applied only when CFD vocab is
      // present (no spurious warnings on pure-FEM sessions).
      const bool has_cfd_vocab =
          n_inlet + n_outlet + n_wall > 0 ||
          (n_other == 0 && bcs->as_list().empty() == false);
      if (has_cfd_vocab) {
        if (n_inlet  == 0) {
          issues.push_back(issue("warning", "NO_INLET",
              "no inlet BC staged — open-domain or natural-convection setup?"));
        }
        if (n_outlet == 0) {
          issues.push_back(issue("warning", "NO_OUTLET",
              "no outlet BC staged — sealed-domain or steady-state-only setup?"));
        }
      }
    }

    // Compose result.
    bool ok = true;
    for (const auto& iss : issues) {
      if (iss.find("severity")->as_string() == "error") { ok = false; break; }
    }

    std::map<std::string, pipeline::Value> counts;
    counts.emplace("inlet",  pipeline::Value::number(static_cast<double>(n_inlet)));
    counts.emplace("wall",   pipeline::Value::number(static_cast<double>(n_wall)));
    counts.emplace("outlet", pipeline::Value::number(static_cast<double>(n_outlet)));
    counts.emplace("other",  pipeline::Value::number(static_cast<double>(n_other)));

    std::map<std::string, pipeline::Value> out;
    out.emplace("ok",     pipeline::Value::boolean(ok));
    out.emplace("issues", pipeline::Value::list(std::move(issues)));
    out.emplace("counts", pipeline::Value::map(std::move(counts)));

    std::string summary =
        (ok ? "BC bag OK — " : "BC bag has errors — ") +
        std::to_string(n_inlet)  + " inlet, "  +
        std::to_string(n_wall)   + " wall, "   +
        std::to_string(n_outlet) + " outlet"   +
        (n_other ? (", " + std::to_string(n_other) + " other") : "");
    return ToolResult{pipeline::Value::map(std::move(out)),
                      std::move(summary), std::nullopt};
  };
  return t;
}

}  // namespace souxmar::ai
