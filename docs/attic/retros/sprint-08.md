# Sprint 8 retro — subprocess harness, OpenFOAM + Blender adapters, CFD-aware agent surface, tool-contract v1 candidate, v0.9.0-beta2

**Closed:** 2026-05-11. **Pushes:** 6. **Theme:** "ship the GPL adapters and the CFD agent surface."

Sprint 8 was the adapter sprint: every push delivered against the GPL/process-isolation contract from ADR-0009 or against the CFD-aware agent surface the OpenFOAM adapter needed downstream. Every push lands closed-form / always-on coverage for default CI plus opt-in heavy-dep coverage for the nightly matrix — same "always-on + opt-in" pattern that paid off in Sprints 6 and 7. **No frozen-header surface touched. ABI v1.1 stands the whole sprint.**

## What landed

| Push | Deliverable                                                                                                       | Lines | Frozen-header impact                |
| ---- | ----------------------------------------------------------------------------------------------------------------- | ----- | ----------------------------------- |
| 1    | **Process-isolation subprocess harness.** `souxmar::plugin::run_subprocess` + `find_executable_on_path`. POSIX `posix_spawnp`/`poll`/`waitpid`; Windows `CreateProcessW`. Crash isolation, timeouts, stream-capped capture. 12 unit tests. | ~900   | none — host-side C++ only           |
| 2    | **OpenFOAM adapter (opt-in) + `cfd-stub` (always-on).** ADR-0009 implementation: subprocess-only OpenFOAM, three capabilities sharing one driver, mandatory 1h timeout, placeholder polyMesh. Closes **R-003**. | ~930   | none                                |
| 3    | **Concept-geometry readers.** Always-on `obj-reader` (Wavefront OBJ, all four `f` field forms, fan-triangulation); opt-in `blender-reader` (subprocess `blender -b --python-expr "bpy.ops.wm.obj_export"`). | ~1,030 | none                                |
| 4    | **CFD-aware BC tools.** `apply_inlet` / `apply_wall` / `apply_outlet` — catalogue 13 → 16. Same staging contract as `set_bc`; `type` field routes downstream solvers. | ~720   | none                                |
| 5    | **CFD planner + BC validator; ADR-0010 tool-contract v1 freeze candidate.** `propose_cfd_setup` + `validate_bcs` — catalogue 16 → 18. ADR-0010 locks the tool framework + 18-tool catalogue; `scripts/check-tool-contract.sh` is the (currently non-blocking) gate. | ~1,050 | none                                |
| 6    | **Real Tet4 → polyMesh translator + `examples/pipe-bend/` + v0.9.0-beta2.** Closes the placeholder noted by push 2; pipe-bend example wires the full CFD chain; sprint retro + release. | this   | none                                |

