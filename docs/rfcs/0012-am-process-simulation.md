# RFC 0012: AM process simulation — from closed form to calibrated thermomechanics

- **Author:** celikgokhun
- **Status:** Draft
- **Tracking issue:** TBD — file at manufacturing-block kickoff
- **Affects:** plugin SDK (new capability ids behind existing ones), agent tool contract (additive solver options), validation corpus (new benchmark data sets + goldens), **proposed** ABI minor ratchet (`souxmar_registry_add_mesher_ext`, deferred — see § Proposal 5)
- **Tier:** 3
- **Date opened:** 2026-08-17
- **Date `final-comment` started:** —
- **Date accepted / rejected:** —

## Summary

The manufacturing block ([ADR-0044](../adr/0044-manufacturing-capability-namespaces.md)) ships eighteen additive-manufacturing and marine capabilities as **closed-form and heuristic models**: a Rosenthal moving heat source for LPBF thermal history, an inherent-strain/Stoney construction for distortion, lumped-capacitance cooling plus a reptation healing law for polymer bonding, facet geometry for design-for-additive-manufacturing checks, and closed-form shell formulae for pressure-hull collapse. They are useful, honest and fast — and they are not simulation in the sense a process engineer means it.

This RFC is the path from there to **calibrated layer-wise thermomechanical AM simulation**: layer-wise transient thermal plus elasto-plastic mechanics, executed through the existing CalculiX and FEniCSx adapters, registered **behind the same capability ids** so that no pipeline file, no desktop panel and no agent tool changes when the fidelity ladder is climbed. It also specifies the one ABI change the block wanted and did not take — an additive `souxmar_registry_add_mesher_ext` that gives meshers a value bag — and says why it was deferred.

**No ABI change is proposed for acceptance by this RFC.** § Proposal 5 is a specification of a future ratchet, filed now so the deferral is on the record with its cost.

## Motivation

### What the closed-form models actually approximate

Each model in the block replaces a boundary-value problem with an analytical solution to an idealised version of it. The idealisations are stated in every source header; collected, they are the case for this RFC.

**`solver.am.thermal.lpbf` — Rosenthal (1946) moving point source.** A point source of absorbed power travelling at constant speed over the free surface of a semi-infinite solid with constant properties, in quasi-steady state. There is no mesh conduction, no latent heat of fusion, no temperature-dependent conductivity or specific heat, no powder-versus-solid conductivity contrast, no radiation or convection loss, and no interlayer superposition — the bulk of a tall part reads baseplate temperature instead of the tens of kelvin above it a real build accumulates. Melt-pool geometry is a per-layer quantity, so cells in one layer differ only through their local substrate temperature: contour-versus-infill differences, vector-end overheating and scan-rotation anisotropy are all invisible. The model is known to **under-predict LPBF conduction-mode melt-pool depth, commonly by 1.5–2×**, and therefore to **over-predict lack-of-fusion risk**.

**`postproc.am.melt_pool` — two dimensionless criteria.** Lack-of-fusion via the Tang–Pistorius–Beuth index and keyholing via normalised enthalpy. Both are literature criteria applied to Rosenthal's pool dimensions, so both inherit the depth bias above. The severity anchors (the point at which each sub-score saturates) are explicit interpolation choices, not measured thresholds.

**`solver.am.distortion.inherent_strain` — eigenstrain plus Stoney curvature.** The entire thermo-mechanical history of a layer is lumped into one equibiaxial stress-free strain, and curvature accumulates layer by layer on a growing composite. There is no stiffness matrix, no equilibrium iteration, no plasticity, no scan-vector effect, no support-structure stiffness, no baseplate-cutting step, and no geometry sensitivity beyond the bounding box, the layer bins, and each node's distance from the centroid axis. The displacement ansatz is kinematically admissible but **does not satisfy equilibrium**: it reproduces shape and trend, not the field. Anything not roughly plate-like — a thin-walled tube, a lattice, a topology-optimised bracket — is outside the plate idealisation Stoney rests on. And the whole thing is scaled by `strain_calibration`, which is meaningless until fitted to a measured part on the user's machine.

**`postproc.am.residual_stress` — elastic read-back with a yield cap.** A least-squares displacement gradient per cell, minus the applied eigenstrain, through isotropic Hooke, capped at yield after the fact. No hardening, no kinematic redistribution, no relief from HIP or heat treatment, one averaged strain per cell — so it cannot resolve the near-surface gradient that hole-drilling or XRD actually measures, which is the only residual stress anyone has data for.

**`solver.am.polymer.fff` + `postproc.am.bond_strength` — lumped capacitance plus reptation.** No conduction between roads, no in-plane scan path, no radiation, no latent heat, and therefore no crystallisation plateau for the semi-crystalline polymers the capability targets (PEKK, PA12-CF) — the model cools too fast through it. A buried road keeps the convective time constant of an exposed one, so thick-part interiors are over-cooled. Everything in-plane collapses into `layer_time`.

