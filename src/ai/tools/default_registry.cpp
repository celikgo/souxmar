// SPDX-License-Identifier: Apache-2.0
//
// default_v1_tools() — assembles the v1 agent catalogue per
// docs/AI_INTEGRATION.md. Each tool's factory lives in a sibling .cpp to
// keep the per-tool code reviewable in isolation. The catalogue was
// frozen final at 18 tools (ADR-0011, superseding the freeze-candidate
// ADR-0010); additions land via the "Ratchet: additive tool (ADR-0010)"
// marker and are enforced by scripts/check-tool-contract.sh. The
// manufacturing block adds tools 19-24, taking the catalogue to 24.

#include "souxmar/ai/tool.h"

namespace souxmar::ai {

// Per-tool factories (defined in the sibling translation units).
Tool make_read_geometry_summary_tool();
Tool make_mesh_tool();
Tool make_set_bc_tool();
Tool make_solve_tool();
Tool make_screenshot_viewport_tool();
// Sprint 5 push 2 — grew the catalogue to 8.
Tool make_query_field_tool();
Tool make_compute_field_tool();
Tool make_propose_pipeline_tool();
// Sprint 6 push 1 — catalogue 8 → 9.
Tool make_query_mesh_quality_tool();
// Sprint 6 push 3 — catalogue 9 → 13.
Tool make_set_material_tool();
Tool make_list_plugins_tool();
Tool make_apply_pipeline_diff_tool();
Tool make_export_results_tool();
// Sprint 8 push 4 — CFD-aware BC tools. Catalogue 13 → 16.
Tool make_apply_inlet_tool();
Tool make_apply_wall_tool();
Tool make_apply_outlet_tool();
// Sprint 8 push 5 — CFD planner + BC validator. Catalogue 16 → 18.
// Tool-contract v1 frozen final at this catalogue (ADR-0011, which
// closes the freeze-candidate ADR-0010).
Tool make_propose_cfd_setup_tool();
Tool make_validate_bcs_tool();
// Manufacturing block — additive manufacturing + marine. Catalogue
// 18 → 24 (tools 19-24 of the ADR-0010 additive ratchet; see
// docs/adr/0045-agent-tool-contract-am-ratchet.md).
Tool make_propose_am_setup_tool();
Tool make_check_printability_tool();
Tool make_set_build_orientation_tool();
Tool make_estimate_build_cost_tool();
Tool make_apply_hydrostatic_load_tool();
Tool make_check_marine_integrity_tool();

ToolRegistry default_v1_tools() {
  ToolRegistry r;
  // Sprint 4 push 3 catalogue.
  r.add(make_read_geometry_summary_tool());
  r.add(make_mesh_tool());
  r.add(make_set_bc_tool());
  r.add(make_solve_tool());
  r.add(make_screenshot_viewport_tool());
  // Sprint 5 push 2 additions.
  r.add(make_query_field_tool());
  r.add(make_compute_field_tool());
  r.add(make_propose_pipeline_tool());
  // Sprint 6 push 1.
  r.add(make_query_mesh_quality_tool());
  // Sprint 6 push 3.
  r.add(make_set_material_tool());
  r.add(make_list_plugins_tool());
  r.add(make_apply_pipeline_diff_tool());
  r.add(make_export_results_tool());
  // Sprint 8 push 4 — CFD-aware BC vocabulary.
  r.add(make_apply_inlet_tool());
  r.add(make_apply_wall_tool());
  r.add(make_apply_outlet_tool());
  // Sprint 8 push 5 — CFD planner + BC validator (catalogue closes at
  // 18 for the v1 final freeze; ADR-0011 superseded ADR-0010).
  r.add(make_propose_cfd_setup_tool());
  r.add(make_validate_bcs_tool());
  // Manufacturing block — AM + marine (tools 19-24, catalogue 18 → 24).
  // Additive-only: the 18 tools above keep their names, categories and
  // confirmation tiers untouched.
  r.add(make_propose_am_setup_tool());
  r.add(make_check_printability_tool());
  r.add(make_set_build_orientation_tool());
  r.add(make_estimate_build_cost_tool());
  r.add(make_apply_hydrostatic_load_tool());
  r.add(make_check_marine_integrity_tool());
  return r;
}

}  // namespace souxmar::ai
