# Agent reference

souxmar's agentic AI chat is the desktop app's primary control
surface for engineers who don't want to write Python or YAML. The
agent calls the same tool dispatcher anything else does — there is
no privileged path.

## Tool surface (frozen v1, 18 tools)

The agent tool contract is **frozen final at v1** per
[ADR-0011](https://github.com/souxmar/souxmar/blob/master/docs/adr/0011-tool-contract-v1-final-freeze.md).
Eighteen tools across these categories:

| Category   | Tools                                                                      |
| ---------- | -------------------------------------------------------------------------- |
| Read       | `read_geometry_summary`                                                    |
| Mesh       | `mesh`, `query_mesh_quality`                                               |
| BC         | `set_bc`, `apply_inlet`, `apply_wall`, `apply_outlet`, `validate_bcs`      |
| Material   | `set_material`                                                             |
| Solve      | `solve`                                                                    |
| Field      | `query_field`, `compute_field`                                             |
| Pipeline   | `propose_pipeline`, `apply_pipeline_diff`, `propose_cfd_setup`             |
| Discovery  | `list_plugins`                                                             |
| Export     | `export_results`                                                           |
| UI         | `screenshot_viewport`                                                      |

See [the tool catalogue page](/agents/tools) for each tool's input
schema + output shape + confirmation policy.

## Confirmation policies

Each tool carries one of three policies. The desktop app surfaces
a prompt for `confirm-once` and `confirm-always`; the CLI requires
`--yes` for either.

| Policy             | Used by                                              |
| ------------------ | ---------------------------------------------------- |
| `auto`             | read-only tools (`read_geometry_summary`, `query_*`, `list_plugins`) |
| `confirm-once`     | side-effecting but contained (`mesh`, `set_bc`, `solve`, `compute_field`) |
| `confirm-always`   | filesystem writes (`export_results`, `screenshot_viewport`) |

See [the confirmation page](/agents/confirmation) for the full
matrix + how to override per-project.

## Providers

The Provider abstraction (Sprint 10 push 9) lets the agent talk to
multiple LLM providers:

| Provider          | Status   | Notes                                              |
| ----------------- | -------- | -------------------------------------------------- |
| Anthropic (Claude) | Sprint 14 | The recommended path; best tool-use behaviour     |
| OpenAI (GPT)      | Sprint 14 | Solid; cheaper than Anthropic for high-volume use  |
| Ollama (local)    | **Available v0.9.0** | Llama-3.1, Qwen-2.5, Mistral-Nemo verified |

Per-model compatibility for Ollama:
[`docs/ai-providers/ollama-compatibility.md`](https://github.com/souxmar/souxmar/blob/master/docs/ai-providers/ollama-compatibility.md).

## Audit log

Every tool dispatch lands in `.souxmar/chat/audit.log` per-project.
Fields:

- Timestamp (RFC-3339 UTC)
- Tool name + input hash + output summary
- `consumed_input_tokens` / `consumed_output_tokens`
- `heap_bytes_delta` on supported platforms (Linux + glibc ≥ 2.33)
- `latency_ms` (Sprint 9 push 10)

See [the audit-log page](/agents/audit-log) for the schema +
common queries.

## Eval suite

We run 43 scripted eval tasks against every PR (`evals/v1/`,
expanding to 60 by Sprint 12). The nightly gate requires
≥ 90 % pass-rate. There's also an LLM-driven eval surface
(`evals/v1-llm/`) that exercises the full model-emits-a-tool-call
loop with a configurable provider; see
[`souxmar-eval-llm`](https://github.com/souxmar/souxmar/tree/master/tools/eval-llm).

## What the agent will NOT do

- Touch your filesystem outside `--target-root` directories you
  explicitly approved.
- Send your geometry or analysis results to a non-BYOK provider.
  Pro tier's managed AI runs in a project-scoped proxy you opt
  into; the BYOK path stays the default.
- Bypass `confirm-always` policies (file writes, exports) without
  an explicit `--yes` flag or per-tool override.

## What the agent CAN do (eventually) but doesn't yet

- Edit your pipeline YAML in place. Today it `propose_pipeline_diff`s;
  applying the diff still requires the user (or the CLI's
  `apply_pipeline_diff` tool with `--yes`).
- Run a long-running CFD solve and check back later. The current
  surface is synchronous; long-running orchestration lands in
  Sprint 17 alongside the hosted compute offload story.
- Schedule recurring runs. Out of scope for v1.