**The DfAM trio.** `solver.am.overhang` is exact facet geometry and stays useful. `solver.am.printability` is a hand-weighted heuristic calibrated against nothing, with a wall-thickness proxy that is mesh-resolution dependent for any cell that is not a single cell through the wall. `solver.am.buildtime` is a geometric scan-length model that ignores jumps, scan strategy, warm-up and every operator intervention; real LPBF jobs commonly run 10–40 % longer.

### Where they break, concretely

The failure modes are not academic. Five that a user will hit in the first week:

1. **"My part is a thin-walled tube and the predicted distortion is nonsense."** Correct: the Stoney idealisation does not apply. The model has no way to detect that and no channel to warn on.
2. **"Two orientations of the same bracket give the same melt-pool answer."** Correct: melt-pool geometry is per-layer, driven by the process parameters and the local substrate temperature, not by local geometry. Thin ribs and bulk sections in the same layer read identically, when in reality the rib overheats.
3. **"The residual stress map has nothing at the fillet."** Correct: one averaged strain per cell, no equilibrium, no stress concentration.
4. **"Distortion doubled when I halved the layer height and changed nothing else."** Correct, and documented: the model accumulates one curvature increment per *simulation* layer, so `strain_calibration` must be re-fitted whenever the layer height changes. This is the single most confusing property of the block.
5. **"The build-time estimate is 30 % under what the machine reported."** Expected. The model has no skywriting, no jumps, no dose factors, no warm-up.

Every one of these is a consequence of the model class, not a bug. Fixing any of them means solving the actual problem.

### Why now

Three things converge:

- **The adapters exist.** [RFC-0011](0011-calculix-solver-adapter.md) brings CalculiX in as a subprocess plugin with nonlinear (geometric + material), thermal (steady + transient) and coupled thermal-mechanical procedures — which is, essentially, the engine an AM thermomechanical analysis needs. The FEniCSx adapter is already in-tree. Neither has to be built for this.
- **The capability ids are already the public surface.** Eighteen ids, four example projects, two desktop panels and six agent tools all name them. Replacing the models behind them is a plugin swap; had the block invented an `am.*` namespace or a bespoke vtable, it would now be a migration.
- **The audience asks for it by name.** A process engineer's first question about any AM tool is "is it calibrated?" The honest current answer — "no, and here is exactly what that costs you" — is a good answer for a v1 block and a bad answer forever.

## Proposal

Six parts. Parts 1–4 need no ABI change. Part 5 is a specification filed for later. Part 6 is the validation programme that decides whether any of this worked.

### 1. The fidelity ladder, and the rule that governs it

Three rungs, all reachable through the same capability id:

| Rung | Implementation | Cost per run | Status |
| --- | --- | --- | --- |
| **L0 — closed form** | The plugins in this block | milliseconds | Shipping |
| **L1 — calibrated closed form** | Same plugins, `strain_calibration` / `absorptivity` / healing constants fitted to the user's own coupons | milliseconds | Shipping; procedures in [`MANUFACTURING.md`](../MANUFACTURING.md) § Calibration |
| **L2 — layer-wise thermomechanical FE** | New plugins behind the same ids, driving CalculiX or FEniCSx | minutes to hours | This RFC |

The governing rule, normative for every rung:

> A replacement implementation keeps the **capability id**, the **output field** (name, location, kind, component order, units) and the **input keys**. It may add input keys; unknown keys are ignored by the lower rungs, so one pipeline file stays runnable against all three.

That rule is what makes the ladder a ladder rather than three products. It is stated in `MANUFACTURING.md` § "Plugging a real solver in behind the same capability id" and repeated here because L2 is the first real test of it.

Two ways to select a rung, both existing mechanisms:

- **Same id, different search path.** Only one plugin may own an id in a given `--plugin-path`. Dropping the closed-form example plugin swaps the whole chain.
- **Sibling id, named explicitly.** `solver.am.thermal.lpbf.fe` beside `solver.am.thermal.lpbf`, selected in the pipeline or via the orchestrator's `prefer:` mechanism (the same knob RFC-0011 uses to choose between FEniCSx and CalculiX for linear elasticity).

L2 ships as **sibling ids first**, so a user can run both and diff, and the `prefer:` default stays on L0 until the validation matrix in § 6 is green.

### 2. Layer-wise transient thermal (L2a)

New capability: `solver.am.thermal.lpbf.fe`, same output field (`temperature`, nodal scalar, °C, one step per simulation layer), same input keys plus the ones an FE run needs.

The analysis is the standard layer-activation ("quiet element") scheme:

```
for each simulation layer j = 0 .. n-1:
    activate the cells of layer j        (quiet -> active)
    apply the layer's absorbed energy    (equivalent moving source or
                                          per-layer flux, see below)
    solve a transient conduction step over interlayer_time(j)
    record the nodal temperature state   -> field step j
```

Two source models, selected by a new input key `source_model`:

| `source_model` | What is applied | Cost | Use |
| --- | --- | --- | --- |
| `layer_flux` (default) | The layer's total absorbed energy as a surface flux over the layer's top facets, spread over the layer's scan time | one transient step per layer | Part-scale thermal history; the honest replacement for what L0 does today |
| `moving_source` | A Goldak-style ellipsoidal moving source stepped along a synthesised hatch path | one transient step per *scan* increment | Melt-pool-scale answers on a sub-region; not a whole part |

