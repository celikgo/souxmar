# ADR-0045: Agent tool contract ratchet — manufacturing tools 19–24

- **Status:** Proposed
- **Date:** 2026-08-17 (manufacturing block push 1)
- **Author:** celikgokhun
- **Deciders:** AI, desktop, core, DX
- **Tier:** 2 (standard — additive-only tool ratchet under the [ADR-0010](0010-tool-contract-v1-freeze-candidate.md) rule that ADR-0011 inherited; enforced by `scripts/check-tool-contract.sh`). A change to the **name**, **category**, **confirmation tier**, or documented input/output shape of any of tools 1–18, or any edit to `include/souxmar/ai/tool.h`, would be Tier 3 and is not part of this change.
- **Affects:** AI (`src/ai/tools/` — six new tool factories, `default_registry.cpp`), tests (`tests/unit/test_ai_tools.cpp` count assertion 18 → 24), docs (`docs/AI_INTEGRATION.md` tool table, `docs-site/agents/` overview), CLI (`souxmar agent list` output grows). **`include/souxmar/ai/tool.h` does not change.**

## Context

The agent tool contract was frozen final at **18 tools** by [ADR-0011](0011-tool-contract-v1-final-freeze.md), superseding the freeze-candidate [ADR-0010](0010-tool-contract-v1-freeze-candidate.md). What is frozen is each tool's *name*, *category*, *confirmation tier*, and the *shape of its `inputs` and `data`* as documented in `input_schema_doc` / `output_schema_doc`. A tool's LLM-facing description is documentation, not contract, and may be improved freely.

ADR-0010 § "What's still mutable" defines the ratchet that ADR-0011 inherited verbatim. Rule 1 is the one this change uses:

> **Additive new tools.** A new tool added to the default catalogue bumps an implicit minor count and is allowed. Commit message marker: `Ratchet: additive tool (ADR-0010)`. The unit-test count assertion in `tests/unit/test_ai_tools.cpp` is updated in the same commit.

`scripts/check-tool-contract.sh` is the CI gate. It watches two contract-surface files — `include/souxmar/ai/tool.h` and `src/ai/tools/default_registry.cpp` — and fails a PR that touches either without a recognised marker in the commit range. The ADR-0010 spelling of the marker is preserved deliberately across the freeze-final transition so reviewer muscle memory carries through.

The manufacturing block ([ADR-0044](0044-manufacturing-capability-namespaces.md)) adds eighteen capabilities that the agent cannot reach today. Without tools, the chat surface can *describe* an AM pipeline but not propose, run or summarise one, which fails the architectural commitment that "the agent goes through the same tool surface a user could script." Six tools close that gap: one planner, three read-only analyses, and two BC-staging tools.

One inconsistency to record rather than paper over: `docs/AI_INTEGRATION.md` still carries the sentence "Adding a tool requires an RFC." ADR-0010's ratchet rule 1 and ADR-0011's inheritance of it are the operative rule for the *additive* case, and [RFC-0011](../rfcs/0011-calculix-solver-adapter.md) § "Recap" states the same reading explicitly when it adds six CalculiX tools. This block also ships [RFC-0012](../rfcs/0012-am-process-simulation.md), so even under the strictest reading of that sentence the block is RFC-backed. The doc sentence should be reconciled with the ratchet vocabulary in the same pass that updates the tool table.

## Decision

**The default catalogue goes from 18 tools to 24 by pure addition. Tools 1–18 keep their names, categories, confirmation tiers, schemas and registration order untouched. The commit carrying the contract-surface change is marked `Ratchet: additive tool (ADR-0010)`.**

### The six tools

| # | Name | Category | Confirmation | Purpose |
| --- | --- | --- | --- | --- |
| 19 | `propose_am_setup` | `Pipeline` | `Auto` | Propose a complete, runnable AM pipeline for a process + material + machine |
| 20 | `check_printability` | `Mesh` | `Auto` | Run the DfAM capability and summarise blockers |
| 21 | `set_build_orientation` | `BC` | `ConfirmOnce` | Score candidate orientations; stage the winning build direction |
| 22 | `estimate_build_cost` | `Mesh` | `Auto` | Build time / energy / mass / cost estimate |
| 23 | `apply_hydrostatic_load` | `BC` | `ConfirmOnce` | Stage a depth-derived pressure load case |
| 24 | `check_marine_integrity` | `Field` | `Auto` | Corrosion / galvanic / collapse-margin advisory summary |

Categories reuse the existing vocabulary exactly — `Pipeline`, `Mesh`, `BC`, `Field` are all already in the frozen table. No new category is introduced, so the desktop's category grouping and the docs-site table need no new rows, only new cells.

