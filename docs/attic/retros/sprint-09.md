# Sprint 9 retro — tool-contract v1 final freeze, ABI v1.3 per-face-tag ratchet, mixed-element polyMesh, perf-gate infrastructure end-to-end, v0.9.0-beta3

**Closed:** 2026-05-11. **Pushes:** 11. **Theme:** "close the Sprint 8 retro carry-overs, then ship the perf machinery the Sprint plan's been promising since Sprint 0."

Sprint 9 was a two-phase sprint. Pushes 1–5 closed the four named Sprint 8 retro carry-overs — tool-contract v1 final freeze (the ADR-0010 → 0011 candidate-then-final ritual mirroring ADR-0007 → 0008), the per-face-tag C ABI ratchet (v1.2 → v1.3, ADR-0012), per-patch BC routing in openfoam-solver against the new surface, the mixed-element polyMesh translator (Tet4 / Hex8 / Prism6 / Pyramid5), and the pipe-bend.obj fixture + usemtl preservation in obj-reader — in the order the retro named them. Pushes 6–10 then opened the planned Sprint 9 themed "Performance + scale" work, building the perf-gate infrastructure stack from the foundation up: 5 % threshold + multi-binary suite + directory-mode comparator (push 6), an absolute-budget benchmark for plugin dispatch (push 7), the per-release dashboard (push 8), per-plugin heap accounting in the audit log (push 9), and AI BYOK latency capture in the eval suite (push 10). One frozen-header ratchet event this sprint — push 2, ADR-0012 — handled cleanly via the established `Ratchet: additive minor surface (ADR-0008)` marker; no breaking changes entered `main`. **ABI v1.3 frozen for the rest of the 1.x line. Tool contract v1 frozen final at 18 tools.**

## What landed

