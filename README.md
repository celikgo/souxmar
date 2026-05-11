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

**Third public pre-release — `v0.9.0-beta3` tagged 2026-05-11.** Source, Linux x86_64 CLI tarball, and Python sdist are attached to the [GitHub release](https://github.com/souxmar/souxmar/releases/tag/v0.9.0-beta3). Plugin C ABI is **frozen FINAL at v1.3** ([ADR-0008](docs/adr/0008-abi-v1-final-freeze.md) for the freeze, [ADR-0012](docs/adr/0012-per-face-tag-c-abi-ratchet.md) for the per-face-tag minor ratchet — Sprint 9 push 2). Agent tool contract is **frozen FINAL at v1** with 18 tools ([ADR-0011](docs/adr/0011-tool-contract-v1-final-freeze.md), superseding the freeze-candidate ADR-0010 — Sprint 9 push 1). The desktop app and hosted services are still explicitly **not** in this release — see the Sprint 9 retro ([`docs/retros/sprint-09.md`](docs/retros/sprint-09.md)) and the SPRINT_PLAN.md roadmap.

What changed since beta2 (Sprint 9 in full):

- **Tool contract v1 frozen FINAL** ([ADR-0011](docs/adr/0011-tool-contract-v1-final-freeze.md), Sprint 9 push 1). The candidate-period soak from ADR-0010 cleared every gate; 18 tools locked across categories Read / Mesh / BC / CFD / Material / Solve / Field / Pipeline / Discovery / Export / UI; `scripts/check-tool-contract.sh` flipped blocking-by-default; new `tool-contract-v1-lockdown` CI job mirrors the ABI lockdown.
- **ABI v1.2 → v1.3 ratchet** ([ADR-0012](docs/adr/0012-per-face-tag-c-abi-ratchet.md), Sprint 9 push 2). Three new declarations in `souxmar-c/mesh.h` for per-face tags (`souxmar_mesh_cell_face_count`, `souxmar_mesh_face_tag`, `souxmar_mesh_set_face_tag`) + `SOUXMAR_FACE_UNTAGGED`. Sparse-map storage means untagged meshes pay zero bytes for the feature; tagged meshes cost O(N) for N tagged faces.
- **Per-patch BC routing in `openfoam-solver`** (Sprint 9 push 3). Boundary faces grouped by `souxmar_mesh_face_tag`; one polyMesh patch per matched BC; matching `0/U` + `0/p` boundaryField entries. Untagged faces fall through to the legacy "walls" patch — non-breaking.
- **Mixed-element polyMesh translator** (Sprint 9 push 4). Tet4 + Hex8 + Prism6 + Pyramid5 first-class, including arbitrarily-mixed meshes. Higher-order variants rejected with a clean diagnostic.
- **`pipe-bend.obj` fixture + `usemtl` preservation in `obj-reader`** (Sprint 9 push 5). The pipe-bend example now reads a real 12-vertex L-shaped duct with `inlet` / `walls` / `outlet` groups; obj-reader maps each unique `usemtl` to a sequential integer per-cell tag.
- **Perf-regression gate hardened to the ENGINEERING_PRACTICES.md target** (Sprint 9 pushes 6–8). Threshold 10 % → 5 %; full suite (5 binaries) runs per-PR; new `compare.py` directory mode; new `bench_face_tag` + `bench_plugin_dispatch` (the < 20 µs warm budget) + `bench_heap_accountant` (the < 1 µs always-on accountant). Per-release HTML dashboard via `tools/perf-compare/dashboard.py` ships with every artifact bundle.
- **Per-plugin heap accounting in the audit log** (Sprint 9 push 9). New `souxmar::plugin::HeapAccountant` brackets every tool dispatch; `AuditLog::Entry` carries `heap_bytes_delta` on supported platforms (Linux + glibc ≥ 2.33).
- **AI BYOK latency budget infrastructure in the eval suite** (Sprint 9 push 10). `souxmar-eval --latency-output` writes per-tool + aggregate p50/p95/p99/mean/max JSON; `--max-p95-ms` gate gives the perf workflow a distinct exit code for latency regressions.

The ABI v1 soak that ran across Sprints 5–7 picked up its third additive minor ratchet in Sprint 9 push 2 (per-face tags, v1.2 → v1.3) — handled cleanly via the `Ratchet: additive minor surface (ADR-0008)` marker, no breaking changes. The freeze is permanent for the entire 1.x release series; `scripts/check-frozen-headers.sh` enforces the ABI ratchet and `scripts/check-tool-contract.sh` enforces the matching agent-tool-contract ratchet (Sprint 9 push 1 flipped that gate blocking-by-default). Both surfaces are under blocking lockdown on every PR.

Runnable today:

- **CLI**: `souxmar run <pipeline.yaml>`, `souxmar plugin {list,search,validate-index}` (Sprint 10 pushes 2 + 3 — `search` queries `docs/plugin-index.toml`; `validate-index` is the gate the `plugin-index` CI workflow runs on every PR touching the index), `souxmar agent {list,invoke}` (with `--audit-log`, `--budget-config`, `--yes`), `souxmar-conformance <dir>`, `souxmar-eval <evals-dir>` (with `--latency-output` and `--max-p95-ms`).
- **Python**: `pip install pysouxmar` → parser, registry, loader, runner, cache, **18 agent tools**, audit log, first-class `SessionBudget.on_threshold` callback, `.souxmar/budget.toml` loader.
- **Plugin SDK**: **frozen-final C ABI v1.3** across six capability namespaces (`reader.*`, `mesher.*`, `solver.*`, `writer.*`, `postproc.*`, plus the bulk-buffer ingest path), now with **per-face tags** on `souxmar_mesh_*` (ADR-0012); `souxmar_add_plugin` CMake macro; conformance suite + CI lockdown gate; host-side **subprocess harness** for plugins that drive external binaries.
- **Eleven in-tree reference plugins** (always-on): hello-mesher, grid-mesher, hello-writer, vtu-writer, heat-solver, elasticity-stub, cfd-stub, scalar-magnitude, mesh-quality, stl-reader, obj-reader.
- **Five opt-in external adapters**: `occt-reader` (`-DSOUXMAR_WITH_OPENCASCADE=ON`, STEP / IGES), `gmsh-mesher` (`-DSOUXMAR_WITH_GMSH=ON`), `fenicsx-solver` (`-DSOUXMAR_WITH_FENICSX=ON`, FEM Poisson), `openfoam-solver` (`-DSOUXMAR_WITH_OPENFOAM=ON`, three CFD capabilities), `blender-reader` (`-DSOUXMAR_WITH_BLENDER=ON`, `.blend` import).
- **Four runnable examples**: `examples/cantilever-beam/`, `examples/thermal-fin/`, `examples/stl-cube/`, `examples/pipe-bend/`. Plus the `examples/swap-mesher/` documentation set showing the one-line `grid → gmsh` swap.
- **Out-of-core mesh streaming**: mmap-backed `souxmar_buffer_t` v2. `souxmar_mesh_from_buffers` routes transparently to heap or mmap.
- **Parallel runner**: `RunOptions::max_workers > 1` schedules independent DAG branches with per-plugin reentrancy guards.
- **Agent tool surface v1 (frozen final, ADR-0011)**: 18 tools across categories Read / Mesh / BC / CFD / Material / Solve / Field / Pipeline / Discovery / Export / UI. Structured audit log, per-project token budget config. **30-task agent eval suite** runs nightly; per-provider scores (Anthropic 94 %, OpenAI 92 %, Ollama 89 %) cleared the freeze gate.
- **Perf-regression CI at 5 % per-PR** ([`ENGINEERING_PRACTICES.md`](docs/ENGINEERING_PRACTICES.md) § Performance budgets matched). Five benchmark binaries: `bench_mesh_construction`, `bench_mmap_buffer`, `bench_face_tag`, `bench_plugin_dispatch` (< 20 µs warm dispatch budget), `bench_heap_accountant` (< 1 µs always-on accounting). Self-contained HTML dashboard generated per release.
- **Eval suite v1 latency capture**: `souxmar-eval --latency-output` emits per-tool + aggregate p50/p95/p99/mean/max JSON; `--max-p95-ms` gate carries the future BYOK first-token budget (< 800 ms p95 per ENGINEERING_PRACTICES.md).
- **Audit log carries heap deltas** on Linux + glibc ≥ 2.33 — per-tool `heap_bytes_delta` field surfaces leak indicators and per-call cost in the agent UI.

Not yet done — deliberately scoped out of `0.9.0-beta3`:

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
