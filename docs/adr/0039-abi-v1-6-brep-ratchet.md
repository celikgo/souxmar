# ADR-0039: BREP-session C ABI ratchet (v1.5 → v1.6)

- **Status:** Proposed
- **Date:** 2026-05-12 (Sprint 28 push 1)
- **Author:** celikgokhun
- **Deciders:** core, plugin-host, adapters, desktop, AI, platform, DX
- **Tier:** 3 (heavy / requires RFC — frozen-header ratchet + new plugin type)
- **Affects:** `include/souxmar-c/brep.h` (new), `include/souxmar-c/abi.h`
  (`SOUXMAR_ABI_VERSION_MINOR` bump 5 → 6 + history line), `docs/PLUGIN_SDK.md`
  (new `cad.*` plugin type), conformance suite (new `cad.*` type + STEP round-trip
  fixtures), `examples/plugins/cad-occt` (first conforming plugin), `examples/plugins/occt-reader`
  (becomes a thin wrapper), `docs/LICENSING.md` (new — OCCT relink-rights story).

## Context

Sprint 28 unblocks parametric authoring (Sprints 29–30) by standing up a live CAD kernel session. RFC-003 picks OCCT 7.8 LTS as the in-tree kernel under LGPL-2.1 with the OCCT linking exception, dynamic-link only. The existing `souxmar_geometry_t` (discrete vertex/edge/face/solid tags) is sufficient for downstream meshers but cannot host the BREP topology + analytic-surface metadata the sketcher and feature tree need. The choice is between:

1. Extend `geometry.h` with kernel-specific accessors — couples every future kernel to that surface; rejected.
2. Add a new opaque BREP-session handle as a separate ABI surface — keeps kernel state behind the plugin boundary; selected.

This is structurally similar to ADR-0037 / ADR-0038 (derived-view pattern), but with one important difference: **the BREP session is mutating**, not read-only. Feature operations (extruded geometry, fillets, booleans) modify the session graph between calls. The session is a *plugin-owned mutating handle*, not a host-cached view.

RFC-003 carries the architectural rationale (kernel choice, licensing, plugin contract); this ADR carries the binding declaration that the ratchet has been considered, sized, and reviewed under the ADR-0008 process.

## Decision

The C ABI gains **one new header** (`include/souxmar-c/brep.h`), **one new opaque handle type** (`souxmar_brep_session_t`), and **eleven new function declarations** covering lifecycle, STEP/IGES import, geometry-snapshot export, body queries, and tessellation. A new plugin type `cad.*` is introduced — distinct from `reader.*` because the session is stateful. `SOUXMAR_ABI_VERSION_MINOR` bumps from **5** to **6** with a new history line in `abi.h`. No existing declaration moves; strict additive minor ratchet per ADR-0008. Sits on top of ADR-0037 (v1.4) and ADR-0038 (v1.5).

### New surface

See RFC-003 §Proposal for the verbatim header. Eleven entry points: `souxmar_brep_session_open` / `_close`, `_import_step` / `_import_iges`, `_to_geometry`, `_num_bodies` / `_body_id` / `_body_bounds` / `_body_name`, `_tessellate`. Feature operations (extrude, fillet, etc.) are **not** in this ratchet — they land in ADR-0041 (v1.8) once the sketcher (RFC-004 / ADR-0040) gives them sketch inputs to consume.

### `abi.h` history entry

```
 *   v1.6  Sprint 28 push 1 — BREP session surface + cad.* plugin type
 *                            (souxmar-c/brep.h); ADR-0039.
```

`#define SOUXMAR_ABI_VERSION_MINOR 5` becomes `#define SOUXMAR_ABI_VERSION_MINOR 6`.

### Host-side implementation contract