**Terminology note:** the capability contract for this block writes the no-prompt tier as `Confirmation::None`. The enum in `include/souxmar/ai/tool.h` spells it `Confirmation::Auto` (the three tiers are `Auto`, `ConfirmOnce`, `ConfirmAlways`). Since `tool.h` is a frozen contract surface that this change must not touch, the implementation uses `Auto` and each tool's source header records the mapping. There is no third semantic; it is one tier under two names, and `Auto` is the canonical one.

### Why each tier

The rule the existing 18 tools follow: **`Auto` for read-only or trivially-idempotent work; `ConfirmOnce` for anything that mutates session state a later solve consumes; `ConfirmAlways` for file writes, network egress, and expensive solves.** The six new tools are placed by that rule, not by domain.

- **19 `propose_am_setup` — `Auto`.** Runs no solver, writes no file, reaches no network. It returns a stage list. It *does* write `session_state["manufacturing"]` so tools 21 and 22 can consume the setup, but that write is idempotent (same input, same key) and additive (no other session key is touched) — the same reasoning that keeps `propose_pipeline` at `Auto`. The user still has to accept the proposal before anything runs, exactly as with `propose_pipeline` and `propose_cfd_setup`.
- **20 `check_printability` — `Auto`.** Read-only inspection. It dispatches `solver.am.printability` against the session mesh through the `query_mesh_quality` path (mesh wrapped as a one-off `StageOutput` under `__session_mesh__`), which is bounded closed-form per-cell arithmetic over one mesh, idempotent, no file, no network. `query_mesh_quality` sets the precedent: an implicit bounded dispatch does not by itself earn a prompt.
- **21 `set_build_orientation` — `ConfirmOnce`.** It mutates session state that the next solve consumes — `session_state["boundary_conditions"]` with a discriminating `type`, plus `session_state["manufacturing"]`. Build orientation silently changes distortion, overhang, support need, build time and cost, i.e. every downstream AM answer, so the first call per session surfaces a chip. It is local and reversible, so subsequent calls are silent. Same bargain as `set_bc`.
- **22 `estimate_build_cost` — `Auto`.** No plugin dispatch at all: closed-form arithmetic over the staged setup plus the session mesh's bounding box. Read-only, no file, no network. It is the cheapest tool in the block.
- **23 `apply_hydrostatic_load` — `ConfirmOnce`.** Stages a pressure load case in `session_state["boundary_conditions"]` with `type == "hydrostatic"`. Identical shape and identical risk to `set_bc` / `apply_inlet` / `apply_wall` / `apply_outlet`, all of which are `ConfirmOnce`. Placing it anywhere else would make the BC category incoherent.
- **24 `check_marine_integrity` — `Auto`.** Read-only inspection: two bounded closed-form dispatches (`solver.marine.hull_collapse`, `solver.marine.corrosion`) over one mesh, summarised. It writes nothing. Its *output* is advisory in the sense of `MARINE.md`'s scope statement, but "the answer needs a disclaimer" is not the same as "the call needs a prompt" — the disclaimer travels in the tool's own result text and in the report writer's output, where it belongs.

None of the six is `ConfirmAlways`. Nothing in this block writes a file from the agent: the AM writers (`writer.am.gcode`, `writer.am.cli`, `writer.am.report`, `writer.marine.qualification_report`) are reached through a pipeline stage and, from chat, through the existing `export_results` / `apply_pipeline_diff` tools, which already carry the file-write tier. Adding a seventh tool that writes a G-code file directly would have needed `ConfirmAlways` and was left out of this block deliberately.

### Session-state vocabulary

Reused, not extended, except for one new key:

| Key | Written by | Shape |
| --- | --- | --- |
| `boundary_conditions` | 21, 23 (existing writers: `set_bc`, `apply_inlet`, `apply_wall`, `apply_outlet`) | list; each entry carries a discriminating `type` (`"hydrostatic"` for tool 23) |
| `manufacturing` | 19, 21 | **new key** — the staged AM setup (process, material, machine, build direction) that tools 21 and 22 read |

The new key is additive: no existing tool reads or writes it, and a session that never calls tool 19 never has it. Saved sessions from before this change replay identically.

### Files that must move together

The ratchet is only coherent if the whole set lands in one commit. The gate checks the marker; this list is what a reviewer checks.

| File | Change |
| --- | --- |
| `src/ai/tools/propose_am_setup.cpp` | new — tool 19 factory |
| `src/ai/tools/check_printability.cpp` | new — tool 20 factory |
| `src/ai/tools/set_build_orientation.cpp` | new — tool 21 factory |
| `src/ai/tools/estimate_build_cost.cpp` | new — tool 22 factory |
| `src/ai/tools/apply_hydrostatic_load.cpp` | new — tool 23 factory |
| `src/ai/tools/check_marine_integrity.cpp` | new — tool 24 factory |
| `src/ai/tools/default_registry.cpp` | **contract surface** — six forward declarations + six `r.add(...)` calls appended *after* the existing 18, order preserved |
| `src/ai/CMakeLists.txt` | the six new sources |
| `tests/unit/test_ai_tools.cpp` | catalogue-count assertion 18 → 24, plus per-tool name/category/confirmation assertions for 19–24 |
| `docs/AI_INTEGRATION.md` | tool table rows; reconcile the "adding a tool requires an RFC" sentence with the ADR-0010 ratchet |
| `docs-site/agents/index.md` | "frozen v1, 18 tools" → 24, category table cells |
| `docs/adr/0045-…` | this ADR |

