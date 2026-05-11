# souxmar

An open-source CAE platform: parametric CAD, mesh generation, FEM and CFD, post-processing — wrapped in a cross-platform desktop app with an agentic AI chat that can drive the entire pipeline.

For mechanical, structural, aerospace, and architectural engineers who want a Cursor-style experience for simulation work: open the app, describe the problem in chat, watch it mesh and solve, inspect results in a built-in viewport. C++20 core, Python bindings, stable C plugin ABI, Tauri + React desktop app, Apache 2.0.

souxmar does not replace FreeCAD, Gmsh, FEniCSx, OpenFOAM, Blender, or ParaView. It unifies them under a shared data model and a stable plugin ABI, then puts a modern UI and an agentic AI on top.

## Why

Today, going from a CAD model to results means stitching half a dozen projects together with bespoke Python glue, four file formats, four release cycles, and at least one dropped boundary tag. Reproducing a colleague's setup is hard. Trying a new meshing algorithm against a real industrial model is harder. souxmar treats the pipeline itself — and the user-facing app around it — as the product.

Read [`docs/VISION.md`](docs/VISION.md) for the long version.

## Surfaces

- **Desktop app** (macOS / Windows / Linux) — chat panel, 3D viewport, pipeline editor, inspector. The primary surface for engineers.
- **CLI** (`souxmar`) — for CI, batch runs, scripting.
- **Python** (`pysouxmar`) — for notebooks and embedding in larger tools.
- **Plugin SDK** — stable C ABI for shipping your own meshers, solvers, elements, readers, writers as out-of-tree binaries.

## How it's funded (open-core)

The library, plugin SDK, CLI, Python bindings, **and the desktop app itself** are open source under Apache 2.0 — no crippled "community edition." Optional managed services (managed AI, cloud sync, hosted compute, team features) are commercial. See [`docs/BUSINESS_MODEL.md`](docs/BUSINESS_MODEL.md).

The free tier is the full product. You bring your own Anthropic / OpenAI / local-Ollama key, your own compute, and you owe us nothing.

## Status

