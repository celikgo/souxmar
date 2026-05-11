# Agent eval suite v1 (extended in Sprint 11 push 3)

Sprint 7 push 4 shipped the first thirty canonical tasks; Sprint 11
push 3 expanded the catalogue toward the SPRINT_PLAN.md § Sprint 11
target of 60 tasks total with a **≥ 90 % pass-rate gate** (see
`--min-pass-rate` in `souxmar-eval --help`). Each task is one YAML
file; pass criteria are deterministic checks on tool outputs.

This is the **scripted** eval surface: each task runs the same
dispatch path a real LLM would drive, but the steps are pre-staged
in YAML rather than chosen by a model. The LLM-driven counterpart
lives under `evals/v1-llm/` and is exercised by `souxmar-eval-llm`
(Sprint 10 push 9); the two surfaces share the task vocabulary but
have different stability promises.

## Catalogue

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
| diff       | 3     | `apply_pipeline_diff` (incl. noop diff path)            |
| export     | 2     | `export_results`                                        |
| listing    | 2     | `list_plugins`                                          |
| cfd        | 3     | `propose_cfd_setup`, `validate_bcs`, `apply_*` chain    |
| multistep  | 3     | Chained tool sequences (mesh → BC → solve → query → export) |
| recovery   | 2     | Session-state-after-error patterns                      |
| screenshot | 1     | `screenshot_viewport`                                    |
| **Total**  | **43** | **all 18 v1 tools**                                    |

Sprint 11 push 3 lands 13 new tasks against the 60-task target. The
remaining 17 ride into Sprint 12 (public alpha) as the catalogue
stabilises against external dogfood feedback. The gate today is
**≥ 90 % pass-rate** across the 43 tasks; tightening to 100 % is a
Sprint 13+ ratchet decision.

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