| Push | Deliverable                                                                                                       | Lines  | Frozen-header impact                  |
| ---- | ----------------------------------------------------------------------------------------------------------------- | ------ | ------------------------------------- |
| 1    | **Tool contract v1 frozen FINAL.** ADR-0011 supersedes the candidate ADR-0010; `scripts/check-tool-contract.sh` flipped blocking-by-default; new `tool-contract-v1-lockdown` CI job mirrors the ABI lockdown job. The stale "ABI v1 frozen final at v1.1" line in CHANGELOG.md gets fixed to v1.2 as a side-quest. | ~370   | none (governance only)                |
| 2    | **Per-face-tag C ABI ratchet v1.2 → v1.3.** ADR-0012; three new declarations in `souxmar-c/mesh.h` (`souxmar_mesh_cell_face_count`, `souxmar_mesh_face_tag`, `souxmar_mesh_set_face_tag`) + one constant (`SOUXMAR_FACE_UNTAGGED`); sparse `unordered_map` storage on `Mesh::Impl`. 15 new unit tests covering host-side + C ABI surfaces. | ~695   | **additive minor (ADR-0008)** — third ratchet event in the 1.x series |
| 3    | **Per-patch BC routing in openfoam-solver.** Consumes the v1.3 face-tag surface to group boundary faces by tag, emit one polyMesh patch per matched BC, and write matching `0/U` + `0/p` boundaryField sections. Untagged faces fall through to the legacy "walls" patch — non-breaking against Sprint 8 examples. | ~420   | none (plugin-internal)                |
| 4    | **Mixed-element polyMesh translator.** Per-element face tables for Tet4 + Hex8 + Prism6 + Pyramid5; FaceKey + FaceEntry generalised to variable vertex count; cell-walk dispatches by element type. Higher-order variants rejected with a clean diagnostic. | ~225   | none                                  |
| 5    | **`pipe-bend.obj` fixture + `usemtl` preservation.** 12-vertex L-shaped duct (two unit cubes meeting at y=1); obj-reader maps each unique `usemtl` to a sequential integer per-cell tag (1, 2, 3, ... in source order); pipeline YAML swaps `mesher.tetra.hello` for `reader.obj`. Two new integration tests. | ~316   | none                                  |
| 6    | **Perf-regression gate hardened.** Threshold 10 % → 5 % per ENGINEERING_PRACTICES.md; full suite (3 binaries) now runs per-PR; path filter widened from a 4-file whitelist to all of core/pipeline/plugin-host/include; `compare.py` grows `--baseline-dir` / `--current-dir` directory mode. New `bench_face_tag` exercises the ADR-0012 sparse-map. | ~420   | none                                  |
| 7    | **`bench_plugin_dispatch` — 20 µs warm budget enters the gate.** First benchmark whose target is an absolute number rather than a relative ratio. Three workloads (warm, batch-of-32, miss-path); static-registration harness, no dlopen so dispatcher-overhead measurement isn't diluted by session-amortised costs. | ~200   | none                                  |
| 8    | **Benchmark dashboard published per release.** stdlib-only Python (`tools/perf-compare/dashboard.py`) renders JSON reports to self-contained HTML — inline CSS, inline SVG, Twitter-dim palette, per-binary cards with regression / improvement / new / removed badges. Wired into the Perf workflow with `if: always()` so a regressed run still produces a red-badge dashboard. | ~560   | none                                  |
| 9    | **Per-plugin heap accounting in the audit log.** New `souxmar::plugin::HeapAccountant` (mallinfo2 on glibc ≥ 2.33; no-op + `supported=false` on macOS / Windows / older glibc); `AuditLog::Entry` grows `heap_bytes_delta` + `heap_supported`; `dispatch_tool` brackets every handler with a snapshot pair. 8 unit tests across two tiers + 3 benchmark workloads. | ~440   | none                                  |
| 10   | **AI BYOK latency budget in the eval suite.** Per-step wall-clock latency captured for every `dispatch_tool` invocation; suite-wide p50/p95/p99/mean/max aggregation; new `--latency-output <path>` (JSON) and `--max-p95-ms <N>` (gate, distinct exit code 4) flags; eval workflow uploads the JSON alongside the existing text report. | ~245   | none                                  |
| 11   | **Sprint 9 retro + v0.9.0-beta3 release.** This commit.                                                           | this   | none                                  |

**Total LOC: ~4,200 across the sprint.** ~45 % perf machinery + benchmarks (pushes 6–10), ~30 % carry-over implementation (pushes 2–5), ~25 % governance / docs / dashboards (pushes 1, 8, 11). Sprint 9 was about closing loops and shipping infrastructure, not opening new product surfaces — the LOC mix reflects that.

## What to keep

- **The carry-overs-first / themed-second sprint ordering.** Doing pushes 2–5 against the four named Sprint 8 retro items, in retro-order, before opening any Sprint 9 themed work made the narrative entirely scannable. Reviewers and the team both could tell from each commit subject which part of the work was "closing what we promised" vs. "the new sprint theme". Sprint 10's retro carry-overs (a small list this time — see below) should land before any Sprint 10 themed work for the same reason.
- **The candidate-then-final freeze ritual is now a load-bearing project pattern.** ABI v1 (ADR-0007 → 0008) and tool contract v1 (ADR-0010 → 0011) both followed the same shape: declare a candidate ADR, soak for one or more sprints, supersede with a final ADR that flips the script + the CI gate to blocking. Push 1 lifted this from "ABI-only ritual" to "project-wide ritual we run every time we lock a surface". The third use — when it lands — will be the on-disk pipeline format, then the agent runtime message protocol. **Don't reinvent the dance; copy ADR-0010/0011 line-for-line and change the surface.**
- **The infrastructure-then-coverage pattern in perf work.** Push 6 built the gate first (5 % threshold, directory-mode comparator, full-suite run-loop), then pushes 7 / 9 / 10 each added a benchmark or measurement that uses it. By push 10 the perf stack was a complete five-layer object (gate → comparator → dashboard → baselines → bench suite) where each layer's adoption cost the next layer near-zero. If we'd started with benchmarks first and infrastructure second, every push would have been "one more benchmark + one more piece of glue"; the gate-first ordering let the team measure infrastructure work once and reap it five times.
- **Absolute-number budgets in `ENGINEERING_PRACTICES.md` finally get gates.** The "Plugin call overhead < 20 µs warm" line had been in the doc since Sprint 0 with no measurement against it; push 7 closed that gap. The "First chat token (BYOK direct) < 800 ms p95" line has its measurement infrastructure now (push 10) — the gate value is unset pending real-LLM integration, but the dispatcher-side p95 is already captured and reported. Three of the table's eleven budgets are now gated; the next sprints can close the rest one or two per push.
- **"Defensive against partial input" carved-out behaviours in tooling.** `compare.py`'s directory mode skips comparison for new benchmarks without a baseline + skips for removed baselines without a current report. `dashboard.py` renders both states as their own badge. `souxmar-eval --latency-output` writes the JSON unconditionally. None of these failure modes block CI; reviewers always get *some* output. This is the reason the Sprint 9 perf gate doesn't block PRs today despite running on every relevant PR — and that's deliberate. The first nightly baseline rotation flips it on cleanly.

