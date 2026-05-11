# Team Structure

Six teams, ~11 engineers at founding crew, sized to the architecture. Org is intentionally flat — everyone reports to engineering leadership; teams self-organise around their workstream. This document defines team boundaries, ownership, the cross-team coordination model, and the hiring sequence.

This is a living document. Team names and ownership lines are stable; headcount adjusts at quarterly planning.

## Org chart

```
                              Founders
                         (CEO, CTO, Head of Eng)
                                  |
            +----------+----------+----------+----------+----------+
            |          |          |          |          |          |
          Core /     Plugin    Adapters /  Desktop /     AI      Platform /
         Kernel      Host       Solvers       UI    Integration   Release
           (2)        (1)         (2)        (2)        (2)         (1)
                                  |
                          + DX / Docs (1, shared horizontal)
```

Plus one rotating **on-call** position drawn from any team (post-public-alpha).

## Team responsibilities

### Core / Kernel (2 senior)

Owns the data model and the in-process orchestration. Everything in `src/core/` and `src/pipeline/`.

- `Geometry`, `Topology`, `Mesh`, `Field`, `LinearAlgebra` types.
- Pipeline DAG, content-addressed cache, parallel runner, out-of-core streaming.
- Memory model (large-buffer protocol, mmap discipline).
- The C++ side of correctness: invariants, allocators, lifetime contracts.

Does *not* own: I/O, plugin discovery, anything user-facing.

### Plugin Host (1 senior)

Owns the C ABI and everything it implies. Smallest team; widest blast radius. Every change here is a contract change with every plugin author in the world.

- `include/souxmar-c/` and `src/plugin-host/`.
- ABI versioning, capability registry, manifest schema.
- Plugin discovery, loading, lifecycle, error isolation (signal/SEH frames).
- Conformance suite (`souxmar-conformance`).
- The threading-model contract (`reentrant` / `single-threaded` / `internal-parallel`).

A change in this team's scope often requires an RFC. This is by design.

### Adapters / Solvers (2: 1 senior, 1 mid)

Wraps third-party tools and ships in-tree reference plugins. The "make stuff work" team.

- `src/adapters/` — OpenCASCADE, Gmsh, FEniCSx, OpenFOAM, Blender, VTK.
- `src/plugins/` — native mesher, linear elasticity, heat conduction.
- Process-isolation harness for GPL-licensed adapters (OpenFOAM).
- Result-validation suite (analytical-solution comparisons, golden tests).

This team owns the largest dependency footprint. They drive the vcpkg manifest in coordination with Platform.

### Desktop / UI (2: 1 senior, 1 mid)

Owns everything a user sees. The Tauri shell, the React frontend, the design system.

- `src/desktop/src-tauri/` — Rust shell, Tauri commands, FFI.
- `src/desktop/src/` — React + TypeScript, viewport, chat panel UI, inspector, pipeline editor.
- `src/desktop/ui/` — design tokens, component primitives, themes.
- Storybook, accessibility checks, visual regression tests.
- Performance budgets for the UI surface (cold launch, frame time, viewport interactivity).

The frontend half (React) is mostly the mid; the Tauri/Rust half is mostly the senior. Both pair on viewport work.

### AI Integration (2 senior)

Owns `src/ai/` — providers, agent, keychain, tools, audit log.

- `IAIProvider` abstraction; Anthropic, OpenAI, Ollama implementations.
- BYOK key storage in OS keychain (per-OS).
- Agent loop: prompt construction, tool dispatch, confirmation policy, streaming.
- Tool surface (currently ~18 tools; v1-frozen at S8).
- Audit log; cost meter; agent eval suite.
- Managed-AI proxy (Pro service, post-S14).

Two senior engineers because this is the load-bearing differentiator and the surface most exposed to LLM-API churn.

### Platform / Release (1 senior)

Owns the build, the CI, and the release process. Small but critical team — when this team breaks, no one ships.

- CMake, vcpkg manifest, cross-compilation.
- CI matrix (Linux/macOS/Windows × x86_64/arm64) and CI cost.
- Code signing (Apple notarisation, Windows EV, Linux GPG), auto-updater.
- Release tooling, version bumping, changelog generation.
- Performance regression CI (in coordination with the team that owns the benchmark).
- Determinism gate.

