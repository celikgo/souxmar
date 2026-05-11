# ADR-0007: ABI v1 freeze-candidate declaration

- **Status:** Superseded by [ADR-0008](0008-abi-v1-final-freeze.md) — soak completed cleanly, freeze declared formal in Sprint 7 push 1.
- **Date:** 2026-05-11
- **Author:** souxmar core team
- **Deciders:** core, plugin-host, AI, DX
- **Tier:** 3 (heavy / requires RFC)
- **Affects:** ABI, governance, plugin SDK, release process

## Context

[ADR-0001](0001-c-abi-for-plugins.md) committed souxmar to a stable C
ABI as the plugin contract. [ADR-0004](0004-plugin-conformance-suite.md)
defined the conformance suite that gates ABI stability claims. Five
sprints of incremental work have grown that ABI to its v1 surface:

- ABI version macros + symbol export (`abi.h`)
- Status codes (`status.h`)
- Plugin entry point + host info (`plugin.h`)
- Registry registration (`registry.h`)
- Capability vtables (`mesher.h`, `solver.h`, `writer.h`, `postproc.h`)
- Opaque handles + accessors (`mesh.h`, `geometry.h`, `field.h`,
  `value.h`)
- Opaque type forwards (`types.h`)
- Bulk transfer buffers (`buffer.h`)

The Sprint 5 plan's exit criterion was explicit:

> `include/souxmar-c/` declared **frozen-candidate v1**. Two-sprint
> freeze period begins.

This ADR is the binding declaration.

## Decision

We declare `include/souxmar-c/` **frozen-candidate v1** as of
**2026-05-11**. The two-sprint soak period runs through the end of
Sprint 7, **ending 2026-06-08** (assuming the standard two-week sprint
cadence). If the soak completes without a breaking change to any v1
surface, we issue a formal **ABI v1 frozen** declaration on
2026-06-08; the v1 surface is then immutable for the entire 1.x
release series.

### What is frozen

Every header in `include/souxmar-c/`, specifically:

| Header        | What it freezes                                               |
| ------------- | ------------------------------------------------------------- |
| `abi.h`       | `SOUXMAR_ABI_VERSION_MAJOR=1`, export macros, C-linkage helpers |
| `status.h`    | Status code numeric values + the `souxmar_status_t` struct layout |
| `types.h`     | Opaque handle typedefs (`souxmar_mesh_t`, `_geometry_t`, `_field_t`, `_value_t`, `_buffer_t`, `_topology_t`) |
| `plugin.h`    | `souxmar_plugin_register_v1` signature + `souxmar_host_info_t` layout |
| `registry.h`  | `souxmar_registry_add_*` signatures                            |
| `mesher.h`    | `souxmar_mesher_vtable_t` layout + `_options_t`                |
| `solver.h`    | `souxmar_solver_vtable_t` layout + `_options_t`                |
| `writer.h`    | `souxmar_writer_vtable_t` layout                               |
| `postproc.h`  | `souxmar_postproc_vtable_t` layout + `_options_t`              |
| `mesh.h`      | All `souxmar_mesh_*` function signatures + `SOUXMAR_ET_*` integer values + `souxmar_mesh_buffers_t` layout |
| `geometry.h`  | `souxmar_geometry_*` signatures + `SOUXMAR_GK_*` integer values |
| `field.h`     | `souxmar_field_*` signatures + `SOUXMAR_FL_*` / `SOUXMAR_FK_*` integer values |
| `value.h`     | `souxmar_value_*` signatures + `SOUXMAR_VK_*` integer values   |
| `buffer.h`    | `souxmar_buffer_*` signatures + alignment guarantee            |

What's frozen, specifically:

- **Function names + signatures.** Renaming, adding parameters,
  reordering parameters, or changing return types is a break.
- **Struct layouts.** Adding fields to a struct that plugin code
  values directly (not just through pointer) is a break. Existing
  reserved/padding fields are honored.
- **Numeric constants.** Any `SOUXMAR_*` macro that's part of the
  serialised format or that plugins compile against (e.g.
  `SOUXMAR_ET_TET4 = 9`) cannot change value.
- **Symbol export contracts.** Plugins resolve symbols by name; renamed
  symbols would orphan every shipped plugin binary.

### What is NOT frozen

- Implementation behaviour behind the ABI. Performance, internal data
  structures, internal error message text — all live under
  `src/` and may change freely.
- C++ public headers under `include/souxmar/` (the host-side surface).
  Plugins do not see these; they are versioned independently.
- The plugin manifest schema (`souxmar-plugin.toml`). That has its
  own forward-compatibility story.
- Anything not under `include/souxmar-c/`.

### What is allowed during soak

The ratchet from ADR-0004 applies in full:

1. **New checks** in the conformance suite are allowed if and only if
   they cannot make a previously-passing plugin fail.
2. **New additive surfaces** — new headers, new function declarations,
   new SOUXMAR_X_* constants where X is a fresh prefix, new optional
   fields appended to *forward-compatible* structs (with zero-init
   semantics). These bump `SOUXMAR_ABI_VERSION_MINOR` from 0 to 1.
