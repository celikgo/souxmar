# Agent eval suite v1

Sprint 7 push 4. Thirty canonical agent-tool tasks the eval runner (`tools/eval/`) executes nightly. Each task is one YAML file; pass criteria are deterministic checks on tool outputs.

This is the **scripted** eval surface: each task runs the same dispatch path a real LLM would drive, but the steps are pre-staged in YAML rather than chosen by a model. The BYOK provider integration that lets an actual model drive the same task catalogue lands in Sprint 8+ alongside the desktop app. The contract here is the foundation; the LLM is the next layer.

## Why 30

The Sprint 7 plan target. The catalogue is grouped so every v1 agent tool is exercised:

| Category   | Tasks | Tools covered                                           |
| ---------- | ----- | ------------------------------------------------------- |
| read       | 4     | `read_geometry_summary`                                 |
| mesh       | 4     | `mesh` against multiple `mesher.*` capabilities         |
| bc         | 3     | `set_bc` across Dirichlet / Neumann / Robin             |
| material   | 3     | `set_material` across the canonical material models    |
| solve      | 3     | `solve` against `solver.heat.linear` / `solver.elasticity.linear` |
| query      | 3     | `query_field`                                           |
| quality    | 3     | `query_mesh_quality`                                    |
| postproc   | 3     | `compute_field`                                         |
| pipeline   | 2     | `propose_pipeline`                                      |
| diff       | 2     | `apply_pipeline_diff`                                   |
| **Total**  | **30** | **all 12 v1 tools**                                    |

## Task schema

Every task carries:

```yaml
id: <unique-slug>
description: One-line description shown in the runner output.
category: <bucket from the table above>
auto_confirm: true                  # optional, default true; flips off
                                    # the always-yes prompter for tools
                                    # at Confirmation > Auto.

setup:
  session_state:                    # optional, populates ctx.session_state
    geometry:
      num_vertices: 8

steps:                              # one or more agent-tool dispatches
  - tool: read_geometry_summary
    inputs: {}                      # value tree; missing → null

assertions:                         # checked after every step ran
  - kind: tool_outcome              # ok / fail
    value: ok
  - kind: tool_data_equals
    path: num_vertices              # dotted path into result.data
    value: 8
```

Assertion kinds:

- `tool_outcome` — `value: ok` or `value: fail`.
- `tool_error_code` — `value: "<code>"`; the tool must have errored with this code.
- `tool_data_equals` — `path: <dotted>`, `value: <any>`; `result.data[path]` equals expected.
- `tool_data_gte` — same `path`, numeric `>= value`.
- `tool_data_present` — `path: <dotted>`; non-null entry at the path.
- `tool_summary_contains` — `value: "<substring>"`; the human-readable summary contains it.

Each assertion defaults to checking the **last** step's result. Use `step: <index>` to target an earlier step.

## Running

```bash
cmake --preset dev -DSOUXMAR_BUILD_EXAMPLES=ON
cmake --build --preset dev

build/dev/tools/eval/souxmar-eval evals/v1 \
    --plugin-path build/dev/examples/plugins/hello-mesher \
    --plugin-path build/dev/examples/plugins/grid-mesher \
    ...
```

`--plugin-path` is repeatable; in CI the workflow points at every built plugin directory. The runner exits 0 iff every task passes; per-task `[PASS]` / `[FAIL]` lines + a per-category summary go to stdout.

## Adding a new task

1. Write `evals/v1/<your-id>.yaml`.
2. Run `souxmar-eval evals/v1 --only <your-id>` locally.
3. Open a PR. The nightly eval CI gate (Sprint 7 push 4) catches regressions in the existing 30 as a side effect.

The task catalogue grows by ratchet — never shrinks, never has a task's pass criterion loosened. The `auditing-determinism` skill describes how new tasks interact with the per-platform-byte-identical claim.