`docs-site/agents/tools.md` is generated from the engine binary by `scripts/docs-site/gen-agent-tools.py` and must **not** be hand-edited; it picks the new tools up on the next docs-site build.

Commit message marker, verbatim:

```
Ratchet: additive tool (ADR-0010)
```

### What stays out of scope

- **No change to `include/souxmar/ai/tool.h`.** No new `ToolContext` field, no new confirmation tier, no new error code. If a tool needed one, that would be a separate ratchet with the `Ratchet: additive context field (ADR-0010)` marker.
- **No renaming, re-tiering or re-categorising of tools 1–18.**
- **No AM-specific writer tool.** See above.
- **No tool for the lattice generator.** `reader.lattice` is reachable from `propose_am_setup`'s stage list and from `apply_pipeline_diff`; a dedicated "generate a lattice" tool would need a file-write tier for the spec file and has no demand signal yet.
- **No orientation *optimiser*.** Tool 21 scores a candidate set (the six axis-aligned directions plus anything supplied) and ranks them. A continuous search is a solver problem, not a tool problem.

## Alternatives considered

### One `manufacturing` mega-tool with a `mode` argument

Register a single tool taking `{mode: "printability" | "cost" | "orientation" | "marine" | …}`, keeping the catalogue at 19.

Rejected. It looks cheaper and is more expensive. The confirmation policy is *per tool*, and this block genuinely needs two tiers — a mode switch would force either a prompt on read-only inspection (training users to click through prompts, the worst outcome) or no prompt on session mutation (defeating the policy). It also destroys the typed-schema property: `input_schema_doc` would have to describe a union whose valid keys depend on `mode`, which is exactly the untyped surface the tool contract exists to avoid, and it makes the per-tool audit-log entry uninformative. Finally it is a *worse* fit for the model: six narrow tools with narrow schemas produce better tool selection than one wide tool with a discriminator.

### No agent tools at all — YAML and the desktop panels only

Ship the capabilities and let users reach them through the pipeline editor and the new Manufacturing / Marine panels.

Rejected because it breaks the architectural commitment in `ARCHITECTURE.md` § "the desktop app, the CLI, the Python bindings, and the AI agent are all peers." A capability the agent cannot reach is a capability the chat surface can only talk about, and "the chat is a control surface, not a documentation lookup" is the stated design philosophy. The AM audience is also precisely the one the agent helps most: a process engineer knows their machine and their alloy but not souxmar's eighteen capability ids and their input keys — `propose_am_setup` exists to bridge exactly that.

### A v2 tool catalogue, parallel-loaded

Treat 24 tools as a new contract version, keep v1 loadable for a deprecation window, per ADR-0010's escape hatch.

Rejected as wildly disproportionate. The escape hatch exists for changes that *break* the v1 contract; nothing here does. A parallel catalogue would double the surface the desktop, CLI and eval suite carry, for a change whose entire effect on an existing session is that `souxmar agent list` prints six more rows.

### Defer the two marine tools (23, 24) to a later block

Ship the four AM tools now, add marine tools when the marine capabilities have users.

Rejected because the marine capabilities are what make this block's story coherent — an AM part for subsea service is the motivating use case, and `check_marine_integrity` is the tool that surfaces the advisory framing in chat rather than leaving the user to find `MARINE.md`. Splitting the ratchet also costs two gate events and two doc-update passes instead of one, and leaves `docs/AI_INTEGRATION.md` describing a half-block.

## Consequences

### Positive

- **The whole manufacturing block is reachable from chat** through the same dispatcher, the same confirmation policy and the same audit log as everything else. No privileged path was created for it.
- **The additive ratchet is exercised a second time and it held.** ADR-0010 designed for exactly this; a six-tool domain pack landing without touching a frozen attribute is the evidence that the design works.
- **Category vocabulary unchanged.** `Pipeline` / `Mesh` / `BC` / `Field` absorb all six, so the desktop grouping, the docs-site table and the model's mental map of the surface do not change shape.
- **Confirmation policy stays legible.** Read-only inspection is silent; anything that changes what the next solve computes prompts once. A user can predict which is which without reading the table.
- **`propose_am_setup` validates its own output.** Every capability id it can emit is checked against an in-file allow-list, and every `postproc.*` stage it emits carries `field: {from: …}` — so the planner cannot hand the model an un-runnable pipeline or invent an id the host cannot dispatch. That property is worth more than the planner itself.

