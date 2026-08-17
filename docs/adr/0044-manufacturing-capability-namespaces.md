# ADR-0044: Manufacturing capabilities ship as in-tree plugins under the existing five namespaces

- **Status:** Proposed
- **Date:** 2026-08-17 (manufacturing block push 1)
- **Author:** celikgokhun
- **Deciders:** core, plugin-host, desktop, AI, DX
- **Tier:** 2 (standard — new in-tree plugins, new examples, new docs; **no** ABI, pipeline-format or data-model change). The rejected alternatives below are Tier 3; avoiding that is the substance of this decision.
- **Affects:** plugin SDK (18 new capability ids under existing namespaces), `examples/plugins/` (8 new plugin directories), `examples/` (4 new example projects), docs (`docs/MANUFACTURING.md`, `docs/MARINE.md`), desktop (two new pipeline-editor panels), AI (six new agent tools — declared separately in [ADR-0045](0045-agent-tool-contract-am-ratchet.md)). **Nothing under `include/souxmar-c/` changes.**

## Context

The manufacturing block adds eighteen capabilities covering metal and polymer additive manufacturing plus subsea/marine assessment: a layer-aligned build mesher, a parametric lattice generator, LPBF thermal history and melt-pool diagnostics, inherent-strain distortion and residual stress, FFF interlayer thermal cycling and bond strength, three design-for-additive-manufacturing checks, three AM output writers, and four marine capabilities. The domain content is documented in [`MANUFACTURING.md`](../MANUFACTURING.md) and [`MARINE.md`](../MARINE.md); the physics roadmap is [RFC-0012](../rfcs/0012-am-process-simulation.md).

The obvious first instinct — and the one this ADR rejects — is that "additive manufacturing" is a new *kind* of thing and deserves its own namespace (`am.*`, `manufacturing.*`) or its own plugin type with a vtable shaped for process simulation. Three verified properties of the current host make that instinct expensive and, in two cases, impossible without an ABI ratchet:

1. **Dispatch is a pure prefix match.** `RegistryDispatcher::dispatch` (`src/pipeline/registry_dispatcher.cpp:415-436`) tests `starts_with("mesher.")`, `"solver."`, `"writer."`, `"postproc."`, `"reader."` in order and falls through to `"unsupported capability namespace"`. Any new id **under those prefixes** routes with zero host changes; a brand-new prefix requires editing the dispatcher, the manifest namespace allow-list (`src/plugin-host/manifest.cpp:19-26`), the registry, the conformance suite, and — because a new prefix implies a new vtable shape — a header under `include/souxmar-c/`. That is an ABI minor ratchet plus a new plugin type: Tier 3, an RFC, a 7-day comment window, two maintainer approvals, and a permanent addition to the surface every plugin author and packager carries.

2. **`postproc.*` hard-requires an upstream field.** A postproc stage with no `field: {from: …}` is a *dispatch error*, not a NULL handle (`registry_dispatcher.cpp:252`). So a capability that analyses a mesh and produces a field — overhang, printability, build time, hydrostatic pressure, collapse margin, corrosion — cannot be a `postproc.*` at all. It has to be a `solver.*`, whose entry point takes `(mesh, inputs, options, out_field)`.

3. **Meshers get no value bag.** `souxmar_mesher_mesh_fn(geometry, options, out_mesh, user_data)` — the only tunables are the four fields of `souxmar_mesher_options_t` (`target_size`, `optimize`, `element_order`, `random_seed`), filled from the YAML keys of the same names. A mesher cannot read a custom key such as `unit_cell` or `relative_density`. Readers, by contrast, get `(path, inputs, options)`, so a fully-parametric generator has a channel there.

The C plugin ABI is at MINOR 9 and has taken six additive ratchets in eight sprints (ADRs 0037–0042). That chain was justified by surfaces the host itself needed — renderer streams, CAD sessions, time-series playback. A domain capability pack is a different category of change, and the ratchet budget is not free: every bump is a compatibility matrix row in conformance check C004 forever.

## Decision

**The manufacturing block ships as eight in-tree example plugins registering eighteen capability ids under the five existing namespaces. No ABI change, no new namespace, no new plugin type, no host change.**

The mapping falls out of the three constraints above:

| Constraint | Consequence in this block |
| --- | --- |
| Dispatch is prefix-only | Every id is `mesher.` / `reader.` / `solver.` / `postproc.` / `writer.` + a sub-namespace. Sub-namespaces (`solver.am.thermal.lpbf`) are already legal: the manifest allow-list validates the *first* segment only. |
| `postproc.*` needs an input field | **Mesh-only analyses register as `solver.*`. Only field→field derivations register as `postproc.*`.** So `solver.am.overhang`, `solver.am.printability`, `solver.am.buildtime`, `solver.marine.hydrostatic`, `solver.marine.hull_collapse`, `solver.marine.corrosion` are solvers; `postproc.am.melt_pool`, `postproc.am.residual_stress`, `postproc.am.bond_strength` are postprocs. |
| Meshers get no value bag | Fully-parametric generation goes through `reader.*`. `reader.lattice` takes a spec file path plus a value bag whose entries override the file. `mesher.am.layered` stays a mesher because it genuinely only needs `target_size` + `element_order`, and it documents its build-box fallback as the direct consequence of having no other channel. |

**`solver.am.overhang` being a "solver" is a consequence of the frozen vtable shapes, not a semantic claim.** It solves nothing. It walks the mesh's boundary facets and reports a tilt angle. The word "solver" in a souxmar capability id means "this capability has the shape `(mesh, inputs, options) -> field`", and that is the whole of the meaning. Every affected plugin says so in its source header, `MANUFACTURING.md` says so in its capability map, and this paragraph exists so that nobody later "fixes" the taxonomy by moving these ids and breaking every pipeline file in the wild.

The same honesty applies in the other direction: `reader.lattice` reads a file, so calling it a reader is defensible — but what it mostly does is *generate*, and it lives there because the reader vtable is the only one in v1 that receives both a path and a value bag.

### Sub-namespace convention

Ids are `<kind>.<domain>.<physics>.<variant>` — `solver.am.thermal.lpbf`, `solver.am.distortion.inherent_strain`, `solver.marine.hull_collapse`. Two rules:

- The domain segment (`am`, `marine`) is the second segment, never the first. `am.solver.thermal` would not dispatch.
- The *method* goes last (`inherent_strain`), so a higher-fidelity implementation of the same physics can either take the same id (see below) or sit beside it as `solver.am.distortion.thermomechanical` without renaming anything.

### The capability id is the contract; the model behind it is not

A calibrated thermomechanical implementation can replace any of these models without touching a pipeline file, provided it keeps the id, the output field (name, location, kind, component order, units) and the input keys. `MANUFACTURING.md` § "Plugging a real solver in behind the same capability id" is the normative statement of that contract; RFC-0012 is the plan that exercises it.

### What stays out of scope

- **No new ABI surface of any kind.** `SOUXMAR_ABI_VERSION_MINOR` stays at 9.
- **No material-library ABI.** No plugin in this block reads a material file; every property is an explicit input with a default. The curated library (`examples/materials/am-marine.toml`) is editorial data consumed by the desktop panels and the `propose_am_setup` tool, not by a plugin.
- **No mesher value bag.** The additive `souxmar_registry_add_mesher_ext` ratchet that would give meshers one is specified in RFC-0012 § Proposal and **deliberately deferred out of this block**.
- **No support-structure generator, no scan-path planner, no machine post-processor** beyond the naive G-code writer.

## Alternatives considered

### A new `am.` / `manufacturing.` prefix, with an ABI minor ratchet for a mesher value bag

Give the domain its own top-level prefix and, while the headers are open anyway, add `souxmar_registry_add_mesher_ext` so a parametric build-volume mesher can read `unit_cell` and friends directly instead of masquerading as a reader.

Rejected because the cost is structural and permanent while the benefit is cosmetic. It requires: a new branch in `RegistryDispatcher::dispatch`; a new entry in the manifest namespace allow-list; a new header under `include/souxmar-c/` defining the vtable the new prefix implies; a `SOUXMAR_ABI_VERSION_MINOR` bump to 10 with a history line; a new conformance category; a new row in the C004 compatibility matrix forever; a Tier-3 RFC with a 7-day comment window. In exchange, `solver.am.overhang` gets called `am.overhang`. It also *fragments the user's mental model* rather than clarifying it: the orchestrator, the agent's solver selection, and the desktop's stage pickers are all organised by pipeline role (what produces a mesh, what produces a field, what writes a file), and a domain prefix cuts across that grain. The mesher-value-bag half of the alternative is a genuinely good idea on its own merits, which is why it survives as RFC-0012's proposed ratchet rather than being discarded — but it is not needed to ship this block, and bundling it here would have converted a Tier-2 change into a Tier-3 one for the sake of one plugin's taxonomy.

