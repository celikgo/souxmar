# ADR-0041: BREP feature-ops C ABI ratchet (v1.7 → v1.8)

- **Status:** Proposed
- **Date:** 2026-05-12 (Sprint 30 push 1)
- **Author:** celikgokhun
- **Deciders:** core, plugin-host, adapters, desktop, AI, platform, DX
- **Tier:** 3 (heavy / requires RFC — frozen-header ratchet **on an existing header**)
- **Affects:** `include/souxmar-c/brep.h` (extends — this is the only post-v1.0 ratchet
  that **modifies** an existing v1 ABI header rather than adding a new one),
  `include/souxmar-c/abi.h` (`SOUXMAR_ABI_VERSION_MINOR` bump 7 → 8 + history line),
  `examples/plugins/cad-occt` (gains feature-op vtable methods),
  on-disk format (new `design.yaml` v1), conformance suite (per-op golden tests +
  replay corpus), `docs/PLUGIN_SDK.md` (on-disk format taxonomy).

## Context

Sprint 30 is the capstone of the modeling block: sketches + features produce a parametric body, and `design.yaml` (introduced in RFC-005) records the ordered history. Without this ratchet, the v1.2 release ships a sketcher and a kernel but no way to combine them.

This ratchet has one structural property distinct from ADRs 0037–0040: **it extends an existing v1 ABI header (`brep.h`) rather than adding a new one.** Per ADR-0008's ratchet rules, additive minor surfaces include "new declarations" added to existing frozen headers. The `check-frozen-headers.sh` gate accepts this with the standard `Ratchet: additive minor surface (ADR-0008)` marker; no existing declaration in `brep.h` moves or changes signature.

The feature operations are conceptually paired with `design.yaml`'s replay engine: each feature kind in the YAML maps onto one C ABI entry point. The host's replayer is the only legal caller of these entry points; plugin authors writing `cad.*` plugins implement them but should not call them directly from other capability surfaces.

RFC-005 carries the architectural rationale (`design.yaml` v1 schema, replay determinism semantics, parameter model); this ADR carries the binding declaration for the C ABI surface under ADR-0008.

## Decision

`brep.h` (introduced at v1.6 by ADR-0039) gains **ten new function declarations** covering: extrude, revolve, sweep, loft, fillet, chamfer, boolean (union / subtract / intersect), pattern_linear, pattern_circular, delete_body. `SOUXMAR_ABI_VERSION_MINOR` bumps from **7** to **8**. No existing `brep.h` declaration moves or changes signature; this is a strict additive minor extension per ADR-0008.

### New surface

See RFC-005 §2 (C ABI extension) for the verbatim entry points. Each feature op takes the session + typed arguments and either returns the resulting body ID (for ops that create bodies) or a status code (for ops that modify in place). The session is mutated; the body graph version increments (host-side state).

### `abi.h` history entry

```
 *   v1.8  Sprint 30 push 1 — BREP feature ops (extend brep.h);
 *                            ADR-0041.
```

`#define SOUXMAR_ABI_VERSION_MINOR 7` becomes `#define SOUXMAR_ABI_VERSION_MINOR 8`.

### Host-side implementation contract

- **The replayer is the canonical caller.** The host's `design.yaml` replayer walks the feature list in order and dispatches each entry to the corresponding ABI entry point. Direct invocation outside the replayer is allowed (CLI, Python bindings) but the replayer's "incremental regenerate from affected feature" logic does not run.
- **Body ID stability is the v1.6 contract.** Feature ops produce new bodies with fresh IDs; modifying ops (fillet, chamfer, boolean on a target) preserve the target's body ID. Face/edge IDs within a body may shift on mutating ops; this is the same constraint that affects `design.yaml`'s topology references.
- **Failures are typed.** Sketch input that produces an invalid profile (self-intersecting, open curve where closed is required, etc.) returns `SOUXMAR_E_INVALID_ARGUMENT`. Kernel-side failure (numerical) returns `SOUXMAR_E_INTERNAL` with a textual reason via a separate plugin-error channel — not in this ABI.
- **Boolean tool body is consumed.** `souxmar_brep_boolean(target, tool, …)` deletes the tool body from the session on success; on failure the tool is preserved. This matches OCCT's `BRepAlgoAPI_*` semantics.
- **Patterns are deterministic.** Linear and circular patterns generate body IDs in the same order (deterministic by direction × step) so replay reproduces the same IDs.

