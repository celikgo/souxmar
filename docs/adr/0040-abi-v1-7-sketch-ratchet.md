# ADR-0040: Sketch C ABI ratchet (v1.6 → v1.7)

- **Status:** Proposed
- **Date:** 2026-05-12 (Sprint 29 push 1)
- **Author:** celikgokhun
- **Deciders:** core, plugin-host, desktop, AI, platform, DX
- **Tier:** 3 (heavy / requires RFC — frozen-header ratchet + new plugin type)
- **Affects:** `include/souxmar-c/sketch.h` (new), `include/souxmar-c/abi.h`
  (`SOUXMAR_ABI_VERSION_MINOR` bump 6 → 7 + history line), `docs/PLUGIN_SDK.md`
  (new `sketch.solver.*` plugin type), conformance suite (degenerate / under /
  over / well-constrained corpora), `examples/plugins/sketch-solver-gcs` (first
  conforming plugin), on-disk format (`*.sketch.yaml` v1).

## Context

Sprint 29 brings up the 2D sketcher that feeds the parametric features in Sprint 30 (ADR-0041). Without it, the feature operations have no inputs and the Sprint 30 exit criterion ("build a parametric bracket from a sketch") cannot ship. RFC-004 picks FreeCAD's planegcs (LGPL-2.1) as the v1 constraint solver, wrapped as a `sketch.solver.*` plugin so the kernel can swap without touching the host.

The sketch concept is **passive data plus an active solver**: a sketch is a list of primitives + constraints owned by the host; solver plugins read the sketch, mutate primitive positions, and report status. This is structurally different from the BREP session (ADR-0039) — sketches don't own kernel state; the data is plain numbers on a UV plane.

The sketch must anchor to either a world plane or a planar BREP face. Anchoring to a BREP face creates a coupling between this ratchet and ADR-0039's `souxmar_brep_session_t`, which is why ADR-0039 must land first.

RFC-004 carries the architectural rationale; this ADR carries the binding declaration under ADR-0008.

## Decision

The C ABI gains **one new header** (`include/souxmar-c/sketch.h`), **one new opaque handle type** (`souxmar_sketch_t`), **stable numeric constants** for primitive kinds and constraint kinds, and **eleven new function declarations** covering lifecycle, anchoring, primitive add, constraint add, queries, and solver invocation. A new plugin type `sketch.solver.*` is introduced. `SOUXMAR_ABI_VERSION_MINOR` bumps from **6** to **7**. No existing declaration moves; strict additive minor ratchet per ADR-0008. Sits on top of ADRs 0037–0039.

### New surface

See RFC-004 §Proposal for the verbatim header. The constant blocks `SOUXMAR_SK_*` (primitive kinds: point, line, arc, circle, polyline, spline) and `SOUXMAR_SC_*` (constraint kinds: coincident, parallel, perpendicular, tangent, equal, horizontal, vertical, distance, angle, radius, diameter) are pinned at the values in RFC-004 and never renumbered for the lifetime of v1.x.

### `abi.h` history entry

```
 *   v1.7  Sprint 29 push 1 — 2D sketch surface
 *                            (souxmar-c/sketch.h); ADR-0040.
```

`#define SOUXMAR_ABI_VERSION_MINOR 6` becomes `#define SOUXMAR_ABI_VERSION_MINOR 7`.

### Host-side implementation contract

- **Sketches are host-owned.** Unlike BREP sessions (plugin-owned), sketches live on the host. Solver plugins are stateless: they receive a sketch, mutate it in place, and return status.
- **The solver vtable is one entry point.** `solve_fn(souxmar_sketch_t*) → souxmar_status_t`. Multiple installed solvers compete by manifest `[plugin.priority.solver]` (higher wins).
- **Anchor to a BREP face requires a live session.** `souxmar_sketch_anchor_face` takes a `const souxmar_brep_session_t*` plus a `face_id`; the kernel rejects non-planar faces with `SOUXMAR_E_INVALID_ARGUMENT`.
- **Status enum extends `status.h`.** New error codes `SOUXMAR_E_UNDERCONSTRAINED`, `SOUXMAR_E_OVERCONSTRAINED`, `SOUXMAR_E_NO_CONVERGENCE` join the status code list. These are additive — no existing code value changes.
- **DoF reporting is on the bridge summary, not the C ABI.** `souxmar_sketch_solve` returns the status code only; DoF accounting is computed bridge-side from the primitive + constraint counts.
- **Solver concurrency.** A sketch is one unit of mutation. Concurrent solves on the *same* sketch are undefined; solves on *different* sketches are independent. The host dispatcher enforces.

### What stays out of scope

- **3D sketches.** The UV plane is 2D by construction; 3D sketches (helices, spatial curves) are a separate problem deferred post-v1.3.
- **Reference geometry (construction lines).** RFC-004 Open Q2 — defer to v1.7.1 if a real consumer surfaces.
- **Symmetry constraint.** RFC-004 Open Q3 — defer to a later ratchet.
- **Sketch IDs across projects.** Sketch IDs are project-scoped; cross-project references are not in v1.

## Alternatives considered

### Solver-as-host-code instead of solver-as-plugin

Pro: removes a plugin boundary; planegcs would compile directly into `libsouxmar-core`. Con: removes the swap path — a future souxmar-native solver becomes a host code change, not a new plugin. Plugin boundary also keeps planegcs's LGPL relink-rights story orthogonal to the rest of `libsouxmar-core`.

