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

Pre-alpha — Sprint 5 closed (2026-05-11). **Plugin C ABI v1 is in frozen-candidate state** with formal freeze targeted for 2026-06-08; see [ADR-0007](docs/adr/0007-abi-v1-freeze-candidate.md) for the soak rules. Plugin authors can build against the candidate now — additive minor surfaces are forward-compatible by construction.

Runnable today:

- **CLI**: `souxmar run <pipeline.yaml>`, `souxmar plugin list`, `souxmar agent {list,invoke}`, `souxmar-conformance <dir>`.
- **Python**: `pip install ./bindings/python` → `pysouxmar` (parser, registry, loader, runner, cache, agent tools, audit log).
- **Plugin SDK**: stable C ABI v1 across five capability namespaces (`mesher.*`, `solver.*`, `writer.*`, `postproc.*`, plus the bulk-buffer ingest path); CMake `souxmar_add_plugin` macro; conformance suite gating the index.
- **Five in-tree reference plugins**: hello-mesher, hello-writer, vtu-writer (ParaView-readable), heat-solver (time-series Field), scalar-magnitude (postproc).
- **Two runnable examples**: `examples/cantilever-beam/`, `examples/thermal-fin/`.
- **Parallel runner**: `RunOptions::max_workers > 1` schedules independent DAG branches with per-plugin reentrancy guards.
- **Agent tool surface v1**: 8 tools, structured audit log, session budget plumbing.
- **Perf-nightly CI** + bulk-vs-incremental mesh-construction benchmark.

Not yet done — deliberately scoped out of Phase 0:

- No Tauri desktop app yet (Sprint 8+); CLI and Python only.
- No production-grade adapters yet: the in-tree mesher / solver / writer are reference plugins, not OpenCASCADE / Gmsh / FEniCSx. The OCCT geometry import + native tetrahedral mesher land in Sprint 6.
- The agent tool surface runs offline; the BYOK provider client + token-counting `SessionBudget` first-class callbacks land alongside the desktop app.

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