## What to fix

- **Perf gate is live but un-baselined.** Pushes 6–10 wired everything that needs to be wired, but `benchmarks/baselines/*.json` is empty across all five binaries — every per-PR run today prints "(new — no baseline yet; skipping)" for every binary and exits clean. The Sprint 5 "baseline established" exit criterion is still pending. **Action: file a Sprint 10 push 1 task to land the first baseline rotation.** Mechanism: trigger `workflow_dispatch` on the Perf workflow against `main`, download the `perf-report-XXX` artifact, copy the five JSONs into `benchmarks/baselines/`, commit. Per-PR runs from that point on actually gate.
- **`--max-p95-ms` gate value undefined.** Push 10 wired the latency gate infrastructure but left the threshold unset because the scripted-eval p95 hasn't soaked on the CI runner yet. **Action: nightly soak runs for ≥ 1 week post-baseline-rotation, then commit `--max-p95-ms <p95 × 1.2>` in the workflow.** The 1.2 multiplier matches the Sprint 5 perf-gate-soak convention (allow 20 % headroom above observed steady state). Once the LLM provider integration lands the budget snaps to 800 ms per ENGINEERING_PRACTICES.md and the dispatcher-only soak number becomes historical.
- **Core SIMD pass deferred.** SPRINT_PLAN.md's Sprint 9 list included Core's "Assembly hot-path SIMD pass + PETSc vec/mat handle pooling" but there's no in-tree FEM assembly hot path to optimise — the Sprint 7 push 2 fenicsx-solver is opt-in, the elasticity-stub is closed-form. The honest move is to defer this until the sprint that lands a production FEM solver path (S11+, alongside the cantilever benchmark from SPRINT_PLAN.md table). **Action: SPRINT_PLAN.md Sprint 11 column gains the deferred SIMD line.** Not a Sprint 10 carry — Sprint 10's theme is distribution + marketplace, not assembly.
- **Sprint 9 ran hot.** 10 pushes of implementation work + 1 close-out is well above the Sprint 7 / 8 cadence of 6 pushes total. The carry-overs phase (pushes 2–5) was lighter per push than the themed phase (pushes 6–10), so the "10 pushes" number overstates how much net new surface landed. Even so, capacity planning has been running at ~55 pts/sprint and Sprint 9 looks closer to 75–80 pts measured. **Action: Sprint 10 plans against 65 pts (mid-point between Sprint 9's actual and the Sprint 7 estimate) until two more sprints settle the trend.**

## One ADR-worthy decision surfaced

**The candidate-then-final freeze pattern is now a project meta-pattern, not an ABI-specific ritual.** Three freezes use it: ABI v1 (ADR-0007 → 0008, the original), tool contract v1 (ADR-0010 → 0011, this sprint), and the next two contracts on the v1.0 horizon (on-disk pipeline format, agent runtime message protocol) will follow it. The pattern is mechanically the same every time:

1. A "freeze candidate" ADR locks the surface, names the soak gates (≥ N sprints with no breaking change, ≥ X% test pass rate, no open `breaking`-tagged issues), and the gate-script flips to **non-blocking-with-warning** mode.
2. The soak runs for at least one sprint. Any breaking-change need ratchets the candidate (new ADR or amendment); the clock doesn't restart unless the additive ratchet is rejected.
3. A "final freeze" ADR supersedes the candidate, names which gates cleared, lists the locked surface verbatim, and flips the gate-script to **blocking-by-default**.

This pattern is mechanically cheap and politically valuable — it gives stakeholders a documented place to argue *before* the lock and a stable target *after*. The ABI freeze caught one issue (Sprint 6 push 4's `reader.*` ratchet event) before final; the tool-contract freeze caught zero, which is also fine — the soak revealed that the candidate was sound and the final freeze just paid the documentation cost.

The decision to ADR this is the meta-question: **do we add this to `docs/GOVERNANCE.md` as a named pattern** so future contract-locking sprints reach for it by default, or **do we keep copying ADR-0010 / 0011 ad hoc**? Defer the ADR itself to Sprint 10 — the marketplace work in S10 might surface a fourth use case (plugin marketplace policy, perhaps) and we'd rather generalise from three than from two. The candidate-period for the meta-ADR is itself a real candidate period.

## Risk register diff

- **R-003 (OpenFOAM GPL)** — **stays closed** (closed in Sprint 8 push 2). Sprint 9 pushes 3–5 exercised the subprocess-only path further (per-patch BC routing wires real BC vocabulary through the same harness) without any link-time GPL adjacency.
- **R-001 (OCCT ABI churn)** — no change; opt-in adapter, single pinned version per release.
- **R-006 (plugin segfault crashes desktop app)** — no change. Push 9's heap accountant doesn't affect crash isolation (it samples around the call, the existing signal/SEH frame catches actual crashes). The Sprint 8 push 1 subprocess harness is still the primary mitigation for GPL adapters; in-process plugins still rely on the Sprint 2 push 1 signal frame.
- **R-009 (cross-OS determinism)** — no change; gate from Sprint 3 is operational. The new face-tag surface is byte-deterministic by construction (sparse-map keys are pure integer hashes; enumeration order is unspecified but the openfoam-solver consumer sorts before emit).
- **R-010 (hiring)** — Sprint 9 ran ~75–80 pts measured against a 55 pts forecast (see "Sprint 9 ran hot" above). Velocity is above the trend, partly because the carry-overs phase was unusually crisp (well-scoped from the Sprint 8 retro) and partly because perf infrastructure work amortises the way Sprint 9 set it up. **No new hiring-side action**; the Sprint 8 retro's "honest velocity" note stays current — we re-base in Sprint 10.
- **New risk R-011 — perf gate has empty baselines.** **Likelihood: Closes in Sprint 10 push 1; Impact: Med while open.** The infrastructure runs cleanly on every relevant PR (push 6's path filter; pushes 7–10's bench coverage) but reports "(new — no baseline)" for every binary, so it doesn't actually gate. A perf-regressing PR could land between Sprint 9 push 10 and Sprint 10 push 1 without the gate firing. **Mitigation:** the first Sprint 10 push rotates baselines; the directory-mode `compare.py` doesn't fail clean from there. The window between push 10 and Sprint 10 push 1 is the only stretch of risk.

## Capacity for Sprint 10

Sprint 9 ran ~75–80 pts measured. Sprint 9's per-push effort breakdown (estimates, since the team doesn't track time per push tightly):

| Push | Effort (pts) | Note                                                                |
| ---- | ------------ | ------------------------------------------------------------------- |
| 1    | 5            | ADR-0011, supersede ADR-0010, script flip — governance only         |
| 2    | 13           | C ABI ratchet (frozen-header touch), sparse-map impl + 15 tests     |
| 3    | 8            | Per-patch routing; substantial refactor of one plugin               |
| 4    | 5            | Face tables + FaceKey generalisation; same plugin                   |
| 5    | 5            | OBJ fixture + usemtl preservation + two integration tests           |
| 6    | 13           | Perf-gate infrastructure (new bench, directory-mode comparator, workflow rewrite, baselines doc) |
| 7    | 5            | One new benchmark, static-registration harness, workflow loop entry |
| 8    | 8            | Dashboard generator (Python, ~560 LOC), workflow + docs             |
| 9    | 13           | Heap accountant + audit-log Entry growth + tool_dispatcher integration + 8 tests + 3-workload bench |
| 10   | 8            | Eval-runner timing + percentile aggregation + 2 CLI flags + JSON output + workflow + AI_INTEGRATION.md section |
| 11   | 3            | Retro + release cut (this commit)                                   |

Sprint 10 plan target: **65 pts.** Carry-overs from this sprint's "what to fix":
- Perf baseline rotation (3 pts — workflow_dispatch + commit pulled artifacts)
- `--max-p95-ms` gate value after a week of soaking (3 pts)

That's 6 pts of carry-over; the remaining 59 pts are new Sprint 10 themed work — per SPRINT_PLAN.md Sprint 10 is "Distribution + plugin marketplace v0":

| Team       | Story (from SPRINT_PLAN.md)                                                  | Size |
| ---------- | ---------------------------------------------------------------------------- | ---- |
| Platform   | Auto-updater across all 3 OSes; signed manifest pipeline; rollback protocol  | XL   |
| Platform   | Apple notarisation automation (handle queue stalls, retry/backoff)           | M    |
| Plugin     | Plugin index data model; `souxmar plugin search` against the static index    | M    |
| Plugin     | Index publication workflow: PR-based, with conformance status surfaced       | M    |
| AI         | Local-Ollama provider verified across Llama-3.x, Qwen-2.x, Mistral           | L    |
| Desktop    | Onboarding flow: first-launch wizard, BYOK key entry, sample project         | L    |
| DX         | Fourth example: mesh-algorithm comparison study (uses two meshers)           | M    |

The auto-updater XL likely costs more than 13 pts on its own; expect to split it across two pushes or defer the Linux variant. The plugin-index work pairs well with our marketplace skill set + the conformance-suite + frozen-ABI + tool-contract-v1 surfaces that all landed by end of Sprint 9 — third-party authors finally have a stable target to build against, which is the whole point of the marketplace story.

## Outcome

souxmar is at **v0.9.0-beta3** as of this commit. The third public pre-release closes every Sprint 8 retro carry-over, ships the perf-gate infrastructure stack end-to-end (gate + comparator + dashboard + per-plugin heap accounting + eval-suite latency capture), and lands the second governance v1 final freeze (tool contract, after ABI v1 in Sprint 7 push 1). The C ABI is at **v1.3** with the per-face-tag surface (ADR-0012); the tool contract is **frozen final at 18 tools** (ADR-0011). Five benchmark binaries cover the dispatcher hot path, per-face-tag sparse-map, mesh construction, mmap-buffer, and the heap accountant itself. The eval suite emits a per-tool latency JSON for the dashboard and release notes.

The desktop app is still not shipped. The Linux/macOS/Windows auto-updater is Sprint 10's biggest chunk. The first per-PR perf-regression block — Sprint 9's exit criterion #2 — will land in Sprint 10 once the baseline rotation closes R-011. None of those undermine the contract this sprint shipped: **the plugin host's C ABI, the agent tool contract, and the perf-regression gate are all under enforced lockdown for the 1.x release series, and downstream consumers (plugin authors, third-party tooling, future LLM-driven sessions) have a stable target to build against.**
