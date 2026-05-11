# Sprint Plan

A two-week-cadence plan to ship souxmar from empty repository to v1.0. Twelve sprints (six months) get us to **internal alpha** — the team that built it can use it for real work. Twelve more sprints (six months) get us to **public v1.0**. This document is the rolling plan; the next four sprints are committed, the rest are projected and revised at each retro.

The plan is written for a six-team structure (~10–12 engineers), as defined in [`TEAM_STRUCTURE.md`](TEAM_STRUCTURE.md). Each sprint section lists per-team commitments, cross-team dependencies, and explicit exit criteria. Stories not meeting their exit criteria roll over with an explicit RFC if they are blocking; otherwise they go back to the backlog.

We follow the engineering practices in [`ENGINEERING_PRACTICES.md`](ENGINEERING_PRACTICES.md): perf-as-code, security-as-code, ADRs for non-trivial choices, no merging on red CI.

---

## Conventions

- **Sprints are two weeks.** Demo on Friday week 2; retro Monday week 1 of the next sprint.
- **Stories are sized in T-shirts** (S/M/L/XL). XL is a smell — break it down.
- **Definition of Done** (every story): code merged, tests written, docs updated, perf gates passing, no SEV-2+ regressions in nightly CI for 3 consecutive nights, ADR filed if architectural.
- **A "principal" doesn't ship a PR with no test, no benchmark, no doc.** This is non-negotiable; CI will refuse the merge.
- **Cross-team dependencies** are explicit. A team blocked on another's deliverable for >2 days escalates to the weekly principal sync.

## Risk register (rolling)

| ID    | Risk                                                                 | Likelihood | Impact | Mitigation                                                                                  |
| ----- | -------------------------------------------------------------------- | ---------- | ------ | ------------------------------------------------------------------------------------------- |
| R-001 | OpenCASCADE C++ ABI churn between 7.x minor releases                 | Med        | High   | Pin OCCT version per release; isolate behind adapter; ADR-0004 documents pin policy.        |
| R-002 | Tauri 2 still pre-stable in some edge cases (auto-update on Linux)   | Med        | Med    | Maintain Electron POC as a 2-week bail-out; track Tauri stability monthly.                  |
| R-003 | OpenFOAM is GPL — adapter must be process-isolated to avoid taint    | High       | High   | Adapter execs OpenFOAM as a subprocess, never links it; legal review pre-Sprint 7.          |
| R-004 | Apple notarisation queue stalls block macOS releases                 | Med        | Med    | Bake retry/backoff into release pipeline; have a manual override runbook.                   |
| R-005 | LLM API costs in CI (agent eval suite) blow budget                   | Med        | Med    | Cache responses; run full eval weekly not per-PR; pre-commit per-PR uses local Ollama.      |
| R-006 | Plugin segfault crashes desktop app despite isolation frame          | Low        | High   | Per-plugin out-of-process worker as an escape hatch (post-1.0 if not needed before).        |
| R-007 | Twitter dim palette becomes a trademark issue                        | Low        | Low    | Tokens are similar but not identical to Twitter's; legal sign-off pre-Sprint 1.             |
| R-008 | Single-vendor reliance on Anthropic for default agent quality        | Low        | Med    | OpenAI provider is parity-tested every sprint; Ollama provider proven on Llama-3.x.         |
| R-009 | Determinism across OSes is harder than expected (FP nondeterminism)  | Med        | High   | Determinism gate runs in CI from Sprint 5; deviations file ADRs.                            |
| R-010 | Hiring slower than plan; team understaffed in Sprint 5+              | High       | High   | Plan accommodates +1 sprint slip per uncovered role; contractors as bridge for adapters.    |

## Capacity model

Assumed steady-state team (founding crew, end of Sprint 0):

| Team                | Engineers      | Capacity / sprint (story points) |
| ------------------- | -------------- | -------------------------------- |
| Core / Kernel       | 2 senior       | 16                               |
| Plugin Host         | 1 senior       | 8                                |
| Adapters / Solvers  | 2 (1 sr, 1 mid)| 14                               |
| Desktop / UI        | 2 (1 sr, 1 mid)| 14                               |
| AI Integration      | 2 senior       | 16                               |
| Platform / Release  | 1 senior       | 8                                |
| DX / Docs (shared)  | 1 senior       | 8                                |
| **Total**           | **11**         | **84 pts / sprint**              |

