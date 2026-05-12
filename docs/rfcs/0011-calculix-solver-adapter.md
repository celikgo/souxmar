# RFC 0011: CalculiX solver adapter (subprocess plugin)

- **Author:** celikgokhun
- **Status:** Draft
- **Tracking issue:** TBD — file at Sprint 37 kickoff
- **Affects:** plugin SDK (new capability ids), agent tool contract (additive solver options), on-disk format conventions (INP/FRD reader+writer)
- **Tier:** 3
- **Date opened:** 2026-05-12
- **Date `final-comment` started:** —
- **Date accepted / rejected:** —

## Summary

Ship a CalculiX adapter as a subprocess plugin following the ADR-0009 (OpenFOAM) precedent. CalculiX is a mature, GPLv2-licensed open-source FEM code that brings **linear static, nonlinear (geometric + material), thermal (steady + transient), modal, harmonic, buckling, and small/large-sliding contact** behind a single binary — closing the largest "I can't finish my analysis in souxmar" gap for the mechanical / aerospace audience without growing the souxmar core. Capabilities are added under the `solver.{elasticity,thermal,modal,buckling}.calculix.*` namespace; results flow through the existing `souxmar_field_t` surface. **No ABI bump.** The adapter ships alongside two companion plugins: `writer.inp.calculix` (souxmar Mesh + BCs + materials → `.inp` deck) and `reader.frd.calculix` (CalculiX `.frd` results → `souxmar_field_t`).

## Motivation

The v1.0–v1.6 simulation surface is real but narrow on the structural side:

- `solver.elasticity.linear` is the only force-analysis path (FEniCSx adapter); for a mechanical engineer, **nonlinear and contact are table-stakes**, not nice-to-haves. RFC-009 lands the in-tree nonlinear contract in Sprint 35, but no in-tree solver implements it for the real-world cases users bring (snap-fit assemblies, gasketed flanges, bolt preload).
- `solver.modal.linear` (Sprint 33) is via FEniCSx + SLEPc — capable for clean meshes but historically fragile on Windows (R-016) and offers no harmonic-with-damping path until the harmonic story matures.
- Conjugate heat transfer in Sprint 34 is one-way thermal-structural through `coupler.thermal_to_structural`; the structural leg still routes through FEniCSx.

Concrete user reports the v1.4 forum and `bug-reports/` capture, all of which CalculiX answers in its own engine:

1. *"My bracket has a press-fit pin. FEniCSx doesn't have contact yet — I need to export and run in ABAQUS."* — CalculiX implements ABAQUS-compatible `*CONTACT PAIR`, `*SURFACE INTERACTION`, node-to-surface and surface-to-surface formulations.
2. *"I need to check post-yield behaviour on a 6061-T6 lifting eye."* — CalculiX has `*PLASTIC`, `*HYPERELASTIC`, `*VISCOELASTIC`, and isotropic + kinematic hardening.
3. *"Can souxmar do a linear-buckling factor of safety on a column?"* — CalculiX has `*BUCKLE` (Lanczos-based linear-buckling, eigenvalue extraction on the geometric stiffness).
4. *"The thermal-structural coupling in v1.5 diverges on my radiator design."* — CalculiX runs coupled thermal-mechanical directly via `*COUPLED TEMPERATURE-DISPLACEMENT`, no inter-process coupler needed.

CalculiX is also a strategic asset for the project independent of these specific gaps:

- **Trusted in regulated industries.** CalculiX has been used in production at aerospace primes and reviewed by NAFEMS — it is *the* open-source FEM solver an aerospace stress engineer will already recognise.
- **ABAQUS-compatible input deck.** `.inp` is an industry-standard interchange format. Once souxmar can *write* a clean `.inp`, the door opens to other ABAQUS-deck solvers (Code_Aster's `lance_aster` path, MoFEM, FrontISTR) at incremental cost.
- **Subprocess isolation makes the licence delta a non-issue.** CalculiX is GPLv2; we link none of its code (ADR-0009 pattern). The adapter is Apache-2.0 and ships an `.inp` deck + reads `.frd` results across a process boundary.

Timing: this RFC merges before Sprint 37 starts. It is the headline of the post-v1.6 "production solver coverage" block (Sprints 37–40, `v1.7`–`v1.9`).

## Proposal

Six layers, top-down.

### 1. Capability namespace

| Capability id                                | Sprint | CalculiX procedure                            |
| -------------------------------------------- | ------ | --------------------------------------------- |
| `solver.elasticity.calculix.linear`          | 37     | `*STATIC` (linear, default)                   |
| `solver.thermal.calculix.steady`             | 37     | `*HEAT TRANSFER, STEADY STATE`                |
| `solver.thermal.calculix.transient`          | 37     | `*HEAT TRANSFER` (transient)                  |
| `solver.elasticity.calculix.nonlinear`       | 38     | `*STATIC, NLGEOM` + `*PLASTIC` / `*HYPERELASTIC` |
| `solver.contact.calculix`                    | 38     | `*CONTACT PAIR` (node-to-surface + surface-to-surface) |
| `solver.buckling.calculix`                   | 38     | `*BUCKLE` (linear, Lanczos eigenvalue)         |
| `solver.modal.calculix`                      | 39     | `*FREQUENCY` (Lanczos, real eigenvalues)       |
| `solver.harmonic.calculix`                   | 39     | `*STEADY STATE DYNAMICS` (modal-superposition harmonic) |
| `solver.coupled.calculix.thermal_mechanical` | 39     | `*COUPLED TEMPERATURE-DISPLACEMENT`            |

Note the namespacing: every capability is `solver.<physics>.calculix.<variant>`, never `solver.calculix.<physics>`. The orchestrator (and the agent) selects by physics first; the `.calculix.*` suffix is just an implementer choice the user can override with `prefer: ` in pipeline YAML.

### 2. Subprocess harness (ADR-0009 reuse)

The plugin invokes the `ccx_2.21` (or later) binary via `souxmar::plugin::run_subprocess` — the same harness OpenFOAM uses. Per-stage workflow:

```
host calls solver.solve_fn(mesh, inputs, options, out_field)
    │
    ├─ writer.inp.calculix    → writes <run-dir>/job.inp
    ├─ run_subprocess("ccx_2.21", ["-i", "job"], cwd=<run-dir>)
    │     │
    │     ├─ stdout/stderr   → souxmar log stream + tagged "[ccx]"
    │     ├─ status code     → SOUXMAR_OK | SOUXMAR_E_SOLVER_DIVERGED | SOUXMAR_E_SOLVER_FAILED
    │     └─ files produced  → job.frd  (results),
    │                          job.dat  (history),
    │                          job.cvg  (convergence),
    │                          job.sta  (step summary)
    │
    └─ reader.frd.calculix    → reads job.frd, returns souxmar_field_t
```

No `find_package(CalculiX)`, no link against `libcalculix.so`. The plugin's `souxmar-plugin.toml` carries:

```toml
[plugin.subprocess]
binary       = "ccx_2.21"
binary_env   = "SOUXMAR_CALCULIX_PATH"
min_version  = "2.20"
license_note = "CalculiX is GPLv2. This plugin invokes it as an external binary and links no GPL'd code."
```

Crash isolation, signal masking, stdout/stderr capture, working-directory hygiene, cleanup-on-cancel — all already implemented by the harness. ADR-0009 covers the legal stance.

### 3. INP writer (`writer.inp.calculix`)

CalculiX consumes a subset of the ABAQUS `.inp` deck. Mapping:

| souxmar concept                              | CalculiX `.inp` directive                                  |
| -------------------------------------------- | ---------------------------------------------------------- |
| `Mesh` (nodes + element table)               | `*NODE` + `*ELEMENT, TYPE={C3D4,C3D10,C3D8,C3D20R,C3D6,...}` |
| Element-set per face tag                     | `*ELSET, ELSET=tag_<n>`                                    |
| Surface from per-face tag                    | `*SURFACE, NAME=surf_<n>`                                  |
| `material.elasticity.isotropic`              | `*MATERIAL, NAME=...` + `*ELASTIC, TYPE=ISO`               |
| `material.thermal.isotropic`                 | `*MATERIAL, ...` + `*CONDUCTIVITY` + `*SPECIFIC HEAT` + `*DENSITY` |
| `material.plasticity.isotropic_hardening`    | `*PLASTIC, HARDENING=ISO` (table from materials library)   |
| `bc.fixed`, `bc.displacement`                | `*BOUNDARY`                                                |
| `bc.pressure`                                | `*DLOAD, ... , P, <value>`                                 |
| `bc.force.distributed`                       | `*CLOAD`                                                   |
| `bc.thermal.temperature`                     | `*BOUNDARY, ...` on temperature DoF (11)                   |
| `bc.thermal.flux`                            | `*DFLUX`                                                   |
| `bc.thermal.convection`                      | `*FILM`                                                    |
| `contact.pair` (S38)                         | `*CONTACT PAIR` + `*SURFACE INTERACTION`                   |
| `solver.options{tolerance, max_iterations}`  | `*STEP, INC=<max_iter>` + `*CONTROLS, PARAMETERS=FIELD, ...` |
| `random_seed`                                | unused; CalculiX is deterministic at fixed inputs          |

The writer is a separate plugin (`writer.inp.calculix`) so it is independently testable, callable from the agent (`export.inp` tool, S37), and reusable by other ABAQUS-compatible adapters down the road.

Conformance suite: round-trip golden test — known souxmar `Mesh` + BC bag → expected `.inp` byte-identical to the golden, on all three OSes. (Newline normalisation handled in the writer; LF on disk, regardless of OS.)

### 4. FRD reader (`reader.frd.calculix`)

CalculiX writes results to a plain-text `.frd` file. The reader maps:

| FRD block       | souxmar field                                          |
| --------------- | ------------------------------------------------------ |
| `  -4  DISP `   | `displacement` (vector, 3 components, nodal)           |
| `  -4  STRESS`  | `stress` (tensor, 6 components, nodal)                 |
| `  -4  TOSTRAIN`| `strain.total` (tensor, 6 components, nodal)           |
| `  -4  PE`      | `strain.plastic` (tensor, 6 components, nodal) (S38)   |
| `  -4  NDTEMP`  | `temperature` (scalar, nodal)                          |
| `  -4  FLUX`    | `heat_flux` (vector, 3 components, nodal)              |
| `  -4  ERROR`   | `error_estimator` (scalar, nodal)                      |
| `  -4  CONTACT` | `contact.pressure`, `contact.gap` (S38)                |
| Step records    | populate `num_time_steps`; one frame per `*STEP`       |

For modal results (S39), the FRD `MODE` block emits one frame per mode shape; the reader synthesises a `frequencies` 1D field (length = mode count, units = Hz) and uses the time-series surface from RFC-006 so mode shapes play through the existing viewport animation path.

### 5. Material library handshake

CalculiX needs material cards; Sprint 35 (`v1.5`) ships the material library. The CalculiX adapter consumes the **same** material identifiers the v1.5 library exports. Mapping is *one-way at solve time*: the adapter reads from the library, never writes back.

When the user picks `aisi-4340` from the library, the writer emits:

```
*MATERIAL, NAME=aisi-4340
*ELASTIC, TYPE=ISO
 205000.0, 0.29
*DENSITY
 7.85e-9
*EXPANSION, ZERO=20.0
 1.23e-5
*CONDUCTIVITY
 44.5
*SPECIFIC HEAT
 4.75e8
```

(Units: N, mm, t, s, °C — CalculiX is unit-agnostic; the writer documents the convention in the `.inp` header.)

For Sprint 38's plasticity work, the library gains hardening curves; the writer emits `*PLASTIC` tables. Curve-data conformance: the writer rejects curves with non-monotonic strain (CalculiX requires monotonic), surfaces the violation through the agent diagnostics path (`explain_nonlinear_failure` from R-018 mitigation).

### 6. Agent tool surface (additive)

Six new agent tools across the four sprints. All follow the existing per-tool confirmation policy.

| Tool                                  | Sprint | Confirmation    | Purpose                                                            |
| ------------------------------------- | ------ | --------------- | ------------------------------------------------------------------ |
| `solve_with`                          | 37     | `auto`          | Already exists from S31 — gains `prefer: "calculix"` argument        |
| `export.inp`                          | 37     | `confirm-once`  | Write an `.inp` deck to a user-chosen path; subprocess writer        |
| `set_contact_pair`                    | 38     | `confirm-once`  | Configure a contact pair from two viewport surface selections        |
| `explain_nonlinear_failure`           | 38     | `auto`          | Read `.cvg` + `.sta`; produce a diagnostic with recommended action   |
| `set_buckling_load_factor_request`    | 38     | `auto`          | Configure `*BUCKLE` request (number of modes, search range)          |
| `compare_solver_results`              | 39     | `auto`          | Cross-solver validation: run the same pipeline with two solver capabilities, return field-norm + max-error summary |

Every tool ships ≥1 eval case in `tests/agent-eval/calculix-*.yaml`.

### Recap: what does and does not change in the ABI

**Unchanged** (this is the headline):

- `include/souxmar-c/abi.h` — `SOUXMAR_ABI_VERSION_MINOR` stays at **9**.
- `include/souxmar-c/solver.h` — vtable shape unchanged.
- `include/souxmar-c/field.h` — handle unchanged; CalculiX results map onto the existing surface.
- `include/souxmar-c/status.h` — no new codes; `SOUXMAR_E_SOLVER_DIVERGED`, `SOUXMAR_E_SOLVER_FAILED`, `SOUXMAR_E_NO_CONVERGENCE` already exist.

**Added** outside the ABI:

- New plugin manifests at `examples/plugins/calculix-solver/`, `examples/plugins/calculix-inp-writer/`, `examples/plugins/calculix-frd-reader/`.
- New capability strings (free-form; the `souxmar_registry_register_*` calls take a string).
- New agent tools (the dispatcher table grows; not an ABI-frozen surface — the agent tool *contract* is frozen, but adding new tools is the additive case that does not need an RFC bump on the contract itself — see `docs/AI_INTEGRATION.md`).
- New conformance suite directory `tests/conformance/calculix/`.

## Alternatives considered

### Alternative A: Wait for FEniCSx to grow nonlinear/contact

FEniCSx + DOLFINx has a roadmap for contact (`dolfinx_contact` is in early development as of 2026-Q1) and nonlinear via NewtonSolver.

Rejected because: (a) `dolfinx_contact` is not production-ready at the surface-to-surface formulation a mechanical engineer needs (frictionless point-contact only as of writing); (b) the FEniCSx Newton path requires the user to write a UFL form — directly contradicts the "engineer is not a Python developer" thesis; (c) the schedule risk is uncapped — we cannot promise a v1.7 nonlinear story by relying on an external roadmap. CalculiX's contact has been production-grade since the early 2010s.

### Alternative B: Link Code_Aster as a process-isolated adapter instead

Code_Aster is more mature than CalculiX on some fronts (broader element library, better thermo-mechanical coupling).

Rejected because: (a) Code_Aster is **LGPL** with strong copyleft semantics on its solver kernels — the subprocess pattern works legally but the user-facing operational story is worse (more dependencies, much heavier install footprint); (b) Code_Aster's input language (`.comm`) is Python-flavoured and harder to generate mechanically from souxmar's data model than `.inp`; (c) the aerospace audience recognises `.inp` instantly. We may *also* ship a Code_Aster adapter later under the same subprocess pattern — but the headline production solver is CalculiX.

### Alternative C: Bundle CalculiX in the souxmar installer

Ship `ccx` as part of the desktop installer so users do not need to install it separately.

Rejected because: (a) GPLv2 + Apache-2.0 in a single signed installer is legally workable (separate binaries, no derivative work) but operationally ugly — every souxmar release re-ships a CalculiX build; (b) Windows CalculiX builds are not officially distributed by the CalculiX project (community-maintained `bConverged` distribution); we would be re-publishing a third party's binary; (c) the user pain of "install CalculiX separately" is identical to "install ffmpeg separately" (RFC-006) and we already accepted that pattern. We document a recommended `ccx` install path per OS in `docs/INFRA_STATUS.md`; the Linux path is one apt-get; macOS is `brew install calculix-ccx`; Windows points to `bConverged`'s installer.

### Alternative D: GUI deck-import path only — no real adapter

Just ship a deck importer/exporter and tell users to run CalculiX themselves.

Rejected because: (a) defeats the agent's job — the agent cannot "solve this with contact" if all it can do is hand the user a deck; (b) the pipeline runner cannot orchestrate a multi-stage analysis (mesh → solve → postproc) without invoking the solver; (c) breaks the "one product, one workflow" UX. Deck-export *is* a feature we ship (`export.inp`) on top of the adapter — but it cannot be the only feature.

### (Considered and rejected: do nothing)

Without the CalculiX adapter, the v1.4–v1.6 story is "souxmar can do linear structural and steady heat — for anything more you export to ABAQUS." That is the exact reason engineers abandon young CAE tools. Doing nothing concedes the production-engineering audience to legacy commercial solvers — the audience the project is for.

## Drawbacks

- **External binary dependency.** Same pattern as OpenFOAM and ffmpeg, same installer pain — we accept it three times now. Default error UX in the desktop must be **excellent**, not just functional: "CalculiX not found — install it (link) or set SOUXMAR_CALCULIX_PATH" with a one-click "open install docs" button.
- **Performance gap with FEniCSx on linear cases.** CalculiX's default direct solver (SPOOLES) is single-threaded and slower than PETSc with a parallel iterative solver on >1M-DoF meshes. Mitigation: the orchestrator's automatic-solver selection prefers FEniCSx for `solver.elasticity.linear` when no `.calculix` is explicitly requested.
- **INP-format edge cases.** CalculiX accepts a strict subset of ABAQUS-INP; some directives are silently ignored, some error out. The writer's conformance suite covers the directives we emit, but third-party `.inp` files dropped into souxmar by users are out of scope for this RFC.
- **Plugin surface grows by three plugins, not one.** `calculix-solver`, `calculix-inp-writer`, `calculix-frd-reader` together. Mitigated by them sharing a common library `examples/plugins/calculix-common/` under the same source tree.
- **Cross-platform CalculiX version drift.** `ccx_2.21` on macOS Homebrew vs the bundled `ccx 2.20` on a community Windows installer can differ in `.frd` block formats. Mitigation: writer emits an `INP` directive (`*HEADING`) carrying the souxmar version + targeted CalculiX version; reader version-checks the `.frd` header and surfaces a clear error if the run was done with an unsupported version.
- **Eval cost.** Six new agent tools means six new eval cases in CI; CalculiX runs are 1–60s each, adding ~3 minutes to the agent-eval CI run. Below the existing 15-minute budget; flagged in `docs/ENGINEERING_PRACTICES.md`.

## Migration plan

- **Existing in-tree / out-of-tree plugins:** unaffected. No ABI change.
- **Existing pipeline files:** unaffected. New capabilities are opt-in via `prefer: calculix` or by directly naming `solver.elasticity.calculix.linear` in the pipeline.
- **Existing example projects:**
  - `examples/cantilever-beam/` gains an alternate pipeline `pipeline.calculix.yaml` that runs the same beam through CalculiX; both pipelines produce comparable stress fields and the `compare_solver_results` tool can validate the agreement.
  - `examples/thermal-fin/` gains a CalculiX variant.
  - New: `examples/snap-fit-assembly/` — nonlinear + contact (S38), with no FEniCSx counterpart.
  - New: `examples/buckling-column/` — S38.
- **Saved chat sessions:** the new `solve_with prefer=calculix` invocation is additive. Sessions referencing only FEniCSx tools replay identically.
- **Settings file:** new `[adapters.calculix]` section in `~/.souxmar/settings.toml` (`path`, `version_min`, `timeout_seconds`). Absent → harness defaults.
- **Documentation:** new `docs/tutorials/contact-with-calculix.md`, `docs/tutorials/post-yield-bracket.md`, `docs/tutorials/buckling-column.md`; updates to `docs/PLUGIN_SDK.md` (subprocess pattern second case study), `docs/AI_INTEGRATION.md` (6 new tools), `docs/INFRA_STATUS.md` (CalculiX install paths per OS).

## Pre-mortem

It is 2027-05-12. RFC-011 went badly. What happened:

We shipped v1.7 with the CalculiX adapter and discovered three classes of failure in the first six weeks. First, the INP writer round-trip golden tests were on Linux with LF line endings; a contributor on Windows CRLF environment regenerated the goldens and broke determinism — we now require the conformance harness to byte-normalise. Second, contact pair detection on assemblies with co-planar but mis-tagged surfaces (a UX bug, not a CalculiX bug) routinely produced "no contact pressure" results that users blamed the solver for; the `set_contact_pair` agent tool needed an explicit "verify-contact-found" post-condition. Third, the `bConverged` Windows CalculiX distribution version-drifted ahead of our test matrix; users on a fresh Windows install hit a `.frd` block we didn't parse and the reader silently returned an empty field instead of erroring loudly. The cumulative reputational hit was "souxmar's nonlinear support is unreliable" — the exact opposite of what we shipped.

Leading indicators to watch in the first six months of v1.7:

- Any user-reported "CalculiX returned empty results" issue — implies version-drift in `.frd` parsing.
- `solve_with prefer=calculix` failure rate above 2% on the bundled example projects in user telemetry.
- Convergence-failure rate on `solver.elasticity.calculix.nonlinear` above the FEniCSx baseline by >50% (indicates we are picking bad defaults for `*CONTROLS`).
- Contact-pair "no contact detected" rate above 10% on tutorial cases — UX issue.
- Agent eval suite p95 latency above 2× baseline — adapter is too slow on the CI runner; we picked the wrong matrix.

## Open questions

1. **Default unit system.** CalculiX is unit-agnostic; we have to pick one for the writer to be deterministic. Lean N–mm–t–s–°C (the de-facto ABAQUS convention for mechanical work); document at Sprint 37 day 1; consider a per-project override later.
2. **`*STEP, AMPLITUDE=` for transient loading curves.** Defer to Sprint 38 — needs the `bc.amplitude` shape in the materials/BC bag.
3. **Per-element-set assignment for mixed-element models.** Sprint 37 supports a single material per body; Sprint 38 needs multiple materials on a single mesh (composite layups). Tracked as an Sprint 38 PR item.
4. **CalculiX MPC (multipoint constraints) — `*MPC`, `*EQUATION`.** Out of scope for this RFC; opens the door to rigid-body / kinematic-coupling features which deserve their own RFC.
5. **Postproc derived quantities.** Von Mises is in `.frd` if requested (`*EL FILE, OUTPUT=3D`); principal stresses, Tresca, hydrostatic — we can derive in souxmar postproc rather than asking CalculiX. Decide per quantity in Sprint 37 PR 6.
6. **Determinism gate.** CalculiX is deterministic with SPOOLES on a single thread; the multithreaded PARDISO build (if user has it) is *not* bit-deterministic across runs. Our gate runs with `CCX_NPROC_EQUATION_SOLVER=1`; document in `auditing-determinism` skill.
7. **Restart files.** CalculiX writes `.rin` for restart; for long-running nonlinear analyses this is valuable. Defer to a v1.8 follow-up — needs orchestrator support for "resume a stage."

## Implementation plan

Sixteen PRs across four sprints. Each sprint releases.

### Sprint 37 — `v1.7` — linear + thermal MVP

- [ ] **PR 1 — `calculix-inp-writer` plugin.** Mesh + linear-elastic material + linear BCs → `.inp`. Conformance: byte-identical round-trip on Linux/macOS/Windows.
- [ ] **PR 2 — `calculix-frd-reader` plugin.** `.frd` `DISP` + `STRESS` + `NDTEMP` + `FLUX` blocks → `souxmar_field_t`. Version-check header.
- [ ] **PR 3 — `calculix-solver` plugin, linear path.** Provides `solver.elasticity.calculix.linear`, `solver.thermal.calculix.steady`, `solver.thermal.calculix.transient`. Subprocess driver. Stdout/stderr capture and tagged log streaming.
- [ ] **PR 4 — Examples.** `examples/cantilever-beam/pipeline.calculix.yaml`, `examples/thermal-fin/pipeline.calculix.yaml`. `compare_solver_results` regression matrix.
- [ ] **PR 5 — Agent tools.** `export.inp`, `solve_with` `prefer:` argument; eval cases.
- [ ] **PR 6 — Docs + install paths.** `docs/INFRA_STATUS.md` per-OS install matrix; `docs/PLUGIN_SDK.md` subprocess-plugin second case study; `docs/tutorials/cantilever-with-calculix.md`.
- [ ] ADR-0043 filed: `docs/adr/0043-calculix-subprocess-isolation.md` — captures the legal stance + the choice of `.inp`/`.frd` as interchange formats.
- [ ] Release `v1.7`.

### Sprint 38 — `v1.8` — nonlinear + contact + buckling

- [ ] **PR 7 — Plasticity writer.** `*PLASTIC` table emission from material library hardening curves; non-monotonic-strain rejection with diagnostic.
- [ ] **PR 8 — Nonlinear solver path.** Provides `solver.elasticity.calculix.nonlinear` (`*STATIC, NLGEOM=YES`). Reads `.cvg` + `.sta`; produces convergence history field.
- [ ] **PR 9 — Contact.** Provides `solver.contact.calculix`. Surface-from-tags writer; node-to-surface + surface-to-surface formulations; per-pair friction coefficient. `examples/snap-fit-assembly/`.
- [ ] **PR 10 — Buckling.** Provides `solver.buckling.calculix`. `*BUCKLE` step; eigenvalue extraction reader; mode-shape display through the RFC-002 surface. `examples/buckling-column/`.
- [ ] **PR 11 — Agent tools + diagnostics.** `set_contact_pair`, `explain_nonlinear_failure`, `set_buckling_load_factor_request`; eval cases.
- [ ] Release `v1.8`.

### Sprint 39 — modal + harmonic + cross-solver validation

- [ ] **PR 12 — Modal + harmonic.** Provides `solver.modal.calculix`, `solver.harmonic.calculix`. Modal results into the RFC-006 time-series playback path; harmonic results as frequency-indexed field.
- [ ] **PR 13 — Coupled thermal-mechanical.** Provides `solver.coupled.calculix.thermal_mechanical`. Drop-in replacement for the staggered `coupler.thermal_to_structural` chain when the user wants tight coupling.
- [ ] **PR 14 — Cross-solver validation matrix.** Automated CI: `cantilever-beam`, `thermal-fin`, `modal-beam`, `pipe-bend` (where applicable) all run on both FEniCSx and CalculiX; field norms must agree within documented tolerances. `compare_solver_results` agent tool.
- [ ] **PR 15 — Validation report.** Per the `validating-solver` skill: analytical comparisons (NAFEMS LE-1, LE-10, T2); convergence study; cross-solver report. Published as `docs/validation/calculix-v1.8.pdf` (generated).

### Sprint 40 — `v1.9` — fatigue postproc + report generator

- [ ] **PR 16 — Fatigue postproc.** Goodman / Soderberg / Gerber surfaces from cyclic-load CalculiX results; safety-factor field. (Builds on CalculiX nonlinear; uses the materials library S-N curves added in S38.) Release `v1.9`.

## References

- `docs/SPRINT_PLAN.md` — Post-v1.6 plan, Sprints 37–40 (added by this PR).
- `docs/adr/0009-openfoam-process-isolation.md` — Subprocess-plugin precedent.
- `docs/rfcs/0002-field-stream-protocol.md` — Field handle CalculiX results map onto.
- `docs/rfcs/0006-time-series.md` — Mode-shape playback path.
- `docs/rfcs/0007-...` — Modal-solver contract this RFC implements an instance of (RFC slot reserved in the post-v1.3 block).
- `docs/rfcs/0009-...` — Nonlinear-solver contract this RFC implements an instance of (RFC slot reserved in the post-v1.3 block).
- `docs/AI_INTEGRATION.md` — Tool-surface contract; 6 new tools.
- `docs/PLUGIN_SDK.md` — Plugin-type taxonomy; subprocess-pattern second case study.
- `docs/ROADMAP.md` — "Nonlinear and transient solvers" post-1.0 theme this RFC delivers concretely.
- `docs/ENGINEERING_PRACTICES.md` — eval-suite CI budget impact.
- CalculiX home: `https://www.dhondt.de/`; INP/FRD format documentation in the CalculiX manual.
- NAFEMS LE-1 / LE-10 / T2 benchmarks for validation matrix.
