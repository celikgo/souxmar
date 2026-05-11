// SPDX-License-Identifier: Apache-2.0
//
// default_v1_tools() — assembles the five v1 agent tools per
// docs/AI_INTEGRATION.md. Each tool's factory lives in a sibling .cpp
// to keep the per-tool code reviewable in isolation.

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
  return r;
}

}  // namespace souxmar::ai