3. **Bug fixes** to documentation, comments, or non-load-bearing
   declaration details (e.g. the `souxmar_value_t` typedef gap fixed
   in Sprint 5 push 4 — restoring the typedef did not change anything
   a pre-typedef plugin had relied on, so it's a fix, not a break).

### What cancels the candidacy

Any of:

1. A breaking change to a v1 surface enters `main` during soak.
2. A discovered design flaw forces a non-backward-compatible fix.
3. Conformance suite C001–C010 starts failing on any in-tree plugin
   on `main` for two consecutive nights.

Cancellation resets the soak clock. We issue a public note describing
the cancellation reason, the design adjustment, and the new
candidate's expected freeze date.

### Mechanics of formal freeze

On 2026-06-08, assuming clean soak:

1. Re-run the conformance suite on `main` against every in-tree plugin
   — must pass.
2. Re-run ASAN / TSAN nightly for three consecutive nights — must be
   clean.
3. Run the perf-nightly benchmark against the baseline established
   during Sprint 5 push 5 — must be within threshold.
4. Update `abi.h`'s status comment from "frozen-candidate" to
   "frozen", and `SOUXMAR_ABI_FREEZE_CANDIDATE` is removed.
5. Tag a release on `main` titled `abi-v1-frozen`.

Post-freeze, any breaking change to v1 surfaces requires an
`SOUXMAR_ABI_VERSION_MAJOR = 2` bump. v2 ABI work happens on a side
branch with a parallel-load story for plugin migration.

## Alternatives considered

### Skip the soak — declare frozen immediately

Pro: faster shipping. Con: every previous freeze of a stable C ABI in
adjacent projects (FreeCAD's ABI, Gmsh's plugin contracts, Blender's
Python API) discovered at least one design regret in the first few
weeks of broad use that *would have been caught* by a soak period.
The soak's whole point is to surface those regrets while they're
still cheap.

### Soak for longer (4 sprints, 6 sprints)

Pro: more confidence. Con: more committed surface area without the
benefit of plugin-author feedback. The plugins exist (5 in-tree, all
conformance-passing) but third-party adoption is the real test. Two
sprints is the minimum that lets the next sprint's work (Gmsh
adapter) prove out the ABI under a non-toy plugin without dragging
the freeze beyond the v1.0 release window.

### Freeze a subset (just `abi.h`, `types.h`, `mesher.h`)

Pro: smallest commitment. Con: the value of a freeze is that plugin
authors don't have to track multiple stability surfaces. A plugin
that uses `solver.h` cannot ignore the stability of `field.h` —
together they're its contract. Half-frozen ABIs are why FreeCAD
plugin authors keep getting surprised between FreeCAD 0.x releases.

## Consequences

### Positive

- Plugin authors get a date they can commit to. "v1 freezes
  2026-06-08" is a planning input the project hasn't been able to
  give before.
- The Sprint 6 adapter work (Gmsh, OpenCASCADE) lands against a
  formally-stable ABI. If the soak surfaces something the adapters
  expose, the canceling mechanism gives us a clean rollback.
- The conformance suite + ratchet policy + freeze-candidate marker
  collectively make the ABI a binding contract, not a wish.

### Negative

- The soak constrains what we can do in Sprint 6 and Sprint 7. New
  abilities have to fit the existing ABI shape or wait for v1.1
  (additive minor) or v2 (breaking).
- A cancellation costs time — somewhere between two and four weeks of
  re-soak. Discipline during soak matters.

### Risks

- **Risk:** Third-party plugin authors begin shipping against the
  candidate ABI before formal freeze. **Mitigation:** Document
  candidate-vs-frozen status prominently; ship the
  `SOUXMAR_ABI_FREEZE_CANDIDATE` macro so plugin code can detect the
  state at compile time.
- **Risk:** The Sprint 6 Gmsh adapter discovers a missing ABI surface
  (e.g. parallel-piece mesh ingest). **Mitigation:** Add the surface
  as a minor bump (SOUXMAR_ABI_VERSION_MINOR → 1). Only a clash with
  an existing v1 declaration cancels the candidacy.
- **Risk:** A latent bug similar to the `souxmar_value_t` typedef gap
  surfaces during soak. **Mitigation:** The ratchet allows
  non-breaking fixes; document each one in the release notes so
  downstream consumers can audit.

### Pre-mortem (one year from today)

It is 2027-05-11 and the ABI freeze went badly. The most likely
failure mode: the Gmsh adapter shipped against the candidate and a
shape-mismatch in `souxmar_mesh_from_buffers` was discovered after
formal freeze. We accepted the bug rather than break ABI v1, which
forced a workaround in every consumer.

Leading indicators we should watch:

- The Sprint 6 adapter work surfacing buffer-format edge cases that
  weren't caught in `tests/unit/test_c_abi_buffer.cpp`.
- Third-party plugin issues filed against the candidate ABI in the
  first two weeks of soak (target: zero showstoppers).
- Nightly conformance suite skipping any check on `main` (target:
  100% pass rate across all in-tree plugins).

## References

- ADR-0001 — original C ABI for plugins.
- ADR-0004 — plugin conformance suite + ratchet rule.
- ADR-0005 — postproc C ABI (the most recent surface addition).
- ADR-0006 — memory-mapped large-buffer protocol (v1 heap landed).
- `include/souxmar-c/*.h` — the headers under freeze.
- `tests/integration/test_conformance.cpp` — the freeze gate.
- `docs/GOVERNANCE.md` § Release process — formal freeze process.

## History

- 2026-05-11: Proposed, accepted (Sprint 5 push 6). Soak begins.
- 2026-06-08 (target): Formal freeze on clean soak completion.
