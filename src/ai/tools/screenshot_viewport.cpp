// SPDX-License-Identifier: Apache-2.0
//
// Tool: screenshot_viewport
//
// In a full desktop build this captures the viewport as a PNG and
// returns a thumbnail path. The C++ library has no viewport — that
// lives in the Tauri desktop shell (Sprint 4 push 4+ / Sprint 5).
// For the headless library + CLI agent this tool is an honest stub:
// it returns NOT_AVAILABLE with a structured suggestion so the model
// can route around it.
//
// Why ship a stub now: the docs/AI_INTEGRATION.md v1 tool list is part
// of the agent contract. Having the entry exist + return a clean error
// is better than having tool name resolution fail differently on
// headless vs. desktop builds — model recovery is consistent either way.

#include "souxmar/ai/tool.h"

namespace souxmar::ai {

Tool make_screenshot_viewport_tool() {
  Tool t;
  t.name             = "screenshot_viewport";
  t.description      =
      "Capture the current 3D viewport as a PNG and return its path. "
      "Requires the souxmar desktop build (headless / CLI agent: returns "
      "NOT_AVAILABLE).";
  t.category         = "Read";
  // Tagged "leaves machine" in the design doc — image goes to the AI
  // provider if the agent forwards it. ConfirmOnce per docs.
  t.confirmation     = Confirmation::ConfirmOnce;
  t.input_schema_doc =
      "{}    # current viewport is implicit; future revisions may take camera params";
  t.output_schema_doc =
      "{path: string,    # filesystem path to the captured PNG\n"
      " width, height: number}";
  t.handler = [](const pipeline::Value& /*inputs*/, ToolContext& /*ctx*/) -> ToolResult {
    return ToolResult{
        pipeline::Value::null_value(),
        "viewport screenshot not available in this build",
        ToolError{"NOT_AVAILABLE",
            "screenshot_viewport requires the souxmar desktop application "
            "(the headless library has no viewport)",
            "run `souxmar` in the Tauri desktop build, or skip this tool "
            "and continue without a screenshot"}};
  };
  return t;
}

}  // namespace souxmar::ai