This is the baseline. Sprints 1–4 plan at ~70% of capacity (60 pts) to leave room for build-out friction. Sprints 5+ plan at 85%.

---

## Sprint 0 — Foundation (pre-sprint, ~2 weeks)

**Goal:** the team can ship code on day 1 of Sprint 1.

| Workstream     | Story                                                                        | Owner            | Size |
| -------------- | ---------------------------------------------------------------------------- | ---------------- | ---- |
| Repo           | GitHub org + monorepo + branch protections + DCO bot + CODEOWNERS            | Platform         | M    |
| CI             | Matrix (Linux/macOS/Windows × x86_64/arm64), CMake bootstrap, caching        | Platform         | L    |
| Build          | vcpkg manifest with pinned OCCT, PETSc, VTK, Eigen, pybind11; reproducible   | Platform         | L    |
| Legal          | Trademark filing started; OpenFOAM GPL-process-isolation ADR; Twitter palette legal review | Founders | M    |
| Docs           | All Phase-0 docs merged (DONE), `CONTRIBUTING.md`, `CODE_OF_CONDUCT.md`, RFC template | DX        | S    |
| Skills         | `.claude/skills/` populated with souxmar-specific skills                     | DX               | M    |
| Hiring         | Hire to 11 engineers; define on-call rota; set up Slack + Linear              | Founders         | XL   |
| Process        | First weekly principal sync calendared; ADR repo live; risk register live    | Founders         | S    |
| Compliance     | Apple Developer team enrolled; Windows EV cert ordered; GPG release key      | Platform         | L    |

**Exit criteria:**
- A new engineer can `git clone && cmake -B build && cmake --build build && ctest` and get a green build on all 3 OSes.
- DCO sign-off, branch protection, mandatory review enforced.
- Trademark application filed.
- All Phase-0 docs accepted.
- 11 engineers on payroll, teams formed.

---

## Sprint 1 — Core skeleton + design-system foundation

**Theme:** types and tokens. Get the data model and the visual system both bootstrapped so every subsequent sprint has stable foundations.

| Team       | Story                                                                      | Size |
| ---------- | -------------------------------------------------------------------------- | ---- |
| Core       | `Geometry`, `Topology` types as plain structs; opaque OCCT handle wrapper; constructors + accessors + unit tests | L |
| Core       | `Mesh` and `Field` types; mixed-element support, tag inheritance machinery | L |
| Plugin Host| Manifest parser (TOML); discovery walker for SOUXMAR_PLUGIN_PATH           | M |
| Adapters   | OpenCASCADE wrapper: STEP read producing a `Geometry`; tag preservation    | L |
| Desktop    | Tauri 2 hello-world on all 3 OSes; React + Vite scaffold; design tokens (`tokens.css`) | M |
| Desktop    | Component primitives v0: Button, Input, Tabs, ScrollArea (Radix + Tailwind, dim theme) | M |
| AI         | `IAIProvider` interface + Anthropic skeleton (no streaming yet); OS keychain on macOS via `SecItem*` | L |
| Platform   | ASAN/UBSAN/TSAN modes wired into CMake + CI nightly                        | M |
| DX         | First example project structure + `souxmar new` skeleton                   | S |

**Cross-team deps:** Adapters needs Core's `Geometry` interface (deliver mid-sprint via header-only stub).

**Exit criteria:**
- `libsouxmar-core` builds on all 3 OSes with >85% line coverage.
- A STEP file imports producing a `Geometry` with named tags preserved.
- Tauri shell launches a window showing a "souxmar" splash with dim theme on all 3 OSes.
- Anthropic key saves to and loads from macOS keychain.

**Risks raised:** R-001 (OCCT pin discipline), R-007 (palette legal — should clear by S1).

---

## Sprint 2 — Plugin host + first plugin

**Theme:** prove the extension model works end-to-end with a no-op plugin before we trust it with a real one.

