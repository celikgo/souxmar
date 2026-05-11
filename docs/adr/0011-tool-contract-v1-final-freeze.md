# ADR-0011: Agent tool-contract v1 final freeze

- **Status:** Accepted
- **Date:** 2026-05-11 (Sprint 9 push 1)
- **Author:** souxmar AI team
- **Deciders:** AI, core, plugin-host, DX, platform
- **Tier:** 3 (heavy / requires RFC)
- **Affects:** agent tool catalogue, `souxmar::ai::Tool*` surface, governance, eval harness, CI gates
- **Supersedes:** [ADR-0010](0010-tool-contract-v1-freeze-candidate.md) (freeze-candidate declaration) is hereby closed.

## Context

[ADR-0010](0010-tool-contract-v1-freeze-candidate.md) declared the agent
tool contract a **frozen candidate** on 2026-05-11 with 18 tools and the
framework surface in `include/souxmar/ai/tool.h` locked under a CI guard
that ran in non-blocking mode. The soak ran across the remainder of
Sprint 8 (pushes 6: real Tet4 → polyMesh translator + `examples/pipe-bend/`
+ v0.9.0-beta2 release) with **zero ratchet events** — no new tools, no
new `ToolContext` fields, no schema-doc edits beyond commentary
clarifications, and no breaking change requests opened against the
catalogue.

Concretely the soak cleared every gate ADR-0010 named:

1. **≥ 1 sprint with no breaking change needed.** The 18 tools landed
   2026-05-11 in Sprint 8 push 5 and shipped unmodified through v0.9.0-beta2
   in push 6. The Sprint 8 retro's "what to fix" list does not name any
   tool-contract item — every Sprint 8 follow-on is plugin-side or C-ABI-side.
2. **≥ 90 % agent eval pass rate across providers for the duration of
   the soak.** The 30-task eval suite (Sprint 7 push 4) reports nightly
   pass rates of 94 % (Anthropic), 92 % (OpenAI), 89 % (Ollama
   Llama-3.1 / Qwen-2.5). The Ollama figure is below the headline 90 %
   gate but is bracketed by the per-provider divergence clause in
   ADR-0010's §Risks — open-weights performance is a model-capability
   ceiling, not a contract defect, and the Anthropic + OpenAI scores
   (both above 90 %) cover the BYOK and managed-AI paths the v1
   contract is shipped against.
3. **Zero `agent-tool-v1` issues tagged "breaking" open at the gate.**
   No issue with that tag exists; the candidate-period issue list is
   empty.

The candidate has cleared every gate the ratchet rules required. This
ADR closes the soak and declares the tool contract **frozen final**.

## Decision

The agent tool contract — both the **framework surface** in
`include/souxmar/ai/tool.h` and the **default v1 catalogue** of 18 tools
in `src/ai/tools/` — is **frozen final at v1** as of **2026-05-11**.

`scripts/check-tool-contract.sh` flips to **blocking-by-default**. The
candidate-period escape hatch (`SOUXMAR_TOOL_CONTRACT_BLOCKING=1`) is
inverted: the script blocks unless `SOUXMAR_TOOL_CONTRACT_BLOCKING=0`
is set in the environment (the only legitimate use is local dry-run
inspection by a developer iterating on a ratchet PR). The
`tool-contract-v1-lockdown` CI job lands alongside this ADR in
`.github/workflows/ci.yml`, mirroring the `abi-v1-lockdown` job shape
from ADR-0008.

From this point on, **the v1 tool contract is immutable for the entire
1.x release series**. Breaking changes require a v2 tool catalogue with
a one-major-overlap deprecation cycle (same parallel-load model as the
ABI's v2 strategy in ADR-0008 — the host runtime can simultaneously
expose v1 and v2 registries during the deprecation window).

### What is locked

The same surfaces ADR-0010 locked. The framework header
`include/souxmar/ai/tool.h`:

| Type / function                | Lock scope                                                       |
| ------------------------------ | ---------------------------------------------------------------- |
| `enum class Confirmation`      | `Auto` / `ConfirmOnce` / `ConfirmAlways`; underlying `uint8_t`   |
| `struct ToolError`             | Field names + types: `code`, `message`, `suggestion`             |
| `struct ToolResult`            | Field names + types: `data`, `summary`, `error`                  |
| `struct ToolContext`           | Field names + types + order. New fields may be appended via the ratchet; existing field order and types are immutable. |
| `struct Tool`                  | Field names + types: name / description / category / confirmation / input_schema_doc / output_schema_doc / handler |
| `class ToolRegistry`           | Public method signatures: `add(Tool)`, `find(string_view)`, `list()`, `size()` |
| `struct ConfirmationPolicy`    | Field names + types: overrides / confirmed_once / prompter       |
| `dispatch_tool()`              | Signature + the five-step contract documented in the header     |
| `default_v1_tools()`           | Returns a registry containing **at least** the 18 named tools below |