- **Sessions are plugin-owned.** The host calls `souxmar_brep_session_open` on the `cad.*` plugin's vtable; the returned handle is opaque to the host. Calls into the session round-trip through the plugin's vtable.
- **One session per thread.** OCCT's kernel state is per-thread; sessions cannot be passed across threads. Multiple sessions in one process are allowed (the desktop opens one per loaded project).
- **Body IDs are stable within a session.** Re-importing the same STEP file into the same session gives the same body IDs; importing into a fresh session does not (the IDs are session-local).
- **Snapshot is the only legal output to non-cad plugins.** `souxmar_brep_to_geometry` produces a discrete `souxmar_geometry_t` consumable by `mesher.*`, `solver.*`, etc. The plugin host enforces that only `cad.*` plugins ever see the live session handle.
- **Tessellation deflection is in model units.** Default at the host: `bbox_diagonal × 0.01` (linear), `0.5 rad` (angular); the host derives the linear default from the body bounds. Plugins may override.

### What stays out of scope

- **Feature operations.** Deferred to ADR-0041 (v1.8). Splitting kernel-import from feature-create keeps the v1.6 surface reviewable in one pass.
- **Stable IDs across kernel-version bumps.** Promising stable face/edge IDs across OCCT bumps is out of scope; RFC-005 layers a fingerprint heuristic on top in v1.8.
- **Memory caps.** A `souxmar_brep_session_set_memory_cap` accessor is desirable for large STEP imports; deferred to a v1.6.1 ratchet if a real OOM event surfaces in dogfood.
- **Multi-body assemblies.** The session supports multiple bodies (the BREP graph is multi-rooted), but assembly-level mates and joints are a much larger problem deferred post-v1.3.

## Alternatives considered

### Extend `geometry.h` with BREP-topology accessors

Pro: keeps the kernel concept on the existing handle. Con: forces every consumer of `souxmar_geometry_t` (CLI, Python, meshers, solvers) to know about live kernel sessions, even when they only want the discrete snapshot. The session is fundamentally a *plugin-state* concept; the discrete geometry is the *handoff*. Conflating them costs every downstream consumer.

### Make `cad.*` an extension of `reader.*`

Pro: fewer plugin types. Con: readers are one-shot — they produce a snapshot and forget. The kernel session lives for the duration of the project's design phase and accepts mutating calls. Forcing the cad concept into the reader vtable would either bloat the reader API with `is_mutable` flags (ugly) or compromise the session model (worse).

### Skip OCCT; use a discrete kernel (Manifold / CGAL)

Per RFC-003 §Alternatives A and B — rejected. Manifold is triangle-mesh-only; CGAL's BREP is discrete-only. Neither is a parametric CAD kernel.

### Static-link OCCT

Per RFC-003 §Alternative D — rejected. The OCCT linking exception preserves downstream relink rights; static linking removes that right de facto.

### Skip the ratchet; carry the BREP through the value-bag

Same shape as ADR-0037's value-bag rejection. The session is a live, long-lived handle — modeling it as a JSON blob in the agent's value-bag would require serializing the entire OCCT BREP graph on every call. Catastrophic perf cost.

## Consequences

### Positive

- **Sprints 29 (sketch anchor) and 30 (feature ops) have a stable target.** Sketches anchor to BREP faces (RFC-004); features mutate the session (RFC-005). Both land on this v1.6 surface.
- **`reader.*` capability preserved.** `examples/plugins/occt-reader` becomes a thin wrapper (open → import → snapshot → close), so any existing pipeline that names `reader.step` keeps working unchanged.
- **Mesher refinement on curved surfaces.** The optional `cad_session` / `cad_body_id` mesher value-bag keys (RFC-003 §Mesher input) let Gmsh refine on the analytic surface rather than the tessellated approximation — meaningful quality win for curved geometry.
- **Documented stateful-handle pattern.** First mutating opaque handle in the v1 ABI. Future stateful surfaces (e.g., a meshing session for interactive remeshing) inherit the pattern.

### Negative