**Total LOC: ~5,500 across the sprint.** ~50% adapter implementations (push 1 / 2 / 3 / 6's translator), ~30% AI surface (push 4 / 5), ~20% governance + docs (ADR-0010, retros, CHANGELOG entries, scripts/check-tool-contract.sh, the pipe-bend README).

## What to keep

- **The subprocess harness paid for itself twice in one sprint.** Push 1 landed it as foundational work for the OpenFOAM adapter (push 2). Push 3 used it again for the Blender importer with **zero changes** to the harness — same `SubprocessOptions{argv, work_dir, env, timeout, max_capture_bytes}` shape, same `SubprocessResult{ok, exit_code, fatal_signal, timed_out, stdout, stderr, error_message, duration}` surface. The OS process boundary turned out to be the right abstraction for both GPL OpenFOAM (process isolation for license reasons) and Blender (process isolation for ABI-stability reasons — bpy is not a stable embedding target). ADR-0009 named this generality in passing; push 3 proved it.
- **ADR-0010's candidate-then-final pattern was deliberately cargo-culted from ADR-0007 → ADR-0008.** The ABI freeze process worked — the candidate period surfaced one additive ratchet event (Sprint 6 push 4 `reader.*`) without restarting the clock, and the final freeze locked a known-good surface. The tool-contract freeze copies the structure exactly: candidate now, final at Sprint 9 push 1 conditional on (a) ≥1 sprint with no breaking change needed, (b) ≥90% agent eval pass rate across providers, (c) zero `agent-tool-v1` breaking issues open. The mechanism is already proven; we're just re-running it for the AI surface.
- **The "always-on + opt-in" pattern is now the default reflex for every external dependency.** Five domains landed under it: OCCT (Sprint 6 push 4), Gmsh (Sprint 6 push 5), DOLFINx (Sprint 7 push 2), OpenFOAM (this sprint push 2), Blender (this sprint push 3). At this point the team writes the always-on closed-form sibling first — it's the conformance-gate plugin — and the opt-in adapter follows the same skeleton. Onboarding new adapters is now a templated exercise.
- **The CFD-aware BC tools (push 4) + the planner (push 5) materially moved the agent's CFD performance.** The Sprint 7 push 4 eval suite logged CFD-bucket failure rates around 25–30% with `set_bc` as the only BC vocabulary (LLMs writing `{type:'dirichlet', value:[1,0,0]}` and getting silently wrong inlet-vs-temperature semantics). The `apply_inlet`/`apply_wall`/`apply_outlet` trio plus `propose_cfd_setup`'s tag-name heuristic dropped this to single digits on the nightly Anthropic + OpenAI runs. The CFD vocabulary was worth its weight in tool-count.

## What to fix

- **The Tet4-only restriction in the polyMesh translator (push 6) is brittle.** Real CAD-meshed geometries arrive mixed: Tet4 + Pyramid5 + Prism6 + Hex8, especially when an STL surface gets swept or extruded. The current translator rejects with INVALID_ARGUMENT, which is honest but unhelpful. **Action: open an additive-minor follow-on PR for Sprint 9 push 1 adding Pyramid5 + Prism6 + Hex8 face tables.** The translator's structure (face-key dedup + owner/neighbour bookkeeping + boundary patch extraction) generalises directly; only the per-element-type face-vertex table changes. No frozen-header touch — pure plugin-internal.
- **`openfoam-solver` writes a single "walls" boundary patch.** The Sprint 8 push 4 CFD-aware tools stage BCs on `session_state.boundary_conditions` keyed by `tag`, but the C ABI's `souxmar_mesh_*` surface exposes cell tags only, not face tags. So even with `apply_inlet{tag: 'inlet', velocity: 1.5}` staged correctly and validated by `validate_bcs`, the OpenFOAM adapter has no way to map that tag onto polyMesh boundary faces. **Action: file an additive-minor ratchet PR for Sprint 9 exposing `souxmar_mesh_face_tag(mesh, cell, local_face)` via the existing `mesh.h` surface.** This is a frozen-header touch using the additive-minor marker (`Ratchet: additive minor surface (ADR-0008)`); it bumps ABI v1.2 → v1.3. The plugin-side change is then mechanical: walk staged BCs, look up matching face tags, emit one boundary patch per inlet/wall/outlet tag.
- **The blender-reader's Python script is brittle to Blender version skew.** `bpy.ops.wm.obj_export` is canonical from Blender 4.0 onwards but 3.4–3.6 expose the same operator under `bpy.ops.export_scene.obj` with a slightly different keyword set. We currently document the version_range as `>=4.0,<5.0` and refuse to register on older Blender installs. **Action: revisit when a real user files a 3.6 issue.** Pre-emptive multi-version support is dead weight without a concrete request.
- **The placeholder geometry in `examples/pipe-bend/` is genuinely a placeholder.** The mesher stage is still `mesher.tetra.hello` producing a unit tet — there is no real pipe-bend geometry in-tree, and the CFD result is uniform velocity regardless. **Action: file a Sprint 9 task to land a `pipe-bend.obj` fixture + swap the mesh stage to `reader.obj`.** The translator's per-tag boundary patch extraction (the prior bullet) is a precondition for the OpenFOAM matrix to produce numerically-interesting results on this geometry; until then, the pipe-bend example is structurally complete but numerically trivial.

## One ADR-worthy decision surfaced

**Per-face tags on the C ABI as an additive-minor v1.3.** The current `souxmar-c/mesh.h` exposes per-cell tags (`souxmar_mesh_cell_tag`) and the C ABI has good support for getting cell node indices, but nothing about per-face tags. Three downstream consumers want this:

1. **`openfoam-solver` boundary patch extraction** — map staged inlet/wall/outlet BCs onto polyMesh patches (this sprint's blocker).
2. **The mesh-quality postproc plugin** would surface "face-tag X has aspect-ratio outliers" diagnostics if face tags were addressable.
3. **The reader plugins** (`obj-reader`, `stl-reader`, `blender-reader`) currently throw away the original file's named-collection / `usemtl` group information — they could surface it as face tags if the C ABI exposed the slot.

The right shape is roughly:

```c
int32_t souxmar_mesh_face_tag(const souxmar_mesh_t* mesh,
                              uint64_t cell_index,
                              uint8_t  local_face_index);  /* 0..N where N is cell's face count */

souxmar_status_t souxmar_mesh_set_face_tag(souxmar_mesh_t* mesh,
                                           uint64_t cell_index,
                                           uint8_t  local_face_index,
                                           int32_t  tag);
```

Storage on the host side can stay sparse (only tagged faces materialise) — most cells have all-default-untagged faces and don't pay for slots.

This is an **additive-minor ratchet** (ABI v1.2 → v1.3) per ADR-0008's rules: the existing functions don't move, new declarations join, the conformance check C004 catches mismatched-minor plugins at registration time so old plugins keep working. **Defer the formal ADR to a Sprint 9 push.** The mechanism is the contract; the design is straightforward once a real consumer (openfoam-solver per-patch routing) drives the shape.

## Risk register diff

- **R-003 (OpenFOAM GPL)** — **Closed** (push 2). ADR-0009 design + push 1 harness + push 2 plugin + push 6 real translator. The risk lived for three sprints and closed on schedule.
- **R-001 (OCCT ABI churn)** — no change; the adapter still pins to a single OCCT version per release, and Sprint 6's adapter has been stable across the soak.
- **R-006 (plugin segfault crashes desktop app)** — likelihood downgraded Low → Very Low. The subprocess harness (push 1) means GPL-isolated and otherwise-risky adapters now run out-of-process by default; an in-process plugin segfault is the worst case, not the only case. The desktop app hasn't shipped yet so this is hypothetical until Sprint 11ish, but the architectural runway is now in place.
- **R-009 (cross-OS determinism)** — no change; the determinism gate from Sprint 3 is operational; the subprocess harness's POSIX vs. Windows paths are tested at the SubprocessResult level (fatal_signal mapping for SEH codes) so cross-platform output stays comparable.
- **R-010 (hiring)** — Sprint 8 ran at ~55 pts measured (matching the Sprint 7 retro's adjusted estimate). The "always-on + opt-in" templating made adapter work cheaper per-push than Sprints 5–6 estimated. We're at honest velocity now.

## Capacity for Sprint 9

Sprint 7 retro forecast Sprint 8 at ~55 pts; we landed ~55 pts. Sprint 8's per-push effort breakdown:

| Push | Effort (pts) | Note                                                            |
| ---- | ------------ | --------------------------------------------------------------- |
| 1    | 13           | Subprocess harness — substantial; both platforms; 12 unit tests |
| 2    | 13           | OpenFOAM adapter; ADR-0009 implementation; closes R-003         |
| 3    | 8            | Two more reader plugins; subprocess harness reused              |
| 4    | 8            | Three new AI tools; uniform structure with set_bc               |
| 5    | 8            | Two more AI tools + ADR-0010 + check-tool-contract.sh           |
| 6    | 5            | Real polyMesh translator (focused) + example + retro + release  |

Sprint 9 plan target: **~55 pts again.** Carry-overs from this sprint's "what to fix":
- Pyramid5 + Prism6 + Hex8 face tables in openfoam-solver (5 pts)
- Per-face-tag C ABI ratchet (8 pts — frozen-header change, careful)
- Per-patch BC routing in openfoam-solver (8 pts — depends on above)
- `pipe-bend.obj` fixture + reader-driven mesh stage (3 pts)

That's 24 pts of follow-on; the remaining 30 are new Sprint 9 work — most likely the tool-contract v1 **final freeze** (ADR-0011) + the agent eval suite's CFD-bucket regression bar + the desktop app's first viewport stub.

## Outcome

souxmar is at **v0.9.0-beta2** as of this commit. The OpenFOAM adapter is real, runs `simpleFoam`/`pimpleFoam`/`interFoam` under ADR-0009 process isolation, and produces correct polyMesh from arbitrary Tet4 souxmar meshes. The Blender importer is real, runs `blender -b --python-expr` under the same harness, and round-trips .blend → OBJ → Tri3. The agent has 18 tools — six of them CFD-aware — and a planner that turns a verbal goal into a dispatch-ready BC plan. ADR-0010 has the tool contract under candidate freeze.

The desktop app is still not shipped. Per-patch BC routing is Sprint 9's problem. None of those undermine the contract this sprint shipped: **a third-party plugin author can now target the souxmar plugin host with confidence that their adapter — in-process or subprocess, FEM or CFD, free or GPL — fits the same hexagonal pattern.**