### What stays out of scope

- **Parameter expressions.** RFC-005 Open Q2 — scalar values only in v1.8. Expressions (`bracket_height = bolt_diameter * 1.5`) deferred to v1.9 RFC.
- **Multi-body assemblies with mates.** Multi-body designs are supported (the session graph is multi-rooted); assembly-level mates/joints are a separate, larger problem.
- **Configurations / variants.** "Show this bracket in three thicknesses" — deferred post-v1.3.
- **Mesh-side boolean ops.** A `feature.boolean_mesh` path (Manifold-backed) is a future addition; this ratchet is BREP-only.
- **Helix / coil sweep paths.** Sweep takes a profile sketch + a path sketch (both 2D for now); 3D path support deferred.

## Alternatives considered

### Put feature ops on a separate header (`brep_features.h`)

Pro: keeps `brep.h` slim; segregates the static "import/query" surface from the mutating feature surface. Con: every plugin implementing `cad.*` would include both headers anyway, and the natural reading is "feature ops are part of the BREP surface." Splitting feels like organization for its own sake.

### Make feature ops part of the `cad.*` vtable directly (no C ABI declarations)

Pro: vtable extension is more flexible than ABI declarations. Con: vtable additions are also ABI-visible (the vtable layout is part of the v1 contract), and inline-declaring feature ops on the vtable rather than as standalone ABI functions hides them from non-cad consumers (Python bindings, CLI). The standalone declarations are the right shape for tooling.

### Defer to v2 ABI

Pro: would let us reconsider the whole BREP-session shape with v1.6+v1.7 hindsight. Con: a v2 ABI break is a 12-month migration for plugin authors; we are not three minor bumps from a major. The ratchet is fine; v2 reasoning is post-v1.x.

### Implement feature ops as a separate plugin type (`feature.*`)