**Second public pre-release — `v0.9.0-beta2` tagged 2026-05-11.** Source, Linux x86_64 CLI tarball, and Python sdist are attached to the [GitHub release](https://github.com/souxmar/souxmar/releases/tag/v0.9.0-beta2). Plugin C ABI is **frozen FINAL at v1.2** ([ADR-0008](docs/adr/0008-abi-v1-final-freeze.md)) — unchanged from beta1. Agent tool contract is a **freeze candidate** at 18 tools ([ADR-0010](docs/adr/0010-tool-contract-v1-freeze-candidate.md)); final freeze targets Sprint 9 push 1. The desktop app and hosted services are still explicitly **not** in this release — see the Sprint 8 retro ([`docs/retros/sprint-08.md`](docs/retros/sprint-08.md)) and the SPRINT_PLAN.md roadmap.

What changed since beta1 (Sprint 8 in full):

- **Subprocess plugin harness** — `souxmar::plugin::run_subprocess` with cross-platform crash isolation, mandatory timeouts, stream-capped capture. Foundation for every external-binary plugin going forward.
- **OpenFOAM CFD adapter** (opt-in) per [ADR-0009](docs/adr/0009-openfoam-process-isolation.md) — `simpleFoam` / `pimpleFoam` / `interFoam` invoked via subprocess only, never linked. **Closes R-003.** Real Tet4 → polyMesh translator (face dedup + owner/neighbour bookkeeping + boundary patch extraction) lands in push 6.
- **Blender (.blend) importer** (opt-in) — `blender -b --python-expr "bpy.ops.wm.obj_export"` via the same subprocess harness; round-trips .blend → OBJ → Tri3.
- **Wavefront OBJ reader** (always-on) — all four `f` field forms supported, polygon faces fan-triangulated.
- **CFD-aware agent vocabulary** — `apply_inlet` / `apply_wall` / `apply_outlet` siblings of `set_bc`; `propose_cfd_setup` heuristic planner; `validate_bcs` sanity check. Catalogue 12 → 18 tools.
- **Always-on `cfd-stub` solver** + the `examples/pipe-bend/` example wiring the full mesh → CFD → write chain.

The ABI v1 soak that ran across Sprints 5–7 stayed clean through Sprint 8 — zero ratchet events this sprint, frozen-header surface untouched. The freeze is permanent for the entire 1.x release series; `scripts/check-frozen-headers.sh` enforces the ratchet on every PR. The tool-contract candidate has its own non-blocking gate (`scripts/check-tool-contract.sh`) that flips to blocking when the final freeze ADR lands.

Runnable today:

- **CLI**: `souxmar run <pipeline.yaml>`, `souxmar plugin list`, `souxmar agent {list,invoke}` (with `--audit-log`, `--budget-config`, `--yes`), `souxmar-conformance <dir>`, `souxmar-eval <evals-dir>`.
- **Python**: `pip install pysouxmar` → parser, registry, loader, runner, cache, **18 agent tools**, audit log, first-class `SessionBudget.on_threshold` callback, `.souxmar/budget.toml` loader.
- **Plugin SDK**: **frozen-final C ABI v1.2** across six capability namespaces (`reader.*`, `mesher.*`, `solver.*`, `writer.*`, `postproc.*`, plus the bulk-buffer ingest path); `souxmar_add_plugin` CMake macro; conformance suite + CI lockdown gate; host-side **subprocess harness** for plugins that drive external binaries.
- **Eleven in-tree reference plugins** (always-on): hello-mesher, grid-mesher, hello-writer, vtu-writer, heat-solver, elasticity-stub, cfd-stub, scalar-magnitude, mesh-quality, stl-reader, obj-reader.
- **Five opt-in external adapters**: `occt-reader` (`-DSOUXMAR_WITH_OPENCASCADE=ON`, STEP / IGES), `gmsh-mesher` (`-DSOUXMAR_WITH_GMSH=ON`), `fenicsx-solver` (`-DSOUXMAR_WITH_FENICSX=ON`, FEM Poisson), `openfoam-solver` (`-DSOUXMAR_WITH_OPENFOAM=ON`, three CFD capabilities), `blender-reader` (`-DSOUXMAR_WITH_BLENDER=ON`, `.blend` import).
- **Four runnable examples**: `examples/cantilever-beam/`, `examples/thermal-fin/`, `examples/stl-cube/`, `examples/pipe-bend/`. Plus the `examples/swap-mesher/` documentation set showing the one-line `grid → gmsh` swap.
- **Out-of-core mesh streaming**: mmap-backed `souxmar_buffer_t` v2. `souxmar_mesh_from_buffers` routes transparently to heap or mmap.
- **Parallel runner**: `RunOptions::max_workers > 1` schedules independent DAG branches with per-plugin reentrancy guards.
- **Agent tool surface v1 candidate**: 18 tools across categories Read / Mesh / BC / CFD / Material / Solve / Field / Pipeline / Discovery / Export / UI. Structured audit log, per-project token budget config. **30-task agent eval suite** runs nightly.
- **Perf-nightly CI** + bulk-vs-incremental mesh-construction benchmark + heap-vs-mmap buffer benchmark.

Not yet done — deliberately scoped out of `0.9.0-beta2`:

- **No Tauri desktop app yet** (Sprint 11+ per the revised plan); CLI and Python only.
- **No per-patch CFD BC routing yet** — `apply_inlet`/`apply_wall`/`apply_outlet` stage BCs but `openfoam-solver` writes a single "walls" patch. Per-face-tag exposure on the C ABI lands as an additive-minor v1.3 ratchet in Sprint 9.
- **No production-grade FEM solver yet** — `solver.heat.linear` + `solver.elasticity.linear` are closed-form demonstrations; the FEniCSx adapter (opt-in, Poisson only in v1) handles the real case.
- **No BYOK AI provider client yet** (Anthropic / OpenAI / Ollama). The budget / audit / tool-dispatch plumbing is fully in place; the eval suite scripts the same dispatch path.
- **No signed installers / auto-update** yet — `release.yml` ships source + Linux CLI tarball + Python sdist; macOS notarisation + Windows EV signing land with the desktop app.

## Building

Prerequisites:

- CMake ≥ 3.25, Ninja, a C++20 compiler (GCC 13 / Clang 17 / AppleClang / MSVC 19.36+)
- [vcpkg](https://github.com/microsoft/vcpkg) — cloned and `VCPKG_ROOT` exported

```bash
git clone https://github.com/souxmar/souxmar.git
cd souxmar

export VCPKG_ROOT="$HOME/vcpkg"   # or wherever you cloned vcpkg

cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
```

Other presets (see [`CMakePresets.json`](CMakePresets.json)): `ci-linux-gcc`, `ci-linux-clang`, `ci-macos`, `ci-windows`, `asan`, `tsan`.

The first `cmake --preset` invocation builds vcpkg dependencies from source (~5 min for the default feature set; longer if heavy adapters are enabled). Subsequent runs use the vcpkg binary cache.

## Documents

Product & architecture:

- [`docs/VISION.md`](docs/VISION.md) — purpose, target users, scope, non-goals
- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — layered architecture, data model, dependencies
- [`docs/DESKTOP_APP.md`](docs/DESKTOP_APP.md) — Tauri + React stack, layout, performance budgets
- [`docs/AI_INTEGRATION.md`](docs/AI_INTEGRATION.md) — agentic chat, BYOK credentials, tool surface
- [`docs/UI_DESIGN.md`](docs/UI_DESIGN.md) — design system using the Twitter dim palette
- [`docs/PLUGIN_SDK.md`](docs/PLUGIN_SDK.md) — C ABI, plugin lifecycle, versioning
- [`docs/BUSINESS_MODEL.md`](docs/BUSINESS_MODEL.md) — open-core model, tiers, marketplace

Engineering & process:

- [`docs/SPRINT_PLAN.md`](docs/SPRINT_PLAN.md) — 24-sprint plan with team commitments, exit criteria, risks
- [`docs/TEAM_STRUCTURE.md`](docs/TEAM_STRUCTURE.md) — six-team org, RACI, hiring sequence, on-call
- [`docs/ENGINEERING_PRACTICES.md`](docs/ENGINEERING_PRACTICES.md) — quality bar, perf budgets, security, observability
- [`docs/GOVERNANCE.md`](docs/GOVERNANCE.md) — upstream merge process, RFCs, maintainer roles
- [`docs/ROADMAP.md`](docs/ROADMAP.md) — phased delivery plan
- [`docs/adr/`](docs/adr/) — Architecture Decision Records (template + first three: C ABI, Tauri, BYOK)

Claude skills (`.claude/skills/`):

- `developing-souxmar-plugin` — plugin SDK walkthrough
- `reviewing-abi-changes` — ABI / agent-tool contract gating
- `writing-souxmar-rfc` — Tier-3 RFC process
- `benchmarking-souxmar` — perf budgets and regression triage
- `releasing-souxmar` — release flow with signing
- `adding-agent-tool` — agent tool surface
- `reviewing-ui-changes` — design system enforcement
- `validating-solver` — analytical / patch / convergence / cross-solver validation
- `auditing-determinism` — cross-platform reproducibility
- `triaging-plugin-crash` — `SOUXMAR_E_PLUGIN_FAULT` triage
- `auditing-mesh-quality` — pre-solve mesh quality audit
- `onboarding-souxmar-contributor` — first PR walkthrough
- `updating-design-tokens` — token contract + visual regression
- `publishing-plugin-marketplace` — open index and paid marketplace

## License

Apache License 2.0. See [`LICENSE`](LICENSE).