| Team       | Story                                                                      | Size |
| ---------- | -------------------------------------------------------------------------- | ---- |
| Core       | `LinearAlgebra` opaque handles; PETSc backend (default), Eigen backend (small problems) | L |
| Plugin Host| `souxmar_plugin_register_v1` ABI lock; capability registry; signal/SEH error frame | L |
| Plugin Host| `souxmar_add_plugin` CMake macro; out-of-tree "hello-mesher" sample        | M |
| Adapters   | Native mesher v0 (uniform tetra fill, correctness only); registers via plugin ABI | L |
| Desktop    | Chat panel UI scaffold (no provider wired); message blocks; tool-card primitive | M |
| Desktop    | Viewport: Three.js scene, render an axes gizmo, camera controls            | M |
| AI         | Anthropic streaming completion + prompt caching; Windows DPAPI + Linux libsecret keychain | L |
| Platform   | Apple notarisation infra; Windows EV signing infra; GPG signing for tarballs | L |
| DX         | Plugin SDK header reference page; "write your first plugin" tutorial v0   | M |

**Cross-team deps:** Plugin Host blocks Adapters (native mesher); deliver ABI headers by Wednesday Week 1.

**Exit criteria:**
- Out-of-tree "hello-mesher" plugin compiles against installed SDK, ships as `.so`/`.dylib`/`.dll`, loads in `souxmar plugin list`.
- Native mesher produces a valid tet mesh of a unit cube.
- Anthropic streaming works in headless test (no UI yet).
- Plugin segfault is caught and reported, not fatal.

**ADRs:** ADR-0005 (PETSc as default LinearAlgebra backend).

---

## Sprint 3 — First end-to-end pipeline

**Theme:** cantilever beam. STEP → mesh → solve → VTU. Everything is a plugin, the orchestrator drives.

| Team       | Story                                                                      | Size |
| ---------- | -------------------------------------------------------------------------- | ---- |
| Core       | `Pipeline` orchestrator: DAG resolution, content-addressed cache, sequential runner | L |
| Plugin Host| Capability dispatch by name; threading-model enforcement                   | M |
| Adapters   | Linear elasticity solver (PETSc direct solve, dirichlet+neumann BCs); registers as `solver.elasticity.linear` plugin | XL |
| Adapters   | VTK writer (`.vtu`); registers as `writer.vtu` plugin                      | M |
| Desktop    | Pipeline timeline strip; stage status (pending/running/done/error/cached) | M |
| Desktop    | Inspector panel scaffold; entity selection from viewport                   | M |
| AI         | OpenAI provider parity with Anthropic; tool-call schema design (frozen-candidate v1) | L |
| Platform   | Cross-OS determinism harness: same pipeline produces same VTU hash on all 3 OSes | M |
| DX         | First example project: `examples/cantilever-beam/` runnable end-to-end via CLI | M |

**Exit criteria:**
- `souxmar run examples/cantilever-beam/cantilever.souxmar.yaml` produces a `.vtu` that opens in ParaView and shows visibly correct stress.
- The same pipeline produces byte-identical `.vtu` on Linux, macOS, and Windows.
- Determinism CI gate live and passing.

**Demo at retro:** load the cantilever beam result in ParaView, walk through the audit trail of plugin invocations.

---

## Sprint 4 — Python bindings + parallel runner

**Theme:** open the second front door (Python) and make the orchestrator parallel.

| Team       | Story                                                                      | Size |
| ---------- | -------------------------------------------------------------------------- | ---- |
| Core       | Pipeline parallel runner (independent DAG branches in parallel via thread pool) | L |
| Plugin Host| Reentrancy enforcement based on manifest declaration                       | S |
| Python     | pybind11 module `pysouxmar`: data model + pipeline runner exposure         | XL |
| Python     | `@sx.plugin.mesher` / `@sx.plugin.solver` decorator (Python plugin shim)   | M |
| Desktop    | Tauri commands: load project, run pipeline, stream progress to UI          | L |
| Desktop    | Viewport: render `.vtu` from disk, basic field colour ramp using `--viz-*` tokens | L |
| AI         | Tool dispatcher v1 (5 tools: `read_geometry_summary`, `mesh`, `set_bc`, `solve`, `screenshot_viewport`) | L |
| AI         | Confirmation policy machinery (`auto`/`confirm-once`/`confirm-always`)     | M |
| Platform   | `pysouxmar` wheels build matrix; PyPI dry-run                              | M |
| DX         | Python tutorial: 20-line cantilever-beam script                            | S |

**Exit criteria:**
- `pip install pysouxmar` (from local index) and run a 20-line analysis.
- Desktop app loads a `.vtu` and renders it with field colour mapping.
- AI agent (called from CLI in headless mode) completes a 5-step tool sequence ending in a screenshot.
- Pipeline parallel runner shows ≥1.6× speed-up on a 2-branch DAG vs sequential.