Becomes a 2-person team by S10 when auto-update + signing complexity demands it.

### DX / Docs (1 senior, shared horizontal)

Crosses every team. The "make using the project not painful" person.

- Public docs site, API reference, tutorials, examples (`examples/`).
- `CONTRIBUTING.md`, RFC template, plugin authoring guide.
- Storybook curation alongside Desktop team.
- Plugin index curation alongside Plugin Host team.
- Triages external bug reports (post-public-alpha) and routes to the right team.

DX is a single role to start; it scales to 2 by S15 when external community grows.

## Ownership (CODEOWNERS reflection)

Every directory in the repo has a single owning team. CODEOWNERS enforces the team owner must approve any PR touching their area. Cross-cutting PRs need approvals from each affected team.

| Path                              | Owner               |
| --------------------------------- | ------------------- |
| `include/souxmar-c/`              | Plugin Host         |
| `src/core/`, `src/pipeline/`      | Core                |
| `src/plugin-host/`                | Plugin Host         |
| `src/adapters/*`                  | Adapters            |
| `src/plugins/*`                   | Adapters            |
| `src/cli/`                        | Core (joint with Platform) |
| `src/bindings/python/`            | Core                |
| `src/ai/`                         | AI                  |
| `src/desktop/`                    | Desktop             |
| `cmake/`, `.github/`, `vcpkg.json`| Platform            |
| `docs/`, `examples/`, `README.md` | DX                  |
| `tests/plugin-conformance/`       | Plugin Host         |
| `benchmarks/`                     | Platform (joint with team owning benchmarked code) |
| `.claude/skills/`                 | DX                  |

## RACI for major workstreams

R = Responsible (does the work), A = Accountable (signs off), C = Consulted, I = Informed.

| Workstream                                  | Core | Plugin Host | Adapters | Desktop | AI  | Platform | DX  |
| ------------------------------------------- | ---- | ----------- | -------- | ------- | --- | -------- | --- |
| Data-model design (Geometry/Mesh/Field)     | R/A  | C           | C        | I       | I   | I        | C   |
| C ABI design and evolution                  | C    | R/A         | C        | I       | I   | I        | C   |
| Pipeline orchestrator                       | R/A  | C           | I        | I       | I   | C        | I   |
| New adapter (e.g. OpenFOAM)                 | I    | C           | R/A      | I       | I   | C        | C   |
| New in-tree reference plugin                | I    | C           | R/A      | I       | I   | I        | C   |
| Desktop chat + viewport feature             | I    | I           | I        | R/A     | C   | I        | C   |
| Agent tool (new)                            | I    | C           | C        | C       | R/A | I        | C   |
| BYOK provider integration                   | I    | I           | I        | C       | R/A | I        | I   |
| Build / vcpkg / signing                     | I    | I           | C        | C       | I   | R/A      | I   |
| Release (cut, sign, publish)                | I    | I           | I        | C       | I   | R/A      | C   |
| Performance regression                      | C    | C           | C        | C       | C   | R/A      | I   |
| Determinism gate                            | C    | C           | C        | I       | I   | R/A      | I   |
| Plugin marketplace                          | I    | C           | C        | C       | I   | C        | R/A |
| Public docs site                            | C    | C           | C        | C       | C   | C        | R/A |
| RFC review (Tier 3)                         | A*   | A*          | A*       | A*      | A*  | A*       | C   |
| Incident response (post-launch)             | C    | C           | C        | C       | C   | C        | R/A |

\* Tier 3 RFCs require two maintainer approvals; "A" here means accountable for review when the RFC touches their area.

## Coordination cadence

- **Weekly principal sync** (60 min, Monday): one rep per team. Focus: cross-team blockers, RFC pipeline, risk register, capacity. Not a status meeting.
- **Bi-weekly sprint demo + retro** (90 min, Friday end-of-sprint): every team demos. Retro is structured: keep / change / one ADR surfaced.
- **Weekly RFC office hours** (30 min, Wednesday): authors present in-flight RFCs to maintainers. Drives the 7-day comment-window decision.
- **Monthly architecture review** (90 min, first Thursday): one or two deep dives on areas under stress. Output: ADRs.
- **Quarterly planning** (half day): roadmap revisit, capacity reset, hiring plan reset.