### A generic `analysis.*` kind for mesh-in / field-out capabilities

Introduce a fourth compute kind sitting between solver and postproc: `analysis.*`, with a vtable `(mesh, inputs, options) -> field` and *no* required upstream field — which is precisely what overhang, printability, buildtime, hydrostatic, collapse and corrosion want.

Rejected because that vtable already exists and is called `souxmar_solver_vtable_t`. `analysis.*` would be a rename of `solver.*` with different documentation, and it would immediately raise a boundary question nobody can answer crisply — is a steady-state heat conduction solve an "analysis"? is a closed-form beam-deflection stub a "solver"? Every future contributor would have to re-litigate it. The cost is the same ABI + dispatcher + allow-list + conformance work as the previous alternative, and it would also strand the *existing* mesh-only capabilities (`postproc.mesh_quality` is field-requiring today only because it was forced into the postproc shape) unless we migrated them, which is a breaking change to shipped pipelines. Naming honesty is worth something, but not an ABI ratchet plus a migration; a paragraph in the docs and a source-header comment buys the same clarity for free.

### Fold everything into the existing generic ids (`solver.thermal.*`, `postproc.*`) with a `process:` discriminator

Register no new ids at all: extend `solver.thermal.transient` with an `am_mode: lpbf` input, extend `postproc.von_mises` to know about residual stress, and so on.

Rejected because it destroys discoverability and breaks the agent. `list_plugins` and the marketplace index are keyed by capability id; a capability that only exists as an input flag on another capability is invisible to both. It also couples unrelated code: an LPBF-specific bug fix would ship inside the general thermal solver, and the closed-form AM models would be linked into every build that wants heat conduction. The plugin model exists exactly so that this does not have to happen.

### Ship the whole block out-of-tree as a plugin pack

Publish the eight plugins in a separate repository, listed in the plugin index.

Rejected for this iteration, though it is the *eventual* shape if the domain grows. In-tree buys three things the block needs right now: the conformance suite and determinism gate run against these plugins on every PR; the four example projects can be part of the integration test set; and the eighteen ids are stable enough for the desktop panels and the agent planner to name literally. An out-of-tree pack would either duplicate that CI or go untested. Nothing about this decision blocks the move later — the plugins already link only `souxmar::public_headers`, exactly like a third-party pack.

## Consequences

### Positive

- **Zero ABI cost.** MINOR stays at 9; no new compatibility-matrix row; no plugin author or packager pays anything for this block.
- **Tier 2, not Tier 3.** Two reviewer approvals from the affected modules and green CI including the conformance suite, per [`GOVERNANCE.md`](../GOVERNANCE.md) § Merge tiers. The block is reviewable as what it is — new in-tree plugins, tests, examples and docs.
- **Every capability is dispatchable, cacheable, and swappable on day one.** They inherit content-addressed caching, the parallel runner, the crash-isolation frame, `souxmar plugin list`, the marketplace index shape, and the desktop stage pickers with no new plumbing.
- **The "capability id is the contract" property is now demonstrated, not asserted.** Eighteen closed-form models sitting behind ids that a high-fidelity solver can take over is the strongest available argument for the plugin architecture — and RFC-0012 is the concrete plan to do exactly that.
- **The block is self-contained.** Eight directories under `examples/plugins/`, each linking only public headers, C++20 stdlib only. Deleting the block is `rm -r` plus a CMake line.

### Negative