`layer_flux` is the one that matters for part-scale work: it keeps the per-layer field-step contract (one step per simulation layer) while giving real conduction into the substrate, real interlayer accumulation, and temperature-dependent properties. `moving_source` exists because `postproc.am.melt_pool` needs pool dimensions to consume, and a layer-averaged flux cannot produce them.

Additive input keys (all optional, all ignored by L0):

```yaml
source_model: layer_flux          # layer_flux | moving_source
conductivity_table:               # temperature-dependent properties
  - [20.0,   14.0]                # [°C, W/(m·K)]
  - [800.0,  22.0]
specific_heat_table: [...]        # [°C, J/(kg·K)]
latent_heat_fusion: 2.7e5         # J/kg
powder_conductivity_ratio: 0.02   # unconsolidated powder vs solid
convection_coefficient: 15.0      # W/(m²·K) on exposed surfaces
emissivity: 0.4
time_step: 0.0                    # 0 => adapter picks from interlayer_time
```

Execution: through the CalculiX adapter's transient heat-transfer procedure (`solver.thermal.calculix.transient` in RFC-0011 terms) with one `*STEP` per layer and element activation via element-set membership, or through the FEniCSx adapter for the same problem. The AM plugin's job is deck generation, layer bookkeeping and result reassembly into the block's field contract — it does not implement conduction.

### 3. Elasto-plastic layer-wise mechanics (L2b)

New capability: `solver.am.distortion.thermomechanical`, same output field (`distortion_displacement`, nodal vector, metres, one step per layer), and a companion `postproc.am.residual_stress` that reads the real stress tensor rather than reconstructing one.

The analysis is the sequentially-coupled thermal→mechanical scheme:

```
for each simulation layer j:
    activate the cells of layer j
    impose the thermal history of layer j from the L2a field
    solve an elasto-plastic increment (small-strain or NLGEOM)
    record nodal displacement          -> field step j
optionally: release the baseplate constraint and re-solve
                                       -> the cut-off / springback state
```

This is where the two properties the inherent-strain model cannot have appear: **equilibrium** and **plasticity**. It is also where the cost appears — a 300-layer part is 300 nonlinear increments on an activating mesh.

Additive input keys:

```yaml
plasticity: isotropic_hardening    # none | isotropic_hardening | kinematic
hardening_curve:                   # [plastic strain, stress Pa] at temperature
  - [0.0,   4.5e8]
  - [0.02,  5.6e8]
nlgeom: false                      # geometric nonlinearity
baseplate_release: true            # emit the post-cut state as a final step
support_stiffness_fraction: 0.0    # smeared support stiffness, 0 = no supports
thermal_field: { from: <stage> }   # the L2a temperature history
```

Note `thermal_field`. A solver's vtable takes `(mesh, inputs, options, out_field)`; the *upstream handle keys* the dispatcher understands are `geometry`, `mesh`, `field`. A solver that wants a second field handle is a real gap — for L2b the workable options are (a) reference the thermal stage under the existing `field` key and pass the mesh separately, which works because L2b needs exactly one input field, or (b) have one plugin own both stages internally and expose the thermal history as an additional output. **(a) is the plan**; (b) would collapse two capability ids into one and break the ladder rule. This is recorded as Open Question 3.

**The inherent-strain model does not go away.** It becomes the fast rung *and* the calibration target: L2b's converged answer is what `strain_calibration` should be fitted against when no measured coupon exists, which turns the block's most-criticised constant into a derived quantity. That is a better outcome than deleting it.

### 4. Polymer and DfAM

Lower priority, same pattern:

- **`solver.am.polymer.fff.fe`** — transient conduction on the road stack with a crystallisation-latent-heat term and conduction (not convection) into buried neighbours. The healing postproc consumes it unchanged; only the temperature history improves.
- **`solver.am.printability`** — the weights are the problem, not the model class. The fix is not FE, it is *calibration*: score a corpus of parts whose build outcomes are known and fit the weights. That is a data-collection project, and it is honest to say the current weights are unvalidated until it happens.
- **`solver.am.overhang`** stays as it is. Exact facet geometry is already the right answer; the improvement people want is *support generation*, which is a different feature and needs its own RFC.
- **Wall thickness** — the mesh-resolution-dependent proxy in `solver.am.printability` deserves a real medial-axis or ray-cast computation. That needs geometry the mesh ABI does not carry, so it belongs with the BREP surface (ADR-0039/0041), not here.

### 5. The deferred ABI ratchet: `souxmar_registry_add_mesher_ext`

**Filed as a specification, not proposed for acceptance in this RFC.**

#### The problem

`souxmar_mesher_mesh_fn(geometry, options, out_mesh, user_data)` gives a mesher exactly four tunables — `target_size`, `optimize`, `element_order`, `random_seed` — and no channel for a custom key. Three consequences already visible in this block:

1. **`reader.lattice` is a reader because it has to be.** A parametric strut-lattice generator needs `unit_cell`, `cell_size`, `relative_density`, `bbox`. Readers get `(path, inputs, options)`; meshers get nothing. So a generator masquerades as a reader, and it sets a precedent for "reader as generator" that will be copied.
2. **`mesher.am.layered` cannot be told its build box or its build direction.** It falls back to a documented demo box (`[0,0,0] … [0.06, 0.02, 0.02]` m) and a hard-coded `+Z`, because there is literally no other channel. Every example in the block has to explain that.
3. **Layer-aware meshing cannot be parameterised at all** — no `layer_height` independent of `target_size`, no per-region refinement, no support-region tagging.

#### The proposed surface

Strictly additive. Nothing moves; nothing is renamed; no existing declaration changes.

```c
/* include/souxmar-c/mesher.h — ADDITIVE, ABI MINOR 9 -> 10 */

/* Extended mesher entry point. Identical to souxmar_mesher_mesh_fn plus the
 * stage input map, so a plugin can read arbitrary keys the way readers,
 * solvers, postprocs and writers already can.
 *
 * `inputs` is the stage's `input:` map with the `geometry` upstream-handle
 * key removed, mirroring the writer contract (which removes `mesh`/`field`).
 * It may be NULL when the stage supplied no inputs beyond the handle. */
typedef souxmar_status_t (*souxmar_mesher_mesh_ext_fn)(
    const souxmar_geometry_t* geometry,
    const souxmar_mesher_options_t* options,
    const souxmar_value_t* inputs,          /* NEW — may be NULL */
    souxmar_mesh_t** out_mesh,
    void* user_data);

typedef struct souxmar_mesher_vtable_ext {
  int32_t abi_version;                      /* MUST equal SOUXMAR_ABI_VERSION_MAJOR */
  souxmar_mesher_mesh_ext_fn mesh_ext_fn;
  souxmar_mesher_destroy_fn destroy_fn;     /* may be NULL; existing typedef */
} souxmar_mesher_vtable_ext_t;
```

```c
/* include/souxmar-c/registry.h — ADDITIVE */

souxmar_status_t souxmar_registry_add_mesher_ext(
    souxmar_registry_t* registry,
    const char* capability_id,
    const struct souxmar_mesher_vtable_ext* vtable,
    void* user_data);
```

```
/* include/souxmar-c/abi.h — history line */
 *   v1.10 <sprint> — mesher value bag (souxmar_registry_add_mesher_ext);
 *                    RFC-0012, ADR-00NN.
```

Host-side contract:

- `souxmar_registry_add_mesher` and `souxmar_mesher_vtable_t` are **untouched**. A v1.0 mesher keeps loading and keeps working.
- The registry stores which of the two vtables a capability registered. `dispatch_mesher` calls the ext entry point when present and the plain one otherwise; the prefix-match branch in `RegistryDispatcher::dispatch` does not change.
- Registering the same id through both functions is an error, not a preference rule.
- Conformance gains one check: a mesher registered through `_ext` must produce identical output when handed an empty and a NULL `inputs` map, so "no inputs" is unambiguous.
- Plugins targeting v1.10 that need to run on an older host inspect `host->abi_version` / `host->capabilities` and fall back to the plain registration — the existing graceful-downgrade pattern.

#### Exactly what it would buy

- `mesher.am.layered` reads `bbox`, `build_direction`, `layer_height` and `num_layers` directly; the demo-box fallback and the `+Z`-only limitation both disappear.
- A future `mesher.am.conforming` can take a layer height independent of the in-plane target size — the single most-requested AM meshing parameter.
- `reader.lattice` can be re-registered as `mesher.lattice` (keeping the reader id as a deprecated alias for one major, per the deprecation policy), which puts it back in the namespace where a user would look for it.
- Every future parametric generator stops needing a file path it does not read.

#### Why it is deliberately deferred out of this block

- **It converts a Tier-2 change into a Tier-3 one.** ADR-0044's whole argument is that the block needs no ABI ratchet. Bundling this would have required a 7-day comment window and two maintainer approvals for a package whose other 99 % is new in-tree plugins.
- **Nothing in the block is blocked by it.** `reader.lattice` works. `mesher.am.layered`'s fallback is documented and every example runs. The cost of not having it is verbosity, not capability.
- **One consumer is not a demand signal.** The ratchet should land when at least two meshers want it — the honest candidates are a conforming AM mesher and a parametric-primitive generator, neither of which exists yet. Designing the surface against one hypothetical consumer is how ABIs acquire the wrong shape permanently.
- **The ratchet budget is real.** The ABI has taken six additive bumps in eight sprints (ADRs 0037–0042). Each is a permanent row in conformance check C004's compatibility matrix and a permanent line in every plugin author's compatibility story. MINOR 10 should be spent on something the project has evidence for.
- **The `inputs`-key-removal detail needs a second opinion.** Writers get their map with `mesh`/`field` stripped; readers get theirs whole. Which convention the mesher ext should follow is a genuine design question, and it is cheaper to answer it in an RFC discussion than to freeze the wrong answer.

When it lands, it lands as its own ADR under ADR-0008's ratchet rules with the `Ratchet: additive minor surface (ADR-0008)` marker, on the model of ADRs 0037–0042.