We do *not* run daily standups. They cost too much for a remote-friendly senior team. Async daily updates in each team's Slack channel cover the same ground at a fraction of the time.

## Decision-making model

Three patterns, picked by the change author based on impact:

1. **Just merge it.** Trivial / Tier-1 changes, single owner. Reviewer + CI; no coordination.
2. **Lazy consensus.** Standard / Tier-2 changes. PR up; if no objection from affected teams within 48 h, merge.
3. **RFC.** Tier-3 changes (ABI, pipeline format, agent tool contract, governance). 7-day comment window after a maintainer marks `final-comment`. See `GOVERNANCE.md`.

Speed matters, but principal-team speed comes from picking the right pattern, not from skipping all of them.

## Hiring sequence

Headcount plan, in priority order. We hire in this order even if budget allows for more than one at a time, because each role's prerequisites need to be in place before the next.

| # | Role                         | Earliest start | Why this slot                                                       |
| - | ---------------------------- | -------------- | ------------------------------------------------------------------- |
| 1 | Founding CTO / Tech lead     | S0             | Sets the architecture bar; chairs RFCs.                             |
| 2 | Senior C++ engineer (Core)   | S0             | Data model is the keystone; no other team can move without it.      |
| 3 | Senior C++ engineer (Plugin Host) | S0        | ABI is load-bearing; designed before any plugin code is written.    |
| 4 | Senior C++ engineer (Adapters)    | S0        | OpenCASCADE wrapper is on critical path for the first end-to-end.   |
| 5 | Senior eng (AI Integration)  | S0             | BYOK + provider abstraction need a lead from day one.               |
| 6 | Senior eng (Desktop / Tauri) | S0             | Tauri / Rust expertise; chooses the IPC model.                      |
| 7 | Mid eng (Desktop / React)    | S0             | Frontend implementation; pairs with senior on viewport.             |
| 8 | Senior eng (Platform / Release)   | S0        | CI matrix and signing infra owner.                                  |
| 9 | Senior eng (DX / Docs)       | S0             | Docs + skills + plugin tutorials. Without DX, our SDK is unusable.  |
| 10| Mid eng (Adapters)           | S2             | OpenFOAM + Blender adapters need bandwidth.                         |
| 11| Senior eng (AI #2)           | S3             | Agent tool surface complexity grows fast; need pair coverage.       |
| 12| 2nd Platform engineer        | S10            | Auto-updater + marketplace ops demand this.                         |
| 13| 2nd DX engineer              | S15            | External community + plugin index demand this.                      |
| 14| Senior eng (Managed Services / Backend) | S13 | Pro-tier proxy + billing; new workstream.                           |
| 15| Site reliability engineer    | S16            | Pro-tier services need an SRE before SOC 2.                         |

## On-call

Once we ship public alpha (S12):

- **Two-person rotation** across senior engineers, weekly. One primary, one secondary.
- **Scope:** crashes in published builds, release-pipeline failures, security disclosures, AI-provider outages affecting managed-AI customers (post-S14).
- **SLA targets:** P0 acknowledged < 30 min; P1 < 4 h; P2 < 24 h. P0/P1 only page; P2 ticket only.
- **Runbooks live in `docs/runbooks/`.** Every paging-class incident produces a runbook update at the post-mortem.
- **Blameless post-mortems**, public to the team, within 5 working days.

Pre-public-alpha there is no on-call. The team handles its own breakage during business hours.

## Anti-patterns we will not adopt

- **Daily standups across teams.** Async updates instead.
- **A separate "QA team."** Engineers own quality of their code.
- **A separate "infra team" walled off from product engineers.** Platform is small and embedded in product flow.
- **Engineering managers without code authorship.** All managers in this org write code; otherwise they cannot review the architectures they sign off on.
- **Title inflation.** Senior is senior; mid is mid; principal-level distinction is in scope, not title.
- **Heroics culture.** A weekend save is a process bug, not a virtue. Logged in the post-mortem like any other failure.
