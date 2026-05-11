// SPDX-License-Identifier: Apache-2.0
//
// Tool: compute_field
//
// Per docs/AI_INTEGRATION.md: "Calls postproc.* plugin (e.g. von Mises
// from stress)." Postproc needs an input-field-aware C ABI (the existing
// solver vtable takes only mesh + value bag), so the proper
// implementation lands in Sprint 5 push 3 alongside the heat-conduction
// solver and the memory-mapped buffer type (per ADR-0006).
//
// For push 2 we ship the tool as an honest stub returning NOT_AVAILABLE
// with the structured suggestion the model can recover from. This
// matches the screenshot_viewport pattern: the agent contract carries
// the tool, but the runtime is upfront that this build can't service
// it yet. The catalogue count of 8 is satisfied (per the Sprint 5
// commitment) without compromising the ABI surface ahead of freeze.

#include "souxmar/ai/tool.h"

namespace souxmar::ai {

Tool make_compute_field_tool() {
  Tool t;
  t.name             = "compute_field";
  t.description      =
      "Compute a derived field from the current mesh + field (e.g. von "
      "Mises stress from a strain tensor). Requires a registered "
      "postproc.* capability — the postproc C ABI lands in a later "
      "Sprint 5 push alongside the heat-conduction solver.";
  t.category         = "Postproc";
  t.confirmation     = Confirmation::ConfirmOnce;
  t.input_schema_doc =
      "{capability_id: string,                              # e.g. 'postproc.von_mises'\n"
      " options?: {...}                                     # per-plugin\n"
      "}";
  t.output_schema_doc =
      "{capability_id, location, kind, num_components}";
  t.handler = [](const pipeline::Value& /*inputs*/, ToolContext& /*ctx*/) -> ToolResult {
    return ToolResult{
        pipeline::Value::null_value(),
        "compute_field is not available in this build",
        ToolError{"NOT_AVAILABLE",
            "the postproc C ABI surface required by compute_field has not "
            "shipped yet (Sprint 5 push 3 work)",
            "for the v1 catalogue, use the read-style `query_field` tool "
            "to inspect the current field's stats instead, or wait for the "
            "postproc adapter in a later release"}};
  };
  return t;
}

}  // namespace souxmar::ai