- **`solver.am.overhang` reads as a lie to a newcomer.** Three capabilities in this block ("overhang", "printability", "buildtime") are geometric checks wearing a solver's clothes, and two more ("hydrostatic", "corrosion") are closed-form evaluations painted onto a mesh. We pay for the ABI stability with a taxonomy that needs a paragraph of explanation in three places (this ADR, the docs, each source header). That explanation will be re-asked in every onboarding.
- **`reader.lattice` sets a precedent for "reader as generator."** The next contributor who wants a parametric primitive will reasonably copy it, and `reader.*` will drift toward being the namespace where anything with parameters lives. RFC-0012's mesher-value-bag ratchet is the intended correction; until it lands, the drift is real.
- **Repeated input blocks in pipeline YAML.** Because a postproc has no channel to the solver stage before it and no plugin reads a material file, the process parameter block is typed twice in every solver+postproc pair. It is verbose, and it is a genuine source of user error: the thermal stage and the melt-pool stage can silently disagree. The desktop panels write both blocks from one form to mitigate it; a hand-written pipeline has no such protection.
- **Eighteen ids is a large single-block addition to the registry namespace.** `souxmar plugin list` output on a dev build grows noticeably, and the agent's system prompt carries more capability names.
- **Closed-form models under production-sounding ids.** Someone will run `solver.am.distortion.inherent_strain` with the default `strain_calibration` and quote the number. Every mitigation available (fidelity table, source headers, docs, the report's own disclaimers) is documentation, and documentation is opt-in.

### Risks

- **Risk:** a future contributor "corrects" the taxonomy by moving `solver.am.overhang` to `postproc.am.overhang` or a new prefix, breaking every pipeline file that names it. **Mitigation:** this ADR is linked from the capability map in `MANUFACTURING.md` and from each affected plugin's source header; the ids are named literally in `propose_am_setup`'s `kKnownCapabilities` allow-list, so a rename fails a unit test rather than silently shipping.
- **Risk:** cell tags are overloaded. Layer resolution rule 1 treats a non-negative `souxmar_mesh_cell_tag` as the layer index, but the ABI documents `tag` as the inherited geometry-entity id. A conforming mesher's output will be mis-binned into "layers" that are really CAD faces, with no error. **Mitigation:** documented in every plugin header, in `MANUFACTURING.md` § Layer resolution, and in the two skills; `mesher.am.layered` exists precisely to produce a correctly-tagged mesh. A cleaner fix needs a tag-semantics channel the ABI does not have — noted as an open question in RFC-0012.
- **Risk:** two plugins register the same id in one search path (the closed-form example and a high-fidelity replacement). **Mitigation:** the registry rejects a duplicate id; documented in `MANUFACTURING.md` with the two supported patterns (drop one from `--plugin-path`, or use a sibling id and name it explicitly).
- **Risk:** the marine capabilities get read as class calculations. **Mitigation:** `MARINE.md` opens with an advisory-only block, the qualification report carries the disclaimer in its output, the source headers state it, and the report generator refuses to emit certificate numbers, stamps or surveyor names. This is the risk with the highest consequence in the block and the mitigation is deliberately repetitive.
- **Risk:** determinism regressions. Eight new plugins with boundary-face extraction, contour chaining and root-finding are exactly where `unordered_map` iteration and tolerance-based loop exits creep in. **Mitigation:** the determinism gate covers them like anything else; every plugin uses ordered containers and fixed iteration counts, and the `auditing-determinism` skill is the review checklist.

## References

- [`docs/MANUFACTURING.md`](../MANUFACTURING.md) — capability map, per-capability contract, fidelity table, calibration.
- [`docs/MARINE.md`](../MARINE.md) — marine conventions, collapse modes, corrosion, advisory-only scope.
- [ADR-0045](0045-agent-tool-contract-am-ratchet.md) — the agent-tool half of this block (tools 19–24).
- [RFC-0012](../rfcs/0012-am-process-simulation.md) — physics roadmap, validation plan, and the deferred `souxmar_registry_add_mesher_ext` ratchet.
- [ADR-0008](0008-abi-v1-final-freeze.md) — ABI v1 freeze + ratchet rules; the budget this decision declines to spend.
- [ADR-0005](0005-postproc-c-abi.md) — the postproc vtable whose required-field contract forces constraint 2.
- [ADR-0009](0009-openfoam-process-isolation.md) — the subprocess-adapter pattern a high-fidelity replacement would use.
- [`docs/GOVERNANCE.md`](../GOVERNANCE.md) — merge tiers; this change is Tier 2.
- [`docs/PLUGIN_SDK.md`](../PLUGIN_SDK.md) — plugin-type taxonomy and the manifest namespace allow-list.
- `src/pipeline/registry_dispatcher.cpp:252` — the postproc required-field error.
- `src/pipeline/registry_dispatcher.cpp:415-436` — prefix-only dispatch.
- `src/plugin-host/manifest.cpp:19-26` — the namespace allow-list.
- `include/souxmar-c/mesher.h`, `include/souxmar-c/reader.h` — the two vtables whose asymmetry forces constraint 3.
- `examples/plugins/am-layered-mesher`, `lattice-reader`, `am-thermal`, `am-distortion`, `am-polymer`, `am-manufacturability`, `am-slicer`, `marine` — the implementations.

## History

- 2026-08-17 (manufacturing block push 1): Proposed.
