# Roadmap

A phased plan to get from an empty repository to a usable v1.0. Dates are deliberately omitted: this is a volunteer-driven open-source project at its inception. Each phase has a definition of done, not a delivery date.

## Phase 0 — Design (current)

**Goal:** lock in the architecture before code is written. The ABI, data model, agent tool surface, and on-disk pipeline format are the most expensive things to change later, so they are the first things to settle.

Definition of done:
- [x] `docs/VISION.md` accepted.
- [x] `docs/ARCHITECTURE.md` accepted.
- [x] `docs/PLUGIN_SDK.md` accepted.
- [x] `docs/GOVERNANCE.md` accepted.
- [x] `docs/DESKTOP_APP.md` accepted.
- [x] `docs/AI_INTEGRATION.md` accepted.
- [x] `docs/UI_DESIGN.md` accepted (design tokens, dim palette, components).
- [x] `docs/BUSINESS_MODEL.md` accepted (open-core, tiers, marketplace).
- [ ] At least three external reviewers have read the docs and filed substantive feedback.
- [ ] `LICENSE`, `CODE_OF_CONDUCT.md`, `CONTRIBUTING.md` checked in.
- [ ] Project name and namespace finalised (currently `souxmar`); trademark filing initiated.
- [ ] Visual identity: logo + dim/lights-out theme tokens locked.

## Phase 1 — Skeleton

**Goal:** the empty house with all the rooms framed. No real algorithms yet; just the skeleton everything else hangs from.

- CMake build system, vcpkg manifest, CI matrix (Linux/macOS/Windows × x86_64/arm64).
- `libsouxmar-core` with `Geometry`, `Topology`, `Mesh`, `Field` types — no I/O, no algorithms, just the data model and unit tests.
- `libsouxmar-plugin` with discovery, manifest parsing, ABI versioning, and a no-op test plugin.
- `souxmar` CLI that lists plugins and runs a hand-written "identity" pipeline (input file → output file, no transformation).
- `souxmar_add_plugin` CMake macro and a documented "hello world" out-of-tree plugin.

Definition of done: an external developer can clone the repo, build it, write a plugin in their own repo against the published headers, install it, and see it appear in `souxmar plugin list`.

## Phase 2 — First end-to-end pipeline

**Goal:** prove the architecture works by getting one real problem solved end to end. Pick the canonical cantilever beam.

- `adapters/opencascade` — STEP import producing a `Geometry`.
- `plugins/mesher-native` — a basic Delaunay tetra mesher. Quality irrelevant; correctness mandatory. (We are not competing with Netgen here; we are proving the abstraction.)
- `plugins/solver-elasticity` — linear isotropic elasticity, direct solve via PETSc.
- `adapters/vtk` — `.vtu` writer.
- `pipeline/` — DAG runner with the YAML format and content-addressed cache.
- `examples/cantilever-beam/` — STEP file, pipeline YAML, expected output.

Definition of done: `souxmar run examples/cantilever-beam/cantilever.souxmar.yaml` produces a `.vtu` file that opens in ParaView and shows a recognisably correct stress field.

## Phase 3 — Adapters and Python

**Goal:** open the doors to the existing ecosystem.

- `adapters/gmsh` — alternative mesher, opt-in at build time.
- `adapters/fenicsx` — alternative FEM solver, opt-in.
- `adapters/openfoam` — CFD reference adapter (case generation, solver invocation, result read-back).
- `adapters/blender` — `.blend` file import for concept-stage and architectural geometry.
- `bindings/python/` — `pysouxmar` wheel on PyPI. Build pipelines programmatically; iterate in Jupyter; load the same plugins as the CLI.
- Python-plugin shim: `@sx.plugin.mesher` decorator (see `PLUGIN_SDK.md`).
- Plugin conformance suite shipped as `souxmar-conformance` and runnable by external authors.

Definition of done — **not true today; pysouxmar is not on PyPI**
<!-- unpublished-ok: pysouxmar -->: a researcher can `pip install pysouxmar`,
write a 20-line script that imports a STEP file and runs an FEM analysis or a Blender model and runs a CFD analysis via OpenFOAM, and see results in ParaView.

## Phase 4 — Desktop application + AI integration

**Goal:** the engineer's product. This is what turns the library into a thing a non-programmer mechanical / aerospace / architectural engineer wants to use.

