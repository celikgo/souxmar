# BUG-2026-05-12-001 — cfd-stub ignores per-patch boundary conditions

- **Reporter:** internal (Sprint 10 carry-over)
- **Filed:** 2026-05-12 (Sprint 13 push 4)
- **Fixed in:** 2026-05-12 (this push)
- **Closed:** 2026-05-12
- **Priority:** P2 (didn't block the public alpha; affected pipe-bend
  example accuracy; carried over from Sprint 10's retro action list)
- **Surface affected:** `examples/plugins/cfd-stub/`
- **First version with the fix:** v0.9.1 (Sprint 13 push 5 cuts the tag)

## What this document is

The Sprint 13 push 4 commit lands the cfd-stub per-patch BC routing
fix that has been on the carry-over list since Sprint 10's retro
("CFD-stub's per-patch BC routing carry-over from Sprint 10"). The
ADR-0017 triage model treats *every* engine-side fix the same — whether
it came from an external bug report or from an internal carry-over —
so this document is what an external user's bug report's record
*would* look like once the v0.9.0 alpha generates real reports.

It also seeds the regression-tracking habit: every fix that touches
behaviour an external user could observe gets a brief note here,
named after the date + a sequential digit, linkable from the triage
workflow.

## Symptom

```yaml
# pipeline excerpt that produces wrong output
stages:
  - id: cfd
    plugin: solver.cfd.simple
    input:
      mesh: { from: mesh }
      velocity_magnitude: 1.0
      patches:
        - name: walls
          tag: 7
          bc: { type: wall }
        - name: inlet
          tag: 9
          bc:
            type: inlet
            velocity: [4.0, 0, 0]
```

**Expected** (per the documented input schema): velocity at nodes
on the `walls` patch is `(0, 0, 0)` and at nodes on the `inlet`
patch is `(4, 0, 0)`.

**Actual** (in v0.9.0): velocity at every node is the bulk default
`(magnitude, 0, 0)` — the `patches` block was silently ignored.

## Root cause

`examples/plugins/cfd-stub/cfd_stub.cpp` read `velocity_magnitude`
and `flow_direction` from the input map but never read `patches`.
The Sprint 8 push 2 stub was a uniform-flow shortcut for the eval
suite; the per-patch routing was queued but never implemented.

## Fix

Sprint 13 push 4 (this commit). The cfd-stub now:

1. Reads `patches: [{ name, tag, bc }, ...]` from inputs.
2. Walks the mesh's per-cell face tags via the v1.3 plugin C ABI
   (`souxmar_mesh_face_tag`).
3. For each cell + local face whose tag matches a patch, looks up
   the local face's nodes via the canonical Gmsh/VTK/OpenFOAM
   numbering for tet4 / hex8 element types.
4. Applies the patch's BC to those nodes with precedence
   `wall > inlet > outlet > bulk`. Wall-dominance is the
   conservative choice for a stub solver (a real solver would
   disambiguate by face orientation; the stub conservatively pins
   wall-adjacent corners to zero).

Tested by a new integration test
(`tests/integration/test_cfd_stub.cpp` →
`CfdStubEndToEnd.PerPatchBcRoutingAppliesWallAndInlet`) that
stamps face tags on a single-tet mesh + drives the solver +
asserts the routing precedence at four nodes.

## What stayed the same

- Default behaviour with no `patches` input is unchanged — same
  uniform-flow output that the existing eval cases exercise.
- `flow_direction` continues to apply when patches are absent.
- The plugin C ABI is unchanged; no new entry points needed.

## What the fix does NOT cover

- **Non-tet/hex element types** (prism, pyramid, higher-order)
  have no face-vertex table yet; cells of those types skip the
  routing loop and fall back to bulk. Adding the tables is a
  Tier-0 change; queued for Sprint 14 once the surface mesher
  emits those types in a default-CI configuration.
- **Curved boundary corners.** The wall-dominance rule pins
  corner nodes to zero; a real CFD solver disambiguates by
  surface mesh edge orientation. The stub's structure is
  correct; the stub's physics is not.
- **Pressure outlets with explicit pressure values.** The
  `outlet` routing today just passes the bulk flow through —
  pressure is not stored. Pressure-outlet physics requires the
  fenicsx- or OpenFOAM-backed adapter sibling, not the stub.

## Related artefacts

- Sprint 10 retro § "what to fix" — the original action item.
- ADR-0012 (per-face-tag C ABI ratchet) — the v1.3 face-tag API
  this fix consumes.
- ADR-0017 (public-alpha bug-discovery model) — triage process
  this document seeds.
- New test:
  `tests/integration/test_cfd_stub.cpp::CfdStubEndToEnd.PerPatchBcRoutingAppliesWallAndInlet`

— Sprint 13 push 4.