---

## Sprint 5 — Hardening pass 1; ABI freeze candidate

**Theme:** stop adding, start tightening. The ABI candidate goes into a 2-sprint soak.

| Team       | Story                                                                      | Size |
| ---------- | -------------------------------------------------------------------------- | ---- |
| Core       | ASAN + TSAN clean across all unit + integration tests on Linux             | L |
| Core       | Memory-mapped buffer type for large mesh transfer across ABI               | L |
| Plugin Host| Conformance suite v1 (10 conformance checks, runnable as `souxmar-conformance`) | L |
| Adapters   | Heat-conduction solver (`solver.heat.linear`); demonstrates `Field` time-series | M |
| Desktop    | Chat panel wired to live AI providers; tool-card streaming UI              | L |
| Desktop    | Command palette (`⌘K`) with project / file / plugin / tool search          | M |
| AI         | Audit log (`.souxmar/chat/audit.log`); per-session token budget warnings   | M |
| AI         | Tool count to 8; add `query_field`, `compute_field`, `propose_pipeline`    | M |
| Platform   | Nightly perf-regression CI; baseline established                           | L |
| DX         | Plugin authoring tutorial published; second example project (thermal-fin) | M |

**Exit criteria:**
- `include/souxmar-c/` declared **frozen-candidate v1**. Two-sprint freeze period begins.
- Plugin conformance suite runs against all 3 in-tree plugins, all pass.
- ASAN/TSAN clean nightly for at least 3 consecutive nights.
- Desktop chat completes a real cantilever workflow end-to-end with the user supplying their own Anthropic key.

**ADRs:** ADR-0006 (memory-mapped large-buffer protocol).

---

## Sprint 6 — Gmsh adapter + design-system completion

**Theme:** a second mesher proves the abstraction; a complete UI proves we can ship pixels.

| Team       | Story                                                                      | Size |
| ---------- | -------------------------------------------------------------------------- | ---- |
| Adapters   | Gmsh adapter: embed Gmsh as library; expose as `mesher.tetra.gmsh` plugin  | XL |
| Adapters   | Mesh quality metrics module (Jacobian, aspect ratio, dihedral)             | M |
| Plugin Host| Plugin manifest schema validation; reject malformed manifests at discovery | M |
| Desktop    | Design system at 100% component coverage (tables, dialogs, toasts, popovers, tooltips, context menus) | L |
| Desktop    | Lights-out theme variant; theme switcher in settings                       | M |
| Desktop    | Inspector tabs: entity properties, field statistics, mesh quality          | M |
| AI         | Tool count to 12; `set_material`, `apply_pipeline_diff`, `list_plugins`, `export_results` | M |
| AI         | Cost meter UI; per-project token budget configurable                       | M |
| Platform   | Auto-update infra: signed Tauri update manifests; CDN published from CI    | L |
| DX         | Storybook live; component gallery + accessibility checks per component     | M |

**Exit criteria:**
- A user can swap `mesher.tetra.native` for `mesher.tetra.gmsh` in pipeline YAML with no other changes; same result format.
- Design system passes WCAG AA (Storybook a11y addon green).
- Lights-out theme available, switchable per project.

---

## Sprint 7 — FEniCSx adapter + first beta candidate

**Theme:** the second solver. ABI freeze period ends — declare the freeze final or revise.

| Team       | Story                                                                      | Size |
| ---------- | -------------------------------------------------------------------------- | ---- |
| Adapters   | FEniCSx adapter: convert souxmar Mesh + BCs to DOLFINx; consume DOLFINx solution into `Field` | XL |
| Plugin Host| **ABI v1 frozen final** — bake-off review, lock symbol set, version-gate the SDK | M |
| Core       | Out-of-core mesh streaming (mmap-backed buffer flow)                       | L |
| Desktop    | Pipeline editor: drag-to-add stages, edit stage inputs in inspector        | L |
| AI         | Anthropic prompt cache discipline: keep static-prompt cache hit > 70% in agent eval suite | M |
| AI         | Agent eval suite v1: 30 canonical engineering tasks with pass/fail criteria | L |
| Platform   | Public beta (`v0.9.0-beta1`) artefact pipeline: signed installers, GitHub release | M |
| DX         | OpenFOAM legal/process-isolation ADR finalised (precondition for Sprint 8) | S |