- `desktop/src-tauri/` — Tauri 2 shell (Rust), Tauri commands wrapping `libsouxmar-pipeline`, signed installers (`.dmg`, `.msi`, `.AppImage`/`.deb`/`.rpm`).
- `desktop/src/` — React + TypeScript frontend with chat panel, 3D viewport (Three.js + VTK.js), pipeline editor (DAG strip), inspector. Tokens-based design system using the Twitter dim palette per `UI_DESIGN.md`. Lights-out and light theme variants.
- `src/ai/` — `libsouxmar-ai`: provider abstraction (Anthropic, OpenAI, Ollama), agent tool dispatcher, OS-keychain-backed BYOK key storage, audit log, per-tool confirmation policy.
- Agent tool surface frozen at the v1 contract (read tools, pipeline tools, mesh, BC, material, solve, postproc, file-write tools).
- Auto-update infrastructure (signed Tauri update manifests on a static CDN).
- Performance budgets met: cold launch < 1.5 s, 1M-cell open < 2 s, viewport rotate at 60 fps on a 5M-cell mesh, first chat token < 800 ms (BYOK direct).
- Accessibility: WCAG AA across the dim theme, full keyboard nav, command palette, screen-reader pass on macOS / Windows / Linux.

Definition of done: an engineer downloads the desktop app on macOS, Windows, or Linux, supplies their own provider key — Claude, GPT, Grok, or any other OpenAI-compatible service, or points at a local Ollama, drops in a STEP file, and produces a viewable stress result by chatting with the agent — without writing a line of code.

## Phase 5 — Hardening for v1.0

**Goal:** the ABI, on-disk pipeline format, and agent tool contract freeze here. Everything that follows is additive within ABI v1.

- Full ASAN / TSAN / UBSAN clean across the test suite on at least Linux.
- Out-of-core / streaming buffer support, validated on a > 100M-cell mesh.
- Crash isolation of plugin faults (signal/SEH frame around every plugin call) verified by a deliberate-segfault plugin in CI.
- Crash isolation of the AI layer: a malformed provider response or a tool exception cannot kill the desktop app; both surface as recoverable in-chat errors.
- Documentation: API reference, plugin authoring tutorial, agent tool reference, four example projects (cantilever stress / thermal fin / pipe-bend CFD via OpenFOAM / mesh-algorithm comparison study).
- Plugin index live, with at least three third-party plugins listed, at least one developed outside the project's core team.
- Release tooling: signed source tarball, `pysouxmar` wheel matrix, per-OS desktop installers (notarised on macOS, EV-signed on Windows, GPG-signed on Linux).

Definition of done: tag `v1.0.0`, freeze ABI v1 + agent tool contract v1, publish.

## Phase 6 — Manufacturing and marine verticals

**Goal:** answer the question the pipeline could not previously answer — "can this part actually be built, and will it survive where it has to work?" Everything here rides the frozen ABI v1 capability namespaces; no host, ABI, or data-model change is required, which is the point of the phase.

- `examples/plugins/am-layered-mesher` — `mesher.am.layered`: layer-aligned Hex8 build mesh with the layer index carried as the cell tag and per-side boundary-face tags.
- `examples/plugins/lattice-reader` — `reader.lattice`: parametric strut lattices (cubic / bcc / fcc / octet / diamond) generated from a spec file into an Edge2 beam mesh. Parametric generation lives in `reader.*` because the mesher ABI passes no value bag.
- `examples/plugins/am-thermal` — `solver.am.thermal.lpbf` + `postproc.am.melt_pool`: Rosenthal layer-wise LPBF thermal history, melt-pool depth, normalised enthalpy, lack-of-fusion / keyhole porosity risk.
- `examples/plugins/am-distortion` — `solver.am.distortion.inherent_strain` + `postproc.am.residual_stress`: inherent-strain distortion with Stoney-type layer-wise curvature accumulation, plus a residual-stress read-back.
- `examples/plugins/am-polymer` — `solver.am.polymer.fff` + `postproc.am.bond_strength`: FFF interlayer thermal cycling and reptation-healing bond quality.
- `examples/plugins/am-manufacturability` — `solver.am.overhang`, `solver.am.printability`, `solver.am.buildtime`: DfAM screening on the mesh alone. These are `solver.*` rather than `postproc.*` because `postproc.*` requires an input field and a manufacturability check has none.
- `examples/plugins/am-slicer` — `writer.am.gcode`, `writer.am.cli`, `writer.am.report`: planar slicing to FFF G-code and Common Layer Interface ASCII, plus a Markdown build report.
- `examples/plugins/marine` — `solver.marine.hydrostatic`, `solver.marine.hull_collapse`, `solver.marine.corrosion`, `writer.marine.qualification_report`: depth load cases, preliminary pressure-hull collapse margin, seawater corrosion indicators, and an advisory AM-part qualification dossier.
- Six additive agent tools (`propose_am_setup`, `check_printability`, `set_build_orientation`, `estimate_build_cost`, `apply_hydrostatic_load`, `check_marine_integrity`), two workbench panels (Manufacturing, Marine), four runnable examples, and a curated `examples/materials/am-marine.toml` library.
- Decisions: [ADR-0044](adr/0044-manufacturing-capability-namespaces.md) (capability-namespace placement), [ADR-0045](adr/0045-agent-tool-contract-am-ratchet.md) (additive tool ratchet), [RFC-0012](rfcs/0012-am-process-simulation.md) (process-simulation contract).