### 6. Validation programme

An uncalibrated model with a validation report is an engineering tool. Without one it is a plausible-looking number generator. The programme has three levels, mirroring the `validating-solver` skill's hierarchy.

#### Level 1 — analytical and self-consistency

- **Rosenthal against itself.** The L2a `moving_source` path on a semi-infinite plate with constant properties, no latent heat and no losses must reproduce the L0 closed form to within discretisation error. This is the patch test of the AM block: if the FE path cannot reproduce the analytical solution of the problem the analytical solution solves, the deck generation is wrong.
- **Stoney against a two-layer strip.** The L2b path on a bimaterial strip with an imposed eigenstrain must reproduce the Stoney curvature in the thin-film limit.
- **Energy conservation.** Absorbed energy in must equal enthalpy rise plus losses out, per layer, to a stated tolerance.
- **Determinism.** Byte-identical output across Linux/macOS/Windows for the L0 chain; for L2, bit-identical within a pinned adapter configuration (CalculiX with a single-threaded equation solver, per RFC-0011 Open Question 6).

#### Level 2 — public benchmark measurement data

Two families of publicly published measurement data, cited by name; the published data sets and their stated measurement uncertainties are the authority, and this RFC deliberately restates none of their numbers.

- **NIST AM-Bench.** The AM-Bench series publishes measurement data and blind challenge problems for metal AM, including single-track and multi-track **melt-pool geometry and cooling-rate** measurements, **residual elastic strain** measurements, and **part deflection** on bridge-like specimens, with the process parameters and material characterisation needed to model them. It is the closest thing the field has to a NAFEMS benchmark set, and it is the right target for exactly that reason. Deliverables: one souxmar project per adopted challenge problem, a comparison table (predicted versus published measurement, with the published uncertainty band shown), and an explicit statement of every parameter we had to assume because the publication did not state it.
- **Published bridge / twin-cantilever distortion cases.** The bridge (twin-cantilever) specimen is the canonical distortion-calibration geometry in the inherent-strain literature: build it, cut one leg free, measure the tip deflection. Multiple published studies report the geometry, the process parameters and the measured deflection. Deliverables: the same comparison table for L0-calibrated, L1 and L2, plus the fitted `strain_calibration` per case — which is the number the community actually wants and nobody publishes consistently.

Acceptance is stated per case, before the run, in the case's own README: which quantity, which tolerance, and what an out-of-tolerance result means. **We do not publish an accuracy figure for the block as a whole.** Any single number ("within 15 %") would be a claim about geometries, machines and alloys we have not tested. The report is per case or it is marketing.

#### Level 3 — cross-implementation and cross-solver

- L0 versus L1 versus L2 on the same four example projects, reported as a field-norm and max-error table through the existing `compare_solver_results` agent tool (RFC-0011, tool 6).
- L2a through CalculiX versus L2a through FEniCSx on the same deck, which is the only available check on the deck generators themselves.
- A **regression corpus**: every benchmark case runs in CI at a coarse mesh with a stored golden, so a change in the deck generator shows up as a diff and not as a slowly drifting number.

Published as `docs/validation/am-process-<version>.md` plus a generated HTML report per case, per the `validating-solver` skill's reporting contract.

## Alternatives considered

### Alternative A: Stay at L0/L1 forever, and be loud about it

Keep the closed-form block, invest in the calibration story instead — better coupon procedures, a calibration-fit helper tool, a shared library of fitted constants per machine and alloy.

Rejected as the *only* path, though it is a real part of the plan and cheap. It fails on the five concrete failure modes in § Motivation: no amount of calibration makes a plate-idealisation model correct for a thin-walled tube, gives a per-layer melt-pool model geometry sensitivity, or puts stress at a fillet. Calibration moves L0 from "wrong" to "usefully wrong for parts like the one you calibrated on", which is genuinely valuable and genuinely insufficient. It also concedes the audience: the process engineer who asks "is it calibrated?" asks "does it solve equilibrium?" second, and answering "no, by design" ends the conversation.

### Alternative B: Adopt a dedicated open-source AM process code as a subprocess adapter

Wrap an existing AM-specific simulation code the way ADR-0009 wraps OpenFOAM.

Rejected for now, and re-openable. The open-source AM-process landscape is thinner and much younger than the general FE landscape: the credible candidates are research codes with small user bases, narrow platform support, and licences that need individual review, and none has the "an engineer in a regulated industry will recognise the name" property that made CalculiX strategic in RFC-0011. Adopting one would also mean *two* thermomechanical engines in-tree (the CalculiX adapter is landing regardless) with two deck generators and two determinism stories. If a candidate matures — or if a user brings one they already trust — the subprocess pattern is ready and this RFC's ladder rule means it can slot in behind the same ids. That is precisely the point of the ladder.

### Alternative C: Implement layer-wise thermomechanics natively in souxmar core

Write the transient thermal and elasto-plastic mechanics as in-tree reference solvers with layer activation.