- **OCCT distribution overhead.** ~80 MB to the installer pre-prune, ~40 MB post-prune. Real cost; tracked against installer-size budget.
- **LGPL relink-rights obligation.** New release-process step (publish OCCT build instructions + reproducible OCCT binary). Folded into `docs/LICENSING.md` and the `releasing-souxmar` skill.
- **Threading constraint is sharper than other ABI surfaces.** One-session-per-thread is OCCT's reality; the host dispatcher enforces it.
- **Plugin type proliferation.** First `cad.*` plugin; sets a precedent for future kernel types (`cad.solvespace`, etc.). Each future kernel binding ships as a plugin against this same v1.6 surface.

### Risks

- **Risk:** OCCT upstream regression on assembly STEP files. **Mitigation:** Conformance corpus includes multi-body STEP fixtures from the start; OCCT version bumps run the regression suite (PR 1's test plan).
- **Risk:** Snapshot latency dominates for large assemblies (re-snapshot on every viewport refresh stalls the renderer). **Mitigation:** RFC-003 pre-mortem flags this; incremental-snapshot follow-up in v1.6.1 if 500ms p95 budget exceeded on a 100-body assembly.
- **Risk:** LGPL compliance gap (we ship a build that statically links OCCT by accident). **Mitigation:** release-process check verifies dynamic linking on each OS image; documented in `releasing-souxmar` skill.
- **Risk:** Plugin C004 skew on v1.6. **Mitigation:** same gate as ADR-0037 — C004 already covers the version-skew matrix.

## Pre-mortem (one year from today)

It is 2027-05-12 and the BREP-session ratchet went badly. Most likely failure mode: OCCT 7.8's STEP importer has a corner-case regression on assembly files from a popular mid-market CAD tool; a user imports their production model on day one of v1.2 and gets an empty body graph silently. We pin 7.9 in v1.2.1 with the patch, but the launch-week first-impression damage is done. The fix shape is already known — bump OCCT, run the regression corpus — but the "bump OCCT" cadence wasn't established in time.

Less-likely failure mode: the snapshot path turns out to be the dominant cost on large assemblies, and the deferred incremental-snapshot work doesn't land until v1.3 because the team is pulled onto modeling. Renderer stutter on parametric edit is the visible symptom.

Leading indicators:

- Any "empty body graph after import" user report.
- Snapshot latency p95 on a 100-body assembly > 500ms.
- No "bump OCCT" PR within 30 days of an OCCT upstream release.
- Conformance corpus contains only single-body STEP files.

## References

- ADR-0008 — ABI v1 final freeze + ratchet rules.
- ADR-0037 — surface-stream ratchet (v1.3 → v1.4); pattern precedent.
- ADR-0038 — field-stream ratchet (v1.4 → v1.5); pattern precedent.
- ADR-0009 — OpenFOAM process-isolation pattern; precedent for "third-party dependency as a plugin."
- RFC-003 (`docs/rfcs/0003-cad-kernel.md`) — the gating RFC.
- RFC-004 / ADR-0040 — sketcher; anchors to BREP faces from this surface.
- RFC-005 / ADR-0041 — feature operations; extends `brep.h` at v1.8.
- `include/souxmar-c/abi.h` — file under ratchet.
- `include/souxmar-c/geometry.h` — the discrete snapshot target type.
- `scripts/check-frozen-headers.sh` — CI gate.

## History

- 2026-05-11 (Sprint 6 push 4): first ratchet — `reader.*` (v1.0 → v1.1).
- 2026-05-11 (Sprint 7 push 3): second — mmap-backed buffer (v1.1 → v1.2).
- 2026-05-11 (Sprint 9 push 2): third — per-face-tag, ADR-0012 (v1.2 → v1.3).
- 2026-05-12 (Sprint 25 push 1): fourth — surface-stream, ADR-0037 (v1.3 → v1.4).
- 2026-05-12 (Sprint 27 push 1): fifth — field-stream, ADR-0038 (v1.4 → v1.5).
- 2026-05-12 (Sprint 28 push 1): **sixth ratchet — BREP session + cad.* plugin type, this ADR** (v1.5 → v1.6). Proposed; gates on RFC-003 acceptance.