**What this phase is honest about.** Every model in it is closed-form or heuristic, with a named literature source and an explicit statement of what it ignores. They are screening and preliminary-sizing aids: a distortion prediction is only as good as its hand-entered calibration factor, a printability score is a documented weighting rather than a guarantee, corrosion rates are indicative literature values, and the qualification dossier is a checklist derived from public practice — souxmar is not a classification society and issues no approval. Calibrated, mesh-resolved process solvers are a later phase (Sprints 41–44 in [`attic/SPRINT_PLAN.md`](attic/SPRINT_PLAN.md)); topology optimisation stays out of this phase and remains deferred to a dedicated block of its own.

Definition of done: an engineer can take an STL or a build-box geometry, run a manufacturability + build-cost check from chat, see the overhang and printability fields in the viewport, produce a G-code or CLI slice plus a Markdown build report, and — for a subsea part — get depth load cases, a collapse margin, and an advisory qualification dossier, all from one pipeline file on Linux, macOS, and Windows with byte-identical results.

## Post-1.0 themes

These do not have a phase; they are areas the project grows into after the v1 ABI is stable. Order is illustrative, not prescriptive.

- **Nonlinear and transient solvers.** Plastic, hyperelastic, dynamic. New solver plugins; data-model extensions for time series.
- **Higher-order elements.** p-refinement, hierarchical bases. New `element.*` plugins; quadrature-rule registry in core.
- **GPU back-end.** A second `LinearAlgebra` implementation behind the existing handle abstraction. No core change should be needed.
- **Multiphysics coupling.** Conjugate heat transfer (FEM ⇆ CFD via OpenFOAM adapter), fluid-structure interaction. Strong test for the data-model extensibility story.
- **Distributed execution.** A pipeline orchestrator backend that can fan stages out across nodes. Almost certainly its own component, not a core change.
- **Managed services (Pro/Team/Enterprise).** Managed AI proxy with billing; encrypted cloud sync; hosted compute offload; SSO; on-prem AI proxy for regulated industries. See [`BUSINESS_MODEL.md`](BUSINESS_MODEL.md).
- **Plugin marketplace.** Hosted binaries + Stripe billing for paid plugins; revenue share with authors. Open-index plugins remain free regardless.
- **Lightweight in-app geometry edits.** Repositioning, simple feature suppression, parameter dial-tweaks on imported parametric models. Full parametric modelling stays out of scope.

## Things we have decided not to do

Documented here so we do not have to relitigate them every six months.

- A unified meshing language à la Gmsh `.geo`. The pipeline YAML, the Python API, and the chat agent are the three front doors; we will not introduce a fourth.
- A hosted SaaS desktop app. Desktop binary in v1; the Pro tier adds optional cloud services it calls into.
- An "Electron everything" architecture. Tauri keeps bundles small and the security model clean.
- Built-in cloud orchestration in the open-source core. Single-machine in v1; distributed compute is a Pro service that the open desktop app calls into, not a core feature.
- A bespoke "souxmar binary hub" hosting open-source plugin binaries. The plugin index is a list of links; the *paid* marketplace is a separate service for authors who opt in.
- Telemetry on by default. Crash reports require a checkbox on first launch and remain revocable.
- Closed governance, foundations, or membership tiers in v1. Two-tier role model (reviewer / maintainer) until the project demonstrably needs more.
- Relicensing the open-source artefacts away from Apache 2.0. Ever. See [`BUSINESS_MODEL.md`](BUSINESS_MODEL.md).