Rejected. It contradicts `VISION.md`'s "adapter over reinvention" principle in the clearest possible case: nonlinear transient FE with plasticity is a multi-year engineering programme, and CalculiX has had it in production since the early 2010s. It would also make souxmar the owner of a numerics stack whose bugs are indistinguishable from AM-model bugs, exactly when the block needs to be able to say "the conduction solve is CalculiX's, the deck is ours." The reference in-tree solvers exist to demonstrate the ABI, not to compete.

### Alternative D: Ship L2 as an out-of-tree commercial plugin

Put the calibrated thermomechanical chain in the paid marketplace tier.

Rejected on principle and on practice. `BUSINESS_MODEL.md`'s commitment is that the free tier is a complete product and the paid tier is a convenience upgrade, not a feature unlock; putting "actually correct AM simulation" behind a paywall inverts that. Practically, the validation corpus is the asset here, and a validation corpus nobody can run is worthless.

### (Considered and rejected: do nothing)

Doing nothing means the closed-form block is the permanent answer, the fidelity table in `MANUFACTURING.md` is a permanent apology, and the four example projects stay demonstrations rather than analyses. It also wastes the block's main architectural achievement: eighteen ids designed so the models behind them can be replaced, never replaced.

## Drawbacks

- **Runtime goes from milliseconds to hours.** A 300-layer part is 300 transient thermal steps plus 300 nonlinear mechanical increments on an activating mesh. Every UX assumption in the desktop panels (click a button, see a field) breaks. The pipeline runner's caching helps on re-runs; the first run does not.
- **Two rungs to maintain, forever.** The closed-form plugins do not get deleted — they are the fast triage path and (per § 3) L2b's calibration target. So the block carries two implementations of the same ids, with a cross-comparison matrix in CI. That is deliberate and it is still cost.
- **Deck generation is where the bugs will be.** RFC-0011's pre-mortem is instructive: the INP writer's line endings, the contact-pair surface tagging, the `.frd` version drift. A layer-activation AM deck is strictly harder than a linear static one — element sets per layer, per-step activation, a thermal history mapped onto a mechanical mesh — and every one of those is a silent-wrong-answer opportunity.
- **External binary dependency, third time.** OpenFOAM, ffmpeg, CalculiX; now CalculiX again for AM. The install UX debt compounds even though the dependency does not.
- **Validation data is scarce and partial.** Public AM benchmark data does not cover the alloys, machines, or geometries most users care about, and published studies routinely omit a parameter needed to model them. Every comparison table will carry a list of assumptions, and reviewers will (correctly) discount it accordingly.
- **Calibration constants proliferate.** L2 does not remove `strain_calibration`; it adds hardening curves, temperature-dependent property tables, powder conductivity ratios, absorptivity and emissivity. Higher fidelity means *more* things to get wrong, and a badly parameterised L2 run is more convincing and more dangerous than a badly parameterised L0 run.
- **The deferred ABI ratchet keeps costing.** Until `souxmar_registry_add_mesher_ext` lands, `reader.lattice` stays a reader, `mesher.am.layered` keeps its demo-box fallback, and every doc page explains why.

## Migration plan

- **Existing in-tree and out-of-tree plugins:** unaffected. No ABI change is proposed for acceptance here.
- **Existing pipeline files:** unaffected. L2 arrives as sibling ids (`solver.am.thermal.lpbf.fe`, `solver.am.distortion.thermomechanical`); an existing file keeps running L0. Opting in is one `plugin:` line, plus optional additive keys.
- **Existing example projects:** each of the four gains an alternate `pipeline.l2.yaml` running the same part through the FE path, so the two are diffable. `examples/am-lpbf-bracket` is the reference pair.
- **Saved chat sessions:** replay identically. The agent tools name capability ids through `propose_am_setup`'s allow-list; new ids are additions to that list, and a session that used the old ids still resolves.
- **Desktop panels:** the Manufacturing panel gains a fidelity selector writing either the L0 or the L2 id plus the extra keys. Because the key sets are supersets, switching the selector is a stage rewrite, not a form reset.
- **Docs:** `MANUFACTURING.md`'s fidelity table gains an L2 column stating what each L2 capability is and still is not; the calibration section gains "fit `strain_calibration` against an L2 run when you have no coupon". `MARINE.md` gains nothing — none of this makes the collapse arithmetic class-credible, and `MARINE.md` § "What would make this class-credible" already says what would.
- **If the mesher ratchet later lands:** `reader.lattice` is re-registered as `mesher.lattice` with the reader id retained as a deprecated alias for one full major, per `GOVERNANCE.md`'s deprecation policy, with a changelog migration note. `mesher.am.layered` gains `bbox` / `build_direction` / `layer_height` keys; its demo-box fallback stays for pipelines that pass nothing.

## Pre-mortem

It is 2027-08-17 and RFC-0012 went badly. What happened:

We shipped the L2 thermal path and it was slower than anyone budgeted for — a customer's 400-layer bracket took eleven hours where the desktop's progress UI assumed minutes, and two users reported it as a hang. Worse, the deck generator's layer-activation element sets were subtly wrong for meshes whose cell tags were geometry ids rather than layer indices: instead of erroring, the analysis activated an arbitrary partition of the part, converged beautifully, and produced a distortion field that looked plausible and was meaningless. We found it because a user's L0 and L2 answers disagreed by a factor of four and they had the good sense to ask, not because any test caught it. Then the validation programme stalled: we adopted two AM-Bench challenge problems, discovered that reproducing them needed material property data the publications did not include, published a comparison table full of "assumed" rows, and a reviewer on the forum quite reasonably concluded that souxmar's AM validation was decorative. Meanwhile the closed-form plugins — which we had kept as the "fast triage path" — stopped being maintained in practice, because everyone's attention was on L2, so the rung most users actually ran got the least care. Twelve months in, the block had two implementations, one of which was slow and occasionally silently wrong and the other of which was stale.

Leading indicators to watch in the first six months:

- Any L2-versus-L0 disagreement above a documented factor on the four in-tree examples that is *not* explained by a known model difference. This is the silent-wrong-answer canary and it should be a CI check, not a user report.
- L2 runs whose layer count does not equal the mesh's resolved layer count — the direct symptom of the cell-tag mis-binning failure. Assert it in the plugin, not in a test.
- Median L2 wall time on the in-tree examples growing run over run, or exceeding the figure the desktop's progress estimate quotes by more than 2×.
- Benchmark comparison tables where "assumed" rows outnumber "published" rows. If that ratio inverts, stop publishing the case and say why.
- Commit rate on the L0 plugins dropping to zero while L2 churns — the staleness signal.
- Support requests that begin "which one should I run?" This means the fidelity ladder is not legible in the UI, and legibility is the entire premise of keeping both rungs.

## Open questions

1. **Simulation-layer versus machine-layer resolution in L2.** L0 lumps machine layers into simulation layers and asks the user to scale `interlayer_time` accordingly, which is the block's most confusing property. L2 could resolve real machine layers for small parts (hundreds of steps, tractable) and lump for large ones — but then the field's step count means different things in different runs. Proposal: keep the simulation-layer contract, add a `machine_layers_per_step` output-metadata note in the report. Decide before L2a's first PR.
2. **Where the thermal history lives between L2a and L2b.** A full nodal temperature history for 300 layers is large. Options: pass it as a field (simple, memory-hungry), stream it through the RFC-0006 time-series surface (bounded memory, more plumbing), or have the mechanical stage re-run the thermal solve internally (no data transfer, doubles the cost). The time-series surface is the architecturally correct answer and the most work.
3. **A solver with two upstream field handles.** § 3 works around it by using the single `field` key. If a future capability genuinely needs two (a thermal history *and* a prior mechanical state, for a re-start), the dispatcher needs a second handle key — a pipeline-format change, i.e. Tier 3 on its own. Do not design for it speculatively; record it if it recurs.
4. **How calibration constants are stored and travel.** Today `strain_calibration` is a number in a YAML file with no provenance. A fitted constant is only meaningful with its machine, alloy, parameter set, layer height and coupon geometry attached. Does that become a materials-library concern, a project-file concern, or a new artefact? This is the highest-value open question in the RFC and it is not an ABI question at all.
5. **`prefer:` semantics for a fidelity ladder.** RFC-0011 uses `prefer: calculix` to choose an *implementer*. Choosing a *fidelity rung* is a different axis (`prefer: fast` / `prefer: accurate`?), and conflating them in one key will age badly.
6. **Determinism of the L2 path.** Inherited from RFC-0011 Open Question 6: CalculiX is deterministic single-threaded, not with a multithreaded solver. A multi-hour AM run single-threaded may be untenable, in which case the determinism gate needs a documented exemption class for L2 capabilities — which is a governance question, not a technical one, and it must be answered explicitly rather than by drift.
7. **Support-structure stiffness.** `support_stiffness_fraction` in § 3 is a smeared placeholder. Real support modelling needs support geometry, which needs a support generator, which is its own RFC.
8. **Whether `postproc.am.melt_pool` should consume L2a output at all.** A layer-flux thermal field has no melt pool in it. Either the postproc keeps computing pool dimensions from the process parameters (i.e. stays L0 internally, which is honest but confusing), or it errors when handed a `layer_flux` field, or the `moving_source` path becomes mandatory for melt-pool work. Leaning towards the third with a clear error message on the first.

## Implementation plan

Not a contract — a forcing function, so reviewers know what approving this commits them to. Sequenced so that every stage is independently useful and can be stopped after any PR.

### Stage 1 — calibration and validation harness (no new physics)

- [ ] **PR 1 — Benchmark corpus scaffolding.** `tests/validation/am/` with one directory per adopted case: geometry, process parameters, published-measurement table with its uncertainty, acceptance criterion, and a README stating every assumed parameter. Two cases to start: one bridge/twin-cantilever distortion case, one single-track melt-pool case.
- [ ] **PR 2 — Calibration helper.** A CLI path (`souxmar run` plus a documented sweep, or a small script under `scripts/`) that fits `strain_calibration` or `absorptivity` from a measured value by bisection over the existing L0 plugins. Records the fit with its provenance fields.
- [ ] **PR 3 — L0 validation report.** Run the corpus against L0 and L1, publish `docs/validation/am-process-l0.md` with the honest per-case table. **This PR is the one that decides whether L2 is worth building**: if calibrated L0 clears the acceptance criteria on the adopted cases, the priority order in Stage 2 changes.