Pro: keeps `cad.*` plugins free of feature-op vtable methods. Con: features are kernel-bound (extrude calls OCCT's `BRepPrimAPI_MakePrism`); separating them creates a two-plugin dependency for every operation. The `cad.*` plugin owns the kernel; features belong on it.

## Consequences

### Positive

- **`design.yaml` v1 has its full backing.** Every feature kind in the YAML maps onto one ABI entry point.
- **Agent tool surface has obvious bindings.** The ten `feature.*` tools each have a single backing C call.
- **No new plugin types.** `cad.*` already exists from v1.6; this ratchet just adds methods.
- **Replay is auditable.** Every feature replay is one C call with typed inputs and a typed output; the replayer can log + checkpoint trivially.

### Negative

- **Feature-op semantics across kernels.** The C ABI is kernel-agnostic, but each `cad.*` plugin implements the semantics. If a future kernel binding (e.g., a wrapper for a commercial kernel) has slightly different fillet semantics, designs may render differently. Mitigation: the determinism contract is "same kernel version" — cross-kernel reproducibility is explicitly not promised in v1.
- **Test surface grows by ~10× per-op golden tests.** Each feature op has ≥1 golden test in the conformance suite. Necessary cost.
- **OCCT API surface coupled to the C ABI shape.** The C ABI's `souxmar_brep_extrude(session, sketch, distance, direction, operation, target_body, out_status)` shape is informed by OCCT's `BRepPrimAPI_MakePrism` but not slavishly so. A future kernel with very different extrusion ergonomics would map awkwardly — but that's a kernel-bring-up problem, not a v1 ABI problem.

### Risks

- **Risk:** Face/edge ID stability under mutating ops is weaker than `design.yaml`'s replay assumes. RFC-005 pre-mortem flags this; ADR's mitigation is a fingerprint heuristic (face centroid + normal at save time) that re-binds references on kernel-version bumps, deferred to follow-up.
- **Risk:** Replay perf is dominated by a single slow feature (e.g., loft of complex profiles). **Mitigation:** incremental regenerate (cache feature outputs; track dependencies) lands in RFC-005 PR 4. Without it, a 50-feature design rebuilds 48 features on a parameter change — unacceptable.
- **Risk:** OCCT exceptions reach the host as opaque crashes. **Mitigation:** the `cad.occt` plugin wraps every OCCT call in a try/catch and emits typed status codes; the plugin-host fault handler (`SOUXMAR_E_PLUGIN_FAULT`) catches anything that escapes.
- **Risk:** Plugin C004 skew on v1.8. **Mitigation:** same gate as ADR-0037 — C004 covers the version-skew matrix.

## Pre-mortem (one year from today)

It is 2027-05-12 and the feature-ops ratchet went badly. Most likely failure mode (per RFC-005 §Pre-mortem): face-ID drift across OCCT version bumps causes a fillet to attach to the wrong edge after a kernel update. We catch it in v1.2.3 with the fingerprint heuristic, but the user perception of "my bracket has the wrong corner rounded" sticks. Adding the fingerprint should have been v1.0 of `design.yaml` rather than v1.0.x.

Less-likely failure mode: parameter validation gaps let users set `bracket_height = 0`, which reaches OCCT's `BRepPrimAPI_MakePrism` and throws an unhelpful exception. Mitigation: host-side parameter validation (PR 6 of Sprint 30); covered in RFC-005's open questions.

Leading indicators:

- "Wrong edge filleted" or "feature attached to wrong face" user reports.
- OCCT version bumps without a regression suite run on a corpus of v1.2 designs.
- Any feature failing in > 1% of dogfood regenerates.

## References

- ADR-0008 — ABI v1 final freeze + ratchet rules.
- ADR-0037 / ADR-0038 / ADR-0039 / ADR-0040 — preceding ratchets.
- ADR-0012 — per-face-tag ratchet; also extended an existing frozen header.
- RFC-005 (`docs/rfcs/0005-feature-tree.md`) — the gating RFC.
- RFC-003 / ADR-0039 — BREP session this ratchet extends.
- RFC-004 / ADR-0040 — sketches consumed as feature inputs.
- `include/souxmar-c/brep.h` — the header this ratchet extends.
- `scripts/check-frozen-headers.sh` — CI gate; "extends existing header" path uses the same marker as "adds new header."

## History

- 2026-05-11 (Sprint 6 push 4): first ratchet — `reader.*` (v1.0 → v1.1).
- 2026-05-11 (Sprint 7 push 3): second — mmap-backed buffer (v1.1 → v1.2).
- 2026-05-11 (Sprint 9 push 2): third — per-face-tag, ADR-0012 (v1.2 → v1.3); **first ratchet to extend an existing frozen header**.
- 2026-05-12 (Sprint 25 push 1): fourth — surface-stream, ADR-0037 (v1.3 → v1.4).
- 2026-05-12 (Sprint 27 push 1): fifth — field-stream, ADR-0038 (v1.4 → v1.5).
- 2026-05-12 (Sprint 28 push 1): sixth — BREP session, ADR-0039 (v1.5 → v1.6).
- 2026-05-12 (Sprint 29 push 1): seventh — 2D sketch, ADR-0040 (v1.6 → v1.7).
- 2026-05-12 (Sprint 30 push 1): **eighth ratchet — BREP feature ops, this ADR** (v1.7 → v1.8). Proposed; gates on RFC-005 acceptance. Second ratchet to extend an existing frozen header (after ADR-0012).