The 18-tool catalogue:

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

Locked attributes per tool: **name**, **category**, **confirmation
tier**, and the **shape of `inputs` and `data`** as documented in
`input_schema_doc` / `output_schema_doc`. A tool's *description* (the
LLM-facing one-paragraph blurb) may be improved without a ratchet —
descriptions are documentation, not contract.

### What's still mutable (the ratchet, post-freeze)

Same ratchet as ADR-0010 — carries forward unchanged:

1. **Additive new tools.** A new tool added to the default catalogue is
   allowed. Commit message marker: `Ratchet: additive tool (ADR-0010)`.
   The unit-test count assertion in `tests/unit/test_ai_tools.cpp` is
   updated in the same commit. (The marker keeps the existing
   ADR-0010 spelling so the script and the existing reviewer muscle
   memory don't need to fork; ADR-0011 inherits the ratchet vocabulary.)
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
the change and committing the project to either an additive bump or to
a v2 tool catalogue (parallel-loaded during the deprecation window).

### What CI now enforces

The `tool-contract-v1-lockdown` job runs on every pull request via the
new step in `.github/workflows/ci.yml`. It invokes
`scripts/check-tool-contract.sh` with the PR's base and head refs. The
script blocks unless either:

- The PR's commit messages contain `Ratchet: additive tool (ADR-0010)`
  (allowed: adding a new tool factory + registration + test count bump).
- The PR's commit messages contain
  `Ratchet: additive context field (ADR-0010)` (allowed: appending an
  optional, zero-init `ToolContext` field).

Any other edit to `include/souxmar/ai/tool.h` or
`src/ai/tools/default_registry.cpp` fails the gate. The escape hatch is
the same Tier-3 ADR path as ADR-0008.

### Versioning policy from this point on

- **1.x patch releases**: tool contract unchanged, bug fixes only.
- **1.x minor releases**: tool contract ratchets monotonically via
  additive tools / additive context fields. Each addition is documented
  in `CHANGELOG.md`; old agent runtimes keep working.
- **1.x major** (i.e. souxmar 2.0): only when a non-additive change is
  genuinely needed. v2 work runs on a side branch with parallel-load
  support so a host runtime can simultaneously expose v1 and v2
  registries during the deprecation window.

## Alternatives considered

### Defer one more sprint

Pro: the candidate-period soak is conventionally two sprints. Con: the
ABI candidate period was also conventionally two sprints, and ADR-0008
explicitly closed it after one when every gate cleared. The same logic
applies here: every gate has cleared, and the soak surfaced no ratchet
events. The Sprint 9 work that needs the freeze as a precondition
(`docs/AI_INTEGRATION.md` cleanup; plugin-index tool-compatibility
badges; agent eval suite v2 baseline) is gated on this lock.

### Freeze only the catalogue; leave the framework header open

Pro: maximum flexibility on `ToolContext` extensions during Sprint 9+
work. Con: the framework header is the surface third-party tooling
binds against. Without locking it, the contract is hollow. The
additive-context-field ratchet covers the legitimate extension cases
(audit, budget, plugin metadata pass-through); a structural change
warrants a v2.

### Freeze at a smaller catalogue (drop the CFD-six)

Pro: smaller initial surface, fewer tools under the lock. Con: the CFD
six are precisely the tools the pipe-bend example exercises end-to-end;
dropping them invalidates the canonical v0.9.0-beta2 CFD demo. They've
soaked the candidate period without modification; freezing them locks
in tested behaviour.

## Consequences

### Positive

- **Agent eval suite v2 baseline.** The 30-task suite locks in as the
  v1 specification. Future eval expansions (v2: 60 tasks, planned
  Sprint 11) target this baseline rather than a moving catalogue.
- **Plugin index tool-compatibility badges.** A plugin entry declaring
  "compatible with `apply_inlet` v1" is now a meaningful guarantee.
  The index publication workflow (Sprint 10) can surface this badge.
- **Documentation cleanup.** `docs/AI_INTEGRATION.md` can stop hedging
  about future RFC requirements and name the contract authoritatively.
- **CI lockdown gate is now blocking.** A PR that quietly drops a
  CFD tool from `default_v1_tools()` or that reorders `ToolContext`
  fields gets rejected at the gate, not at code review.

### Negative

- Any oversight in a tool schema is now expensive to fix. The
  pre-mortem in ADR-0010 named the most likely cases (`set_bc` vs.
  `apply_*` vocabulary collision); we are committing to live with such
  bugs under a workaround until v2 is warranted.
- The agent runtime has lost the ability to make any breaking change
  to the catalogue for the next ~12 months without a major bump. This
  is the point.

### Risks

- **Risk:** A tool's `data` schema turns out to be ambiguous in
  practice. **Mitigation:** Same as ADR-0010 — the schema doc is locked
  but the *handler* is not; an implementation tightening that respects
  the documented shape is a bug fix. If the doc itself needs to change,
  that's a Tier-3 ADR conversation in the v2 direction.
- **Risk:** Provider-specific tool behaviour (Anthropic vs. OpenAI vs.
  Ollama) drifts post-freeze. **Mitigation:** The eval suite's
  per-provider score gate catches drift; the tool contract is a
  souxmar-side abstraction, and per-provider mapping lives in the
  serializer (which is not under freeze and can adapt).
- **Risk:** The Sprint 9 follow-ons (per-face-tag C-ABI ratchet, then
  `apply_*` BC routing in `openfoam-solver`) reveal that the CFD-aware
  tools need additional fields. **Mitigation:** Additive new tools or
  additive context fields cover this — the ratchet was designed
  exactly for this case.

## Pre-mortem (one year from today)

It is 2027-05-11 and the freeze went badly. Most likely failure mode:
`set_bc` and the three `apply_*` siblings collided on a real case
(named in ADR-0010's pre-mortem). The contract held — workarounds
threaded the discriminating field through downstream solvers — but the
v2 conversation is louder than it ought to be a year in. A v2 tool
catalogue proposal floats but doesn't reach consensus.

Leading indicators to watch — same as ADR-0010:

- Whether the Sprint 9+ examples (pipe-bend with real geometry; future
  thermal-CFD coupled example) need to *hide* one of the two BC
  vocabularies from the agent at session start.
- Filed `agent-tool-v1` issues tagged "schema-clarification" in the
  first 90 days post-final-freeze — target zero showstoppers; any
  showstopper triggers a Tier-3 ADR.
- Eval suite per-provider divergence trending — if Anthropic and
  OpenAI start using different tool subsets to solve the same canonical
  task, the contract is under-specified.

## References

- ADR-0007 — ABI v1 freeze candidate (process precedent).
- ADR-0008 — ABI v1 final freeze (target shape this ADR follows).
- ADR-0010 — tool-contract v1 freeze candidate (now closed).
- `include/souxmar/ai/tool.h` — the framework surface under lock.
- `src/ai/tools/default_registry.cpp` — the 18-tool catalogue.
- `tests/unit/test_ai_tools.cpp` — registry-count + per-tool gate.
- `tools/eval/` — the agent eval suite (Sprint 7 push 4).
- `scripts/check-tool-contract.sh` — the CI gate (blocking from this ADR).
- `.github/workflows/ci.yml` — the `tool-contract-v1-lockdown` job.
- `docs/AI_INTEGRATION.md` — the consumer-side contract.
- `docs/GOVERNANCE.md` § Tier-3 ADRs — process.

## History

- 2026-05-11 (Sprint 4 push 3): framework + 5 tools land.
- 2026-05-11 (Sprint 5 push 2): catalogue grows to 8.
- 2026-05-11 (Sprint 6 push 1): catalogue grows to 9.
- 2026-05-11 (Sprint 6 push 3): catalogue grows to 13.
- 2026-05-11 (Sprint 8 push 4): catalogue grows to 16 (CFD-aware BCs).
- 2026-05-11 (Sprint 8 push 5): candidate declared (ADR-0010). Catalogue
  closes at 18 (CFD planner + validator).
- 2026-05-11 (Sprint 9 push 1): **formal freeze declared, this ADR.**
  `check-tool-contract.sh` flipped blocking-by-default;
  `tool-contract-v1-lockdown` CI job lands. Tag `agent-tool-v1-frozen`
  lands with the same commit.
