---
name: adding-agent-tool
description: Use when adding a new tool to the souxmar AI agent's typed tool surface (in src/ai/tools/). Walks through schema design, dispatcher registration, confirmation policy, audit log entry, and eval coverage. Triggers on "agent tool", "AI tool", "tool dispatcher", "AI_INTEGRATION", "souxmar agent".
---

# Adding an agent tool

The souxmar agent's tool surface is a typed, versioned contract that lives in `src/ai/tools/`. Adding a tool is **additive in a minor version** but the contract is otherwise frozen post-Sprint 8. Every tool the agent can call goes through this pipeline; there is no privileged path.

## When to use this skill

- Adding a new agent-callable capability (e.g. `apply_inlet`, `compute_principal_stress`).
- Wrapping an existing pipeline-orchestrator call as an agent tool.
- Modifying an existing tool's schema (this is breaking — see `reviewing-abi-changes`).

## When NOT to use this skill

- Adding a new plugin: use `developing-souxmar-plugin`.
- Adding a CLI flag: that is a separate surface; CLI flags are not agent tools.
- "Improving" the agent prompt: prompt changes are operational, not tool changes; they happen in `src/ai/prompts/`.

## Workflow

### 1. Scope the tool

Each tool is one capability. If you find yourself wanting parameters like `mode: "plot" | "save" | "analyse"`, split into three tools.

Tools fall into categories:

| Category    | Example                | Typical confirmation policy      |
| ----------- | ---------------------- | -------------------------------- |
| Read        | `read_geometry_summary`| `auto`                           |
| Pipeline    | `propose_pipeline`     | `auto` (proposes; user accepts)  |
| Mesh        | `mesh`                 | `confirm-once` (long-running)    |
| BC          | `set_bc`               | `confirm-once`                   |
| Material    | `set_material`         | `confirm-once`                   |
| Solve       | `solve`                | `confirm-always` if est > 60 s   |
| Postproc    | `compute_field`        | `auto`                           |
| File-write  | `write_pipeline`       | `confirm-always`                 |
| File-write  | `export_results`       | `confirm-always`                 |
| Plugin      | `list_plugins`         | `auto`                           |

If a tool sends data outside the user's machine, it MUST be `confirm-always` and tagged `data_leaves_machine: true` in the schema.

### 2. Write the schema

Tool schemas live in `src/ai/tools/schema/<tool_name>.json`. JSON Schema for inputs and outputs:

```json
{
  "tool": "set_bc",
  "version": 1,
  "category": "bc",
  "confirmation": "confirm-once",
  "data_leaves_machine": false,
  "input": {
    "type": "object",
    "required": ["mesh_ref", "tag", "type", "value"],
    "properties": {
      "mesh_ref": {
        "type": "string",
        "description": "Reference to a mesh stage in the active pipeline."
      },
      "tag": {
        "type": "string",
        "description": "Named tag of the entity (face/edge) to apply the BC to."
      },
      "type": {
        "type": "string",
        "enum": ["dirichlet", "neumann", "robin"]
      },
      "value": {
        "type": "array",
        "items": {"type": "number"},
        "minItems": 1,
        "maxItems": 6
      }
    }
  },
  "output": {
    "type": "object",
    "required": ["bc_id", "summary"],
    "properties": {
      "bc_id": {"type": "string"},
      "summary": {"type": "string", "description": "Human-readable summary for the chat panel."}
    }
  }
}
```

### 3. Implement the dispatcher

In `src/ai/tools/<tool_name>.cpp` (or `.rs` if Rust):

```cpp
#include <souxmar-ai/tool.h>
#include <souxmar/pipeline.h>

namespace souxmar::ai::tools {

ToolResult set_bc(const ToolInput& input, ToolContext& ctx) {
    auto mesh_ref = input.required_string("mesh_ref");
    auto tag      = input.required_string("tag");
    auto type     = input.required_enum("type", {"dirichlet", "neumann", "robin"});
    auto value    = input.required_number_array("value");

    // Validate against the active pipeline.
    auto& pipeline = ctx.active_pipeline();
    auto stage = pipeline.find_stage(mesh_ref);
    if (!stage) {
        return ToolResult::error("Mesh stage '{}' not found in pipeline.", mesh_ref);
    }

    // Apply through the orchestrator (NOT a privileged path).
    auto bc_id = pipeline.add_boundary_condition(stage, tag, type, value);

    return ToolResult::ok({
        {"bc_id", bc_id},
        {"summary", fmt::format("Applied {} BC to '{}' (id={})", type, tag, bc_id)},
    });
}

}  // namespace souxmar::ai::tools

// Registration:
SOUXMAR_AI_REGISTER_TOOL(set_bc, "set_bc", set_bc);
```

### 4. Confirmation policy

The confirmation behaviour is declared in the schema (`confirmation: "auto" | "confirm-once" | "confirm-always"`). The dispatcher reads this; the desktop app's chat panel renders the chip when needed.

There is **no global "trust this AI" override**. Per-tool overrides exist in `~/.config/souxmar/agent-policy.toml` for power users. Do not add a tool with `confirmation: "auto"` if it sends data anywhere; that is a security regression and CI will block it.

### 5. Audit log

Every tool invocation writes to `.souxmar/chat/audit.log`:

```
2026-05-11T14:23:11.231Z  tool=set_bc  status=ok  input_hash=a93f...  runtime_ms=12  cost_tokens=null  user_confirmed=true
```

The dispatcher framework writes this automatically; no per-tool code needed unless you want to log additional fields.

### 6. Add to the eval suite

Every new tool gets at least one test in `tests/agent-eval/canonical.yaml`:

```yaml
- name: "Set a Dirichlet BC on the clamped face"
  setup: examples/cantilever-beam/cantilever.souxmar.yaml
  prompt: "Apply a fixed support to the clamped_face."
  must_invoke: ["set_bc"]
  must_succeed: true
  may_invoke: ["read_geometry_summary"]
  must_not_invoke: ["solve", "export_results"]
  pass_criterion:
    - tool_invocation: { tool: "set_bc", input.tag: "clamped_face", input.type: "dirichlet" }
```

The agent eval suite gates merges in `src/ai/`; pass-rate target is 90 % per `ENGINEERING_PRACTICES.md`.

### 7. Document

Add the tool to:
- `docs/AI_INTEGRATION.md` — one-line entry in the tool table.
- The agent reference page on the public docs site.

## Common mistakes

- Adding a `verbose: bool` parameter "to make the output prettier." Resist. Tool inputs are functional, not cosmetic.
- Returning unstructured prose in the output. The output is consumed by the model — it must be structured JSON the model can chain on.
- Returning megabytes of data. The output should be small (a summary); larger payloads stay in the project filesystem and the tool returns a reference.
- Bypassing the orchestrator and calling plugins directly. The agent has no privileged path. Every tool goes through `pipeline.*`.
- Forgetting to add `data_leaves_machine: true` to a tool that sends data anywhere.
- `confirmation: "auto"` on a tool that writes files. Always `confirm-always` for writes.

## Reference

- `docs/AI_INTEGRATION.md` — agent architecture and current tool catalogue.
- `docs/adr/0003-byok-as-ai-default.md` — the trust model the tool surface lives inside.
- `src/ai/tools/schema/` — existing schemas as worked examples.
- `tests/agent-eval/canonical.yaml` — eval test format.