### Use solvespace's libslvs

Per RFC-004 §Alternative A — rejected. GPLv3 license is incompatible with our Apache-2.0 distribution.

### Build a souxmar-native constraint solver

Per RFC-004 §Alternative B — rejected for v1. Constraint solving has fifteen years of corner-case fixes in planegcs; a naive Gauss-Newton / LM solver hits singular Jacobians on coincident points and branch ambiguity on tangents within the first week. Re-evaluate as a *second* solver plugin later.

### Skip the ratchet; carry sketch data through the value-bag

Same shape as ADR-0037's value-bag rejection. A typical sketch has 10–200 primitives + constraints, well within the value-bag's payload tolerance — but solve-on-edit at 250ms debounce means many round-trips, and the solver plugin needs structured access to primitive positions, not JSON. The C ABI is the right shape.

## Consequences

### Positive

- **Sprint 30 (feature ops) has its input contract.** Feature operations consume sketches via `const souxmar_sketch_t*`; both arrive at this surface.
- **Sketcher UI has a stable backing.** The 16 chat-driven tools and the bridge surface map directly onto the C ABI entry points.
- **Solver swappability is preserved.** Future Apache-2.0 solvers (souxmar-native or third-party) join via `sketch.solver.*` without touching the host.
- **New error codes are clear, typed, and namespaced.** Plugin authors writing custom solvers know exactly what status values to return.

### Negative

- **Two LGPL relink-rights stories now.** OCCT (ADR-0039) and planegcs (this ADR) both need release-process attention. Same shape as OCCT; folded into the same `docs/LICENSING.md` deliverable.
- **DoF reporting at the bridge means the C ABI can't surface "split this sketch" suggestions natively.** A sketch with 250 DoF returns `SOUXMAR_E_NO_CONVERGENCE` after the planegcs timeout; the bridge formats the unfriendly message. Acceptable for v1; revisit if dogfood demands a typed "over-DoF" code.
- **Three new status codes baked into the v1 surface.** Once shipped, they're frozen. The names were considered carefully; the risk is finding a fourth state the solver wants to surface in v1.7.1. Mitigation: status codes are an additive surface — new codes can join at any minor.

### Risks

- **Risk:** planegcs perf cliff above ~200 DoF. **Mitigation:** RFC-004 risk register R-013; UI caps + "split this sketch" hint. Documented in pre-mortem.
- **Risk:** Solver priority resolution is unintuitive when multiple solvers are installed. **Mitigation:** Default priority on the in-tree `sketch-solver-gcs` is 100; third-party solvers must explicitly opt in to higher priority via their manifest.
- **Risk:** Anchoring to a non-planar face produces a runtime error rather than a compile-time refusal. **Mitigation:** the agent tool surface (`sketch.new`) validates the anchor in the bridge before opening the sketch.

## Pre-mortem (one year from today)

It is 2027-05-12 and the sketch ratchet went badly. Most likely failure mode: architectural users (one of our three primary personas) model floor plans with hundreds of walls in a single sketch; planegcs hits the 200-DoF cliff; the "split this sketch" suggestion in the UI is documented but unfriendly enough that users churn before reading it. We add an auto-split heuristic in v1.7.2 and a "convert N-segment polyline to a single primitive" affordance, but the architectural-persona launch-week reviews are critical.

Less-likely failure mode: a unit-conversion bug in the dimensional-constraint dialog lets users enter inches in a millimetre project; the radius/diameter pairing mis-applies the factor of 2 on one side. Mitigation: PR 6's eval suite includes the wrong-unit corner case.

Leading indicators:

- Any user-reported "sketch hangs my app" with > 100 primitives.
- Solver status messages reaching the forum verbatim — implies the bridge isn't formatting them user-friendlily.
- > 5% of saved sketches in dogfood exceed 50 DoF.

## References

- ADR-0008 — ABI v1 final freeze + ratchet rules.
- ADR-0037 / ADR-0038 / ADR-0039 — preceding ratchets.
- RFC-004 (`docs/rfcs/0004-2d-sketcher.md`) — the gating RFC.
- RFC-003 / ADR-0039 — BREP session, the anchor target for face-anchored sketches.
- RFC-005 / ADR-0041 — feature operations, the consumer of sketches.
- `include/souxmar-c/abi.h` — file under ratchet.
- `include/souxmar-c/status.h` — extended additively with the three new solver-status codes.
- `scripts/check-frozen-headers.sh` — CI gate.

## History

- 2026-05-11 (Sprint 6 push 4): first ratchet — `reader.*` (v1.0 → v1.1).
- 2026-05-11 (Sprint 7 push 3): second — mmap-backed buffer (v1.1 → v1.2).
- 2026-05-11 (Sprint 9 push 2): third — per-face-tag, ADR-0012 (v1.2 → v1.3).
- 2026-05-12 (Sprint 25 push 1): fourth — surface-stream, ADR-0037 (v1.3 → v1.4).
- 2026-05-12 (Sprint 27 push 1): fifth — field-stream, ADR-0038 (v1.4 → v1.5).
- 2026-05-12 (Sprint 28 push 1): sixth — BREP session, ADR-0039 (v1.5 → v1.6).
- 2026-05-12 (Sprint 29 push 1): **seventh ratchet — 2D sketch surface + sketch.solver.* plugin type, this ADR** (v1.6 → v1.7). Proposed; gates on RFC-004 acceptance.