### Stage 2 — L2a, layer-wise transient thermal

- [ ] **PR 4 — Deck generator for layer activation.** Element sets per layer, per-step activation, `layer_flux` source. Golden-file tests on the deck itself, byte-identical across platforms.
- [ ] **PR 5 — `solver.am.thermal.lpbf.fe` plugin.** Subprocess driver through the CalculiX adapter; result reassembly into the block's `temperature` field contract; hard assertion that the resolved layer count matches the mesh's (pre-mortem indicator 2).
- [ ] **PR 6 — Temperature-dependent properties + latent heat + powder conductivity.** The additive input keys of § 2.
- [ ] **PR 7 — `moving_source` path** for melt-pool-scale work on a sub-region, plus the Open Question 8 decision on `postproc.am.melt_pool`.
- [ ] **PR 8 — L1-versus-L2a comparison in CI** on the four in-tree examples at coarse mesh, with stored goldens.

### Stage 3 — L2b, elasto-plastic layer-wise mechanics

- [ ] **PR 9 — Sequentially-coupled mechanical deck.** Thermal history mapping, elasto-plastic increments, baseplate release step.
- [ ] **PR 10 — `solver.am.distortion.thermomechanical` plugin** plus the real-tensor `postproc.am.residual_stress` path.
- [ ] **PR 11 — `strain_calibration` fitting against L2b**, closing the loop in § 3.
- [ ] **PR 12 — Validation report** `docs/validation/am-process-l2.md`, per the `validating-solver` skill's reporting contract.

### Stage 4 — polymer, and the deferred ratchet if the signal appears

- [ ] **PR 13 — `solver.am.polymer.fff.fe`** with crystallisation latent heat and conduction into buried roads.
- [ ] **PR 14 — Printability weight calibration** against a corpus of parts with known build outcomes, or an explicit decision not to and a docs statement that the weights remain unvalidated.
- [ ] **PR 15 — (conditional) `souxmar_registry_add_mesher_ext`** per § 5, with its own ADR under ADR-0008's ratchet rules, only once a second mesher wants it.
- [ ] Documentation: `docs/MANUFACTURING.md` fidelity table gains its L2 column; `docs/AI_INTEGRATION.md` gains any additive solver options; `docs/ROADMAP.md` and `docs/attic/SPRINT_PLAN.md` gain the block.

## References

- [ADR-0044](../adr/0044-manufacturing-capability-namespaces.md) — the capability-namespace decision this RFC builds the ladder on top of.
- [ADR-0045](../adr/0045-agent-tool-contract-am-ratchet.md) — the agent tools that will select a fidelity rung.
- [`docs/MANUFACTURING.md`](../MANUFACTURING.md) — the models, the fidelity table, and the calibration procedures.
- [`docs/MARINE.md`](../MARINE.md) — § "What would make this class-credible", the marine counterpart of this roadmap.
- [RFC-0011](0011-calculix-solver-adapter.md) — the CalculiX subprocess adapter this RFC's L2 path drives; also the precedent for the deck-generation risk list and the determinism exemption question.
- [RFC-0006](0006-time-series.md) — the bounded-memory time-series surface Open Question 2 points at.
- [RFC-0002](0002-field-stream-protocol.md) — the field surface every rung's output lands on.
- [ADR-0008](../adr/0008-abi-v1-final-freeze.md) — ABI ratchet rules the deferred § 5 surface would land under.
- [ADR-0009](../adr/0009-openfoam-process-isolation.md) — the subprocess-adapter pattern.
- [ADR-0039](../adr/0039-abi-v1-6-brep-ratchet.md) / [ADR-0041](../adr/0041-abi-v1-8-feature-ops-ratchet.md) — the BREP surface a real wall-thickness computation belongs to.
- [`docs/GOVERNANCE.md`](../GOVERNANCE.md) — Tier-3 process, deprecation policy.
- `.claude/skills/validating-solver/SKILL.md` — the validation hierarchy and reporting contract § 6 follows.
- `.claude/skills/auditing-determinism/SKILL.md` — the gate Open Question 6 negotiates with.
- NIST AM-Bench — public metal-AM benchmark measurement series and blind challenge problems (melt-pool geometry and cooling rate, residual elastic strain, part deflection). Cited as publicly published measurement data; the published data sets and their stated uncertainties are the authority.
- Published bridge / twin-cantilever distortion studies in the inherent-strain literature — the canonical distortion-calibration geometry.
- Model sources implemented at L0, restated in `MANUFACTURING.md`: Rosenthal (1946); Eagar–Tsai; Hann et al. (2011); King et al. (2014); Tang, Pistorius & Beuth (2017); Keller & Ploshikhin (2014); Stoney (1909); Timoshenko (1925); Yang & Pitchumani (2002); Sun et al. (2008); Goldak et al. (1984) for the ellipsoidal moving source proposed at L2a.