**Exit criteria:**
- ABI v1 frozen and published; SDK headers tagged.
- A pipeline using `solver.elasticity.fenicsx` produces results within 1e-6 relative of `solver.elasticity.linear` on the cantilever benchmark.
- Out-of-core test: 80M-cell mesh loads and renders without RAM blow-up.

**ADRs:** ADR-0007 (out-of-core buffer protocol), ADR-0008 (ABI v1 final lock).

---

## Sprint 8 — OpenFOAM adapter + Blender importer

**Theme:** CFD enters; concept geometry from Blender enters.

| Team       | Story                                                                      | Size |
| ---------- | -------------------------------------------------------------------------- | ---- |
| Adapters   | OpenFOAM adapter: case-directory generator from souxmar Mesh + BCs; subprocess driver for `simpleFoam`/`pimpleFoam`; result read-back via `foamToVTK` | XL |
| Adapters   | Blender `.blend` reader: extract triangulated meshes, named collections become tags | L |
| Desktop    | CFD-specific viewport overlays (streamlines, vector glyphs, slice planes)  | L |
| AI         | CFD-aware tool extensions: `apply_inlet`, `apply_wall`, `apply_outlet`     | M |
| AI         | Tool count to ~18; freeze tool-contract v1 candidate                       | M |
| Platform   | Process-isolation harness for GPL adapter; subprocess crash isolation      | M |
| DX         | Third example project: pipe-bend CFD via OpenFOAM                          | M |

**Exit criteria:**
- CFD case (laminar pipe bend) runs end-to-end via `souxmar run`, results render in viewport.
- Blender `.blend` import preserves named-collection → souxmar tag mapping.
- Legal sign-off filed: OpenFOAM is invoked exclusively as a subprocess; no GPL code linked into souxmar binaries.

**Risks closed:** R-003.

---

## Sprint 9 — Performance + scale

**Theme:** budget enforcement. Everything that has been "fast enough" must now be measurably under budget.

| Team       | Story                                                                      | Size |
| ---------- | -------------------------------------------------------------------------- | ---- |
| Core       | Assembly hot-path SIMD pass; PETSc vec/mat handle pooling                  | L |
| Plugin Host| Per-plugin heap accounting; report leaks via instrumentation               | M |
| Desktop    | WebGPU rendering path (where supported); viewport target 60 fps on 5M-cell mesh | L |
| Desktop    | Cold launch budget enforcement (CI gate at 1.5 s); first chat token < 800 ms | M |
| AI         | Latency budget enforcement: p95 first-token < 800 ms BYOK direct           | M |
| Platform   | Benchmark suite gates merges (perf regression > 5 % blocks)                | L |
| DX         | Benchmark dashboard published per release                                  | S |

**Exit criteria:**
- All performance budgets in `DESKTOP_APP.md` met on a reference machine (M2 / Ryzen 7).
- Perf regression gate live; the team has dealt with 1+ regression block in CI.

---

## Sprint 10 — Distribution + plugin marketplace v0

**Theme:** make the project shippable, make the ecosystem joinable.

| Team       | Story                                                                      | Size |
| ---------- | -------------------------------------------------------------------------- | ---- |
| Platform   | Auto-updater across all 3 OSes; signed manifest pipeline; rollback protocol | XL |
| Platform   | Apple notarisation automation (handle queue stalls, retry/backoff)         | M |
| Plugin     | Plugin index data model; `souxmar plugin search` against the static index  | M |
| Plugin     | Index publication workflow: PR-based, with conformance status surfaced     | M |
| AI         | Local-Ollama provider verified across Llama-3.x, Qwen-2.x, Mistral         | L |
| Desktop    | Onboarding flow: first-launch wizard, BYOK key entry, sample project       | L |
| DX         | Fourth example: mesh-algorithm comparison study (uses two meshers)         | M |

**Exit criteria:**
- A user can install the desktop app, hit "auto-update," and roll forward / roll back cleanly.
- Plugin index lists 3+ in-tree plugins + at least 1 third-party plugin.
- First-launch wizard is the only thing standing between download and a working analysis.

---

## Sprint 11 — Internal alpha; dogfooding

**Theme:** the team uses the product for the team's actual work. We find what we built wrong by trying to use it.