### Negative

- **Catalogue growth of one third in a single change.** 18 → 24 tools means a longer system prompt (more tokens per turn, on every provider), a longer `souxmar agent list`, more eval cases in CI, and more surface for the model to choose wrongly from. The tool-selection quality of the *existing* 18 is affected by the presence of six more, and that effect is only measurable in the eval suite.
- **Two names for one tier.** The capability contract says `Confirmation::None`, the header says `Confirmation::Auto`. Six source headers now carry a paragraph explaining that they are the same thing. The alternative — touching a frozen header to add an alias — is worse, but this is a real papercut for the next reader.
- **`docs/AI_INTEGRATION.md` needs a correction, not just an addition.** Its "adding a tool requires an RFC" sentence has been out of step with ADR-0010's ratchet since ADR-0011 landed; this block is the second one to work around it (RFC-0011 was the first). Leaving it is a governance smell.
- **Session-state coupling between tools 19, 21 and 22.** Tool 22 reads a setup tool 19 staged. If the user calls 22 first, it has to fall back to defaults — which it does, but the answer then reflects defaults the user never chose, and the model has to notice. Coupling through an untyped session bag is the existing pattern (`set_bc` → `solve`), so this is consistent, not novel — and it is still the weakest joint in the surface.
- **Six more places where a domain default is baked into the AI layer.** The machine and material tables inside `propose_am_setup` duplicate the desktop panels' tables and the curated `examples/materials/am-marine.toml`. Three copies of a number is two too many; the third consumer is the signal to factor out a materials module.

### Risks

- **Risk:** the model picks `check_marine_integrity` for a non-marine part and reports a collapse margin for a bracket. **Mitigation:** the tool's description and output state that the collapse numbers are hull-form arithmetic keyed to the *input geometry*, not the mesh; the result carries the advisory framing. An eval case covers the "wrong-domain" prompt.
- **Risk:** a user reads `estimate_build_cost` as a quote. **Mitigation:** the tool's own output names its assumptions (the `fill_fraction` stand-in for the real part volume, no supports, no labour, no post-processing) and points at `solver.am.buildtime` for the layer-resolved answer. Same class of risk as every other closed-form number in the block, same mitigation: say it in the output, not only in the docs.
- **Risk:** `set_build_orientation`'s `ConfirmOnce` chip is dismissed once and then orientation changes silently for the rest of the session — the tier's inherent trade-off. **Mitigation:** the audit log records every invocation with its input hash, and the desktop surfaces the staged orientation in the Manufacturing panel rather than only in chat.
- **Risk:** the count assertion in `tests/unit/test_ai_tools.cpp` and `default_registry.cpp` drift apart in a later block, and the gate passes on a marker while the catalogue is inconsistent. **Mitigation:** the assertion is a hard equality on the registry size, so drift is a test failure, not a warning.
- **Risk:** eval-suite runtime. Six new tools mean at least six new eval cases. **Mitigation:** all six are closed-form and sub-second; the block adds no subprocess adapter and no long solve, so the CI budget impact is small relative to the CalculiX block's.

## References

- [ADR-0010](0010-tool-contract-v1-freeze-candidate.md) — the freeze candidacy that defines the additive-tool ratchet and its commit marker.
- [ADR-0011](0011-tool-contract-v1-final-freeze.md) — the final freeze at 18 tools, inheriting ADR-0010's ratchet vocabulary.
- [ADR-0044](0044-manufacturing-capability-namespaces.md) — the capabilities these tools drive.
- [RFC-0012](../rfcs/0012-am-process-simulation.md) — the block's RFC; physics roadmap and validation plan.
- [RFC-0011](../rfcs/0011-calculix-solver-adapter.md) § Recap — the precedent reading that additive tools do not need an RFC bump on the contract itself.
- [`docs/AI_INTEGRATION.md`](../AI_INTEGRATION.md) — tool surface, confirmation policy, audit log.
- [`docs/MANUFACTURING.md`](../MANUFACTURING.md) and [`docs/MARINE.md`](../MARINE.md) — what the tools report and the scope of those reports.
- [`docs/GOVERNANCE.md`](../GOVERNANCE.md) — merge tiers.
- `scripts/check-tool-contract.sh` — the CI gate and its recognised markers.
- `include/souxmar/ai/tool.h` — the frozen framework header this change does not touch.
- `src/ai/tools/default_registry.cpp` — the contract-surface file this change appends to.
- `.claude/skills/adding-agent-tool/` — the walkthrough a contributor follows for the next one.

## History

- 2026-08-17 (manufacturing block push 1): Proposed. Catalogue 18 → 24; `Ratchet: additive tool (ADR-0010)`.
