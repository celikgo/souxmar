// SPDX-License-Identifier: Apache-2.0
//
// Tool: propose_pipeline
//
// The agent supplies a structured pipeline spec (`{version, stages}`);
// the tool round-trips it through emit_value_yaml then parse_pipeline
// to validate it, and returns the canonical YAML for human review. No
// disk writes happen here — the matching `write_pipeline` tool (future)
// is the only path that commits.
//
// Per docs/AI_INTEGRATION.md: "Drafts a YAML pipeline; user must accept
// before it writes." Confirmation::Auto because drafting is read-only;
// the write step has its own confirmation.

#include "souxmar/ai/tool.h"

#include "souxmar/pipeline/parser.h"
#include "souxmar/pipeline/pipeline.h"
#include "souxmar/pipeline/value.h"

#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <variant>

namespace souxmar::ai {

Tool make_propose_pipeline_tool() {
  Tool t;
  t.name             = "propose_pipeline";
  t.description      =
      "Validate and emit a YAML pipeline draft from a structured spec. "
      "Round-trips through the parser so the draft is guaranteed to "
      "load — the agent can iterate before the user runs `write_pipeline` "
      "to commit it to disk.";
  t.category         = "Pipeline";
  t.confirmation     = Confirmation::Auto;
  t.input_schema_doc =
      "{version: 1,\n"
      " stages: [\n"
      "   {id: string, plugin: string, input?: <Value map>},\n"
      "   ...\n"
      " ]}";
  t.output_schema_doc =
      "{yaml: string,                  # canonical YAML form\n"
      " parsed_stages: number,         # number of stages that parsed cleanly\n"
      "}";
  t.handler = [](const pipeline::Value& inputs, ToolContext& /*ctx*/) -> ToolResult {
    if (inputs.kind() != pipeline::Value::Kind::Map) {
      return ToolResult{
          pipeline::Value::null_value(),
          "input must be a map",
          ToolError{"INVALID_ARGUMENT",
              "propose_pipeline input must be a map with `version` and `stages`"}};
    }
    // Emit the spec as YAML, then parse it back. Two reasons: (1) the
    // parser is the ground truth on what a "valid pipeline" is — using
    // it for validation here means propose_pipeline accepts exactly the
    // same shapes `souxmar run` does; (2) the parser does the StageRef
    // / DAG checks that a naive serializer would miss.
    const auto yaml = pipeline::emit_value_yaml(inputs);
    auto parse_result = pipeline::parse_pipeline(yaml);
    if (auto* err = std::get_if<pipeline::ParseError>(&parse_result)) {
      std::ostringstream msg;
      msg << err->message;
      if (err->line)   msg << " (line " << *err->line << ")";
      if (err->column) msg << ", column " << *err->column;
      return ToolResult{
          pipeline::Value::null_value(),
          "pipeline draft failed to parse",
          ToolError{"INVALID_ARGUMENT", msg.str(),
              "fix the reported error and call propose_pipeline again — "
              "the YAML this tool emits is the same shape `souxmar run` "
              "consumes, so a parse error here is the same error the "
              "user would see at run time"}};
    }
    const auto& parsed = std::get<pipeline::Pipeline>(parse_result);

    std::map<std::string, pipeline::Value> out;
    out.emplace("yaml",           pipeline::Value::string(yaml));
    out.emplace("parsed_stages",  pipeline::Value::number(static_cast<double>(parsed.stages.size())));
    out.emplace("version",        pipeline::Value::number(static_cast<double>(parsed.version)));

    return ToolResult{
        pipeline::Value::map(std::move(out)),
        "pipeline draft: " + std::to_string(parsed.stages.size()) +
            " stages (validated)",
        std::nullopt};
  };
  return t;
}

}  // namespace souxmar::ai
