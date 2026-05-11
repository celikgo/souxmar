# ADR-0010: Agent tool-contract v1 freeze candidate

- **Status:** **Superseded by [ADR-0011](0011-tool-contract-v1-final-freeze.md)** (Sprint 9 push 1).
  This ADR is preserved for historical context; the v1 final freeze
  declared in ADR-0011 is the binding source from 2026-05-11 forward.
- **Date:** 2026-05-11 (Sprint 8 push 5)
- **Author:** souxmar AI team
- **Deciders:** AI, core, plugin-host, DX, platform
- **Tier:** 3 (heavy / requires RFC)
- **Affects:** agent tool catalogue, `souxmar::ai::Tool*` surface, governance, eval harness

## Context

The agent tool framework landed in [Sprint 4 push 3](../SPRINT_PLAN.md)
with five tools. The catalogue has grown additively every sprint
since — Sprint 5 push 2 (8), Sprint 6 push 1 (9), Sprint 6 push 3 (13),
Sprint 8 push 4 (16), Sprint 8 push 5 (18). Every addition has
followed the same shape — declare a factory, document the input /
output schema, drop the source into `src/ai/tools/`, register it in
`default_v1_tools()`, write unit tests against the registry. No
existing tool has had its name, schema, or confirmation tier changed
since landing.

The Sprint 7 push 4 agent eval suite (`tools/eval`, 30 canonical tasks)
exercises the catalogue end-to-end across the three default providers
(Anthropic / OpenAI / Ollama). Pass rates have been ≥ 90% on the
nightly matrix since Sprint 7 push 6 (v0.9.0-beta1). The catalogue is
stable in shape, stable in behaviour, and exercised in CI.

This ADR declares the agent tool contract a **freeze candidate**,
mirroring the ABI freeze-candidate process from
[ADR-0007](0007-abi-v1-freeze-candidate.md) → [ADR-0008](0008-abi-v1-final-freeze.md).
A soak period follows; the candidate becomes the **final** v1 freeze
in a future ADR (target: Sprint 9 push 1) when the soak gates clear.

## Decision

The agent tool contract — both the **framework surface** in
`include/souxmar/ai/tool.h` and the **default v1 catalogue** of 18 tools
in `src/ai/tools/` — is a **frozen candidate** as of **2026-05-11**.

Tools may be added to the catalogue during the soak via the ratchet
process below (additive only). The framework surface itself is locked.

The candidate becomes the v1 **final** freeze when:

1. The catalogue has soaked for ≥ 1 sprint with no breaking change
   needed to any tool's schema, name, confirmation tier, or category.
2. The agent eval suite (`tools/eval`) reports ≥ 90% pass rate on every
   default provider for the duration of the soak.
3. No `agent-tool-v1` issue tagged "breaking" is open at the gate.

### What is locked (framework surface)

Every type declared in `include/souxmar/ai/tool.h`:

| Type / function                | Lock scope                                                       |
| ------------------------------ | ---------------------------------------------------------------- |
| `enum class Confirmation`      | `Auto` / `ConfirmOnce` / `ConfirmAlways`; underlying `uint8_t`   |
| `struct ToolError`             | Field names + types: `code`, `message`, `suggestion`             |
| `struct ToolResult`            | Field names + types: `data`, `summary`, `error`                  |
| `struct ToolContext`           | Field names + types: registry / dispatcher / cache / session_state / owned_session_state / geometry_handle / mesh_handle / field_handle / audit_log / budget. New fields may be appended via the ratchet; existing field order and types are immutable. |
| `struct Tool`                  | Field names + types: name / description / category / confirmation / input_schema_doc / output_schema_doc / handler |
| `class ToolRegistry`           | Public method signatures: `add(Tool)`, `find(string_view)`, `list()`, `size()` |
| `struct ConfirmationPolicy`    | Field names + types: overrides / confirmed_once / prompter       |
| `dispatch_tool()`              | Signature + the five-step contract documented in the header     |
| `default_v1_tools()`           | Returns a registry containing **at least** the 18 named tools below |

Specifically: type names, method signatures, struct field order +
types, enum underlying type + value semantics. A tool's *handler
implementation* may change (a heuristic refinement, a bug fix) but its
*name*, *category*, *confirmation tier*, and *schema docs* are locked.

### What is locked (catalogue — 18 tools)

The default v1 catalogue locked at this candidate:

| Category    | Tool name                  | Confirmation   | Mutates session? |
| ----------- | -------------------------- | -------------- | ---------------- |
| Read        | `read_geometry_summary`    | Auto           | No  |
| Read        | `query_field`              | Auto           | No  |
| Read        | `query_mesh_quality`       | Auto           | No  |
| Mesh        | `mesh`                     | ConfirmOnce    | Yes (mesh handle) |
| BC          | `set_bc`                   | ConfirmOnce    | Yes (boundary_conditions) |
| CFD         | `apply_inlet`              | ConfirmOnce    | Yes (boundary_conditions) |
| CFD         | `apply_wall`               | ConfirmOnce    | Yes (boundary_conditions) |
| CFD         | `apply_outlet`             | ConfirmOnce    | Yes (boundary_conditions) |
| CFD         | `propose_cfd_setup`        | Auto           | No  |
| CFD         | `validate_bcs`             | Auto           | No  |
| Material    | `set_material`             | ConfirmOnce    | Yes (materials) |
| Solve       | `solve`                    | ConfirmOnce    | Yes (field handle) |
| Field       | `compute_field`            | ConfirmOnce    | Yes (field handle) |
| Pipeline    | `propose_pipeline`         | Auto           | No  |
| Pipeline    | `apply_pipeline_diff`      | ConfirmOnce    | Yes (pipeline draft) |
| Discovery   | `list_plugins`             | Auto           | No  |
| Export      | `export_results`           | ConfirmOnce    | No (file I/O)    |
| UI          | `screenshot_viewport`      | Auto           | No  |

Locked attributes: tool **name** (string), tool **category**,
**confirmation tier**, and the **shape of `inputs` and `data`** as
documented in `input_schema_doc` / `output_schema_doc`. A tool's
*description* (the LLM-facing one-paragraph blurb) may be improved
without a ratchet — descriptions are documentation, not contract.

### What's still mutable (the ratchet, candidate-period)

Mirrors the ABI ratchet:

1. **Additive new tools.** A new tool added to the default catalogue
   bumps an implicit minor count and is allowed. Commit message marker:
   `Ratchet: additive tool (ADR-0010)`. The unit-test count assertion
   in `tests/unit/test_ai_tools.cpp` is updated in the same commit.
2. **New optional fields on `ToolContext`.** Appending a field with a
   default value that does not affect existing tools' behaviour is
   allowed. Commit message marker: `Ratchet: additive context field (ADR-0010)`.
3. **Bug fixes** to a tool's handler when the documented schema is
   correct and the implementation diverged. No marker required; covered
   by the tests.
4. **Description / schema-doc copy edits** that do not change behaviour.
   No marker required.

Any other edit to the framework surface or to a locked tool attribute
fails the gate. The escape hatch is a Tier-3 ADR explicitly accepting
the change and committing the project to either an additive bump
(extending the contract) or to a v2 tool catalogue (parallel-loaded
during the deprecation window — same model as the ABI's major-bump
strategy in ADR-0008).

### What CI now enforces

A new `scripts/check-tool-contract.sh` guard (landing alongside this
ADR) blocks PRs that:

- Rename / remove a tool factory function in `src/ai/tools/`.
- Change the `name`, `category`, or `confirmation` field of a tool in
  any of the registered factories.
- Modify `include/souxmar/ai/tool.h` without the
  `Ratchet: additive context field (ADR-0010)` marker.

The script is opt-in during the candidate period (CI runs it as a
non-blocking job) and flips to blocking when the v1 final freeze ADR
lands.

### Versioning policy from this point on

- **Sprint 8 → final freeze**: catalogue may grow via the ratchet; the
  framework surface is locked. Soak period.
- **Final freeze (target Sprint 9 push 1)**: the catalogue locks at
  whatever it has become through the ratchet. From there:
- **Patch / minor sprints post-final**: catalogue grows additively only.
- **v2 catalogue** (i.e. souxmar 2.0 era): only when a non-additive
  change is genuinely needed. Parallel-loaded during a deprecation
  window — host can simultaneously expose v1 and v2 tool registries
  to the agent runtime.

## Alternatives considered

### Skip the candidate, freeze final today

Pro: shorter ceremony; the catalogue is already shipping in v0.9.0-beta1
and has been exercised across three providers. Con: the ABI process
proved its worth precisely *because* the candidate period was real —
the Sprint 6 push 4 `reader.*` ratchet event surfaced during the
ABI soak. Skipping the candidate for the tool contract would burn the
same insurance for marginal time savings. The candidate period is
cheap.

### Freeze only the framework surface; leave the catalogue open

Pro: maximum flexibility on tool definitions during Sprint 9+ work.
Con: third-party tooling (eval harness, plugin index, IDE integrations)
need *some* stable catalogue to target. Without naming the 18 tools,
the contract is hollow. The ratchet for additive tools is the right
compromise.

### Freeze at a smaller catalogue (say, the 13 from Sprint 6 push 3)

