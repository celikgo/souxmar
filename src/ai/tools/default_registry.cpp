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

ToolRegistry default_v1_tools() {
  ToolRegistry r;
  r.add(make_read_geometry_summary_tool());
  r.add(make_mesh_tool());
  r.add(make_set_bc_tool());
  r.add(make_solve_tool());
  r.add(make_screenshot_viewport_tool());
  return r;
}

}  // namespace souxmar::ai