| Team       | Story                                                                      | Size |
| ---------- | -------------------------------------------------------------------------- | ---- |
| All        | Dogfood week 1: every engineer runs a real analysis using only the desktop app + chat | XL |
| All        | Triage backlog from dogfood; bug-fix sprint for the rest of S11            | XL |
| AI         | Agent eval suite v2 (60 tasks); regression gate at 90 % pass               | L |
| Desktop    | Polish pass: animations, error states, empty states, loading states        | L |

**Exit criteria:**
- Every team member has produced at least one real-world analysis result through the app, fully via chat.
- All P0/P1 bugs from dogfood closed.
- Agent eval suite passes 90 %.

**Outcome:** project is at **internal alpha**. We could give it to a friendly external team. We do not yet ship publicly.

---

## Sprint 12 — Public alpha (`v0.9.0`)

**Theme:** ship the alpha. Be honest about its rough edges; collect ground-truth feedback.

| Team       | Story                                                                      | Size |
| ---------- | -------------------------------------------------------------------------- | ---- |
| Platform   | First public release `v0.9.0`; signed installers on download page          | M |
| DX         | Public docs site (Vitepress / Docusaurus); API reference auto-generated    | XL |
| DX         | Discord / GitHub Discussions live; triage rotation set up                  | M |
| All        | Bug-bash on candidate build, fix any P0/P1                                 | L |
| Founders   | Launch comms (HN, blog, mailing list, partner outreach)                    | M |

**Exit criteria:**
- `v0.9.0` tagged and downloadable on all 3 OSes.
- Docs site live with at least: install guide, first-pipeline tutorial, plugin authoring guide, agent reference, business-model page.
- Triage rotation handling external bug reports within 48 h.

---

## Sprints 13–24 — Beta to v1.0 (sketched)

Detailed sprint breakdowns produced at S12 retro. Themes:

| Sprint | Theme                                                                                          |
| ------ | ---------------------------------------------------------------------------------------------- |
| S13    | Feedback triage from public alpha; bug fixes; perf regressions caught by external workloads.   |
| S14    | Managed-AI proxy MVP (Pro tier infrastructure); billing integration POC.                       |
| S15    | Cloud sync MVP for Pro tier; encrypted-at-rest, end-to-end if Enterprise.                      |
| S16    | Plugin marketplace v1: paid-plugin hosting, Stripe integration, license-key flow.              |
| S17    | Hosted compute offload POC (Pro tier): submit a `solve` stage to managed HPC, stream results.  |
| S18    | Multi-window / multi-project polish; project-level isolation of AI providers.                  |
| S19    | Lightweight in-app geometry edits (reposition, suppress, parameter tweaks). Not parametric modelling. |
| S20    | SSO (SAML/OIDC) for Team tier; SCIM for Enterprise.                                            |
| S21    | Security audit + pen-test; SOC 2 readiness for managed services; bug-bounty programme.         |
| S22    | Public beta (`v0.99-beta`); marketplace soft-open; first paid customers onboard.               |
| S23    | Release-candidate hardening: ABI bake, full eval suite, tooling lock-down.                     |
| S24    | **`v1.0.0`** — ABI v1 final; agent tool contract v1 final; on-disk pipeline format v1 final.   |

---

## Cross-cutting commitments (every sprint)

- **Perf gate:** no merge that regresses any tracked benchmark by >5 %.
- **Determinism gate:** the cantilever golden runs on every PR; deviation fails the build.
- **Security gate:** new dependencies require an ADR (or a one-line "covered by ADR-NNNN") and a license check.
- **Docs gate:** new public APIs, plugin types, or tools require docs in the same PR.
- **Conformance gate:** in-tree reference plugins must pass `souxmar-conformance` before merge.

## What we are explicitly *not* doing in the first 24 sprints

Captured here so we do not relitigate. RFC required to revive any of these inside this window.

- Custom CAD modelling (full parametric — light edits only post-S19).
- GPU-accelerated solver in core (post-1.0; plugin route open).
- Distributed compute in the open core (Pro/managed only).
- A web app or hosted SaaS desktop.
- Anything in the multiphysics-coupling space (post-1.0).
- A second plugin language ABI (Python decorator shim covers prototyping).

## Retro practice

- Every retro produces three artefacts: **what to keep / what to fix / one ADR-worthy decision surfaced**.
- Risk register reviewed every retro; closed/new risks called out explicitly.
- Capacity adjusted ±10 pts based on actual closure rate of the previous two sprints.
- Backlog grooming happens *during* the retro for the next sprint, not in a separate meeting.
