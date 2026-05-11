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

**First public pre-release — `v0.9.0-beta1` tagged 2026-05-11.** Source, Linux x86_64 CLI tarball, and Python sdist are attached to the [GitHub release](https://github.com/souxmar/souxmar/releases/tag/v0.9.0-beta1). Plugin C ABI is **frozen FINAL at v1.2** ([ADR-0008](docs/adr/0008-abi-v1-final-freeze.md)). The desktop app, hosted services, and OpenFOAM adapter are explicitly **not** in this release — see the Sprint 7 retro ([`docs/retros/sprint-07.md`](docs/retros/sprint-07.md)) and the SPRINT_PLAN.md roadmap for their target sprints.

The ABI v1 soak ran cleanly across Sprint 6 + Sprint 7 with two additive ratchet events — the `reader.*` namespace (v1.0 → v1.1, Sprint 6 push 4) and the mmap-backed buffer protocol (v1.1 → v1.2, Sprint 7 push 3) — and zero breaks. The freeze is permanent for the entire 1.x release series; `scripts/check-frozen-headers.sh` enforces the ratchet on every PR.

Runnable today:

- **CLI**: `souxmar run <pipeline.yaml>`, `souxmar plugin list`, `souxmar agent {list,invoke}` (with `--audit-log`, `--budget-config`, `--yes`), `souxmar-conformance <dir>`, `souxmar-eval <evals-dir>`.
- **Python**: `pip install pysouxmar` → parser, registry, loader, runner, cache, 12 agent tools, audit log, first-class `SessionBudget.on_threshold` callback, `.souxmar/budget.toml` loader.
- **Plugin SDK**: **frozen-final C ABI v1.2** across six capability namespaces (`reader.*`, `mesher.*`, `solver.*`, `writer.*`, `postproc.*`, plus the bulk-buffer ingest path); `souxmar_add_plugin` CMake macro; conformance suite + CI lockdown gate; structured `ManifestRejection` codes for tooling.
- **Nine in-tree reference plugins**: hello-mesher, grid-mesher, hello-writer, vtu-writer, heat-solver, elasticity-stub, scalar-magnitude, mesh-quality, stl-reader.
- **Three opt-in external adapters**: `occt-reader` (`-DSOUXMAR_WITH_OPENCASCADE=ON`, STEP / IGES via OpenCASCADE), `gmsh-mesher` (`-DSOUXMAR_WITH_GMSH=ON`, tetrahedralisation via Gmsh), `fenicsx-solver` (`-DSOUXMAR_WITH_FENICSX=ON`, FEM Poisson via DOLFINx + FFCx).
- **Three runnable examples**: `examples/cantilever-beam/`, `examples/thermal-fin/`, `examples/stl-cube/`. Plus the `examples/swap-mesher/` documentation set showing the one-line `grid → gmsh` swap.
- **Out-of-core mesh streaming**: mmap-backed `souxmar_buffer_t` v2 (Sprint 7 push 3). `souxmar_mesh_from_buffers` ingests through `_data_const` and routes transparently to heap or mmap — datasets that don't fit in RAM page through the OS cache.
- **Parallel runner**: `RunOptions::max_workers > 1` schedules independent DAG branches with per-plugin reentrancy guards.
- **Agent tool surface v1**: 12 tools, structured audit log, per-project token budget config, threshold callbacks fired at 50% / 80% / 100% of each axis. **30-task agent eval suite** runs nightly.
- **Perf-nightly CI** + bulk-vs-incremental mesh-construction benchmark + heap-vs-mmap buffer benchmark.

Not yet done — deliberately scoped out of `0.9.0-beta1`:

- **No Tauri desktop app yet** (Sprint 8+); CLI and Python only.
- **No CFD solver yet** — the OpenFOAM adapter is design-locked in [ADR-0009](docs/adr/0009-openfoam-process-isolation.md) and ships in Sprint 8 push 1.
- **No production-grade FEM solver yet** — the in-tree `solver.heat.linear` + `solver.elasticity.linear` are demonstrative closed-form solutions; the FEniCSx adapter (opt-in, Poisson only in v1) handles the real case when `-DSOUXMAR_WITH_FENICSX=ON`. Full elasticity + structured BC arrays land via additive minor ratchet in Sprint 8+.
- **No BYOK AI provider client yet** (Anthropic / OpenAI / Ollama). The budget / audit / tool-dispatch plumbing is fully in place; the eval suite scripts the same dispatch path. The first real LLM-driven session lands alongside the desktop app.
- **No signed installers / auto-update** yet — `release.yml` ships source + Linux CLI tarball + Python sdist; macOS notarisation + Windows EV signing + Tauri auto-update manifests land with the desktop app.

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