Pro: fewer tools under the lock, less surface to maintain. Con: the
five tools added in Sprint 8 (push 4 + push 5) are exactly the CFD
vocabulary that justified the freeze candidate timing. Without them,
the catalogue is FEM-shaped and incomplete for the Sprint 8 push 6
pipe-bend example. Freezing at 18 captures the post-Sprint-8 state.

## Consequences

### Positive

- **Agent eval suite** can declare a v1 baseline that downstream model
  releases (3rd-party fine-tunes, embedded agents) target. The 30
  canonical tasks in `tools/eval` already reference tool names and
  schemas; locking them turns the harness from internal QA into a
  public spec.
- **Plugin index integration** (planned Sprint 9): the index can
  declare tool-compatibility for plugin entries. A "supports
  `apply_inlet`" badge is meaningful once the tool's name + schema is
  pinned.
- **Documentation surface**: `docs/AI_INTEGRATION.md` can stop using
  hedge language ("the v1 tools include …") and start naming the
  catalogue as authoritative.

### Negative

- Any oversight in a tool schema is now expensive to fix. The
  pre-mortem in §Pre-mortem names the most likely cases.
- The catalogue's growth is now governed: a new domain tool requires
  consideration of whether it belongs in v1 or in a future v2.

### Risks

- **Risk:** A tool's `data` schema turns out to be ambiguous in
  practice (e.g. `query_field` returns inconsistent shapes for nodal
  vs. cell-based fields). **Mitigation:** The schema doc is locked but
  the *handler* is not; a clarification that tightens the implementation
  without changing the documented shape is a bug fix. If the doc itself
  needs to change, that's a candidate-period ratchet event — surface it
  before the final freeze.
- **Risk:** Adding a tool late in the soak forces another candidate
  pass. **Mitigation:** Same rule as the ABI's ratchet: additive new
  tools do not reset the clock; only contract changes do.
- **Risk:** Provider-specific tool behaviour (Anthropic vs. OpenAI
  schema rendering) diverges across the soak. **Mitigation:** The eval
  suite's per-provider score gate catches drift; the tool contract is
  a souxmar-side abstraction, and per-provider mapping lives in the
  serializer (which is not under freeze).

## Pre-mortem (one year from today)

It is 2027-05-11 and the freeze went badly. The most likely failure
mode: `set_bc` and the three `apply_*` siblings collided on a real
case where the agent staged both kinds and a CFD solver picked up the
wrong shape. The workaround threaded a `vocabulary: 'cfd' | 'fem'`
field through every BC entry, which works but is awkward. A v2 tool
catalogue proposal floats but doesn't reach consensus.

Leading indicators to watch:

- Whether the Sprint 8 push 6 pipe-bend example needs to *hide* one of
  the two BC vocabularies from the agent at session start (a sign the
  contract's coexistence isn't natural).
- Filed `agent-tool-v1` issues tagged "schema-clarification" in the
  first 90 days post-final-freeze — target zero showstoppers.
- Eval suite per-provider divergence: if Anthropic and OpenAI start
  using different tool subsets to solve the same canonical task, the
  contract is under-specified.

## References

- ADR-0007 — ABI v1 freeze candidate (process precedent).
- ADR-0008 — ABI v1 final freeze (target shape for the v1 final freeze
  this ADR's candidate becomes).
- `include/souxmar/ai/tool.h` — the framework surface under lock.
- `src/ai/tools/default_registry.cpp` — the 18-tool catalogue.
- `tests/unit/test_ai_tools.cpp` — registry-count + per-tool gate.
- `tools/eval/` — the agent eval suite (Sprint 7 push 4).
- `docs/AI_INTEGRATION.md` — consumer-side contract.
- `docs/GOVERNANCE.md` § Tier-3 ADRs — process.

## History

- 2026-05-11 (Sprint 4 push 3): framework + 5 tools land.
- 2026-05-11 (Sprint 5 push 2): catalogue grows to 8.
- 2026-05-11 (Sprint 6 push 1): catalogue grows to 9.
- 2026-05-11 (Sprint 6 push 3): catalogue grows to 13.
- 2026-05-11 (Sprint 8 push 4): catalogue grows to 16 (CFD-aware BCs).
- 2026-05-11 (Sprint 8 push 5): **freeze candidate declared, this ADR.**
  Catalogue closes at 18 (CFD planner + validator).
- 2026-05-11 (Sprint 9 push 1): **superseded by ADR-0011** — soak cleared,
  v1 final freeze accepted; `check-tool-contract.sh` flipped
  blocking-by-default; `tool-contract-v1-lockdown` CI job lands.
