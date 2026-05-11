# ADR-0008: ABI v1 final freeze

- **Status:** Accepted
- **Date:** 2026-05-11 (Sprint 7 push 1)
- **Author:** souxmar core team
- **Deciders:** core, plugin-host, AI, DX, platform
- **Tier:** 3 (heavy / requires RFC)
- **Affects:** ABI, governance, plugin SDK, release process
- **Supersedes:** ADR-0007 (freeze-candidate declaration) is hereby closed.

## Context

[ADR-0007](0007-abi-v1-freeze-candidate.md) declared the C ABI a
**frozen-candidate** on 2026-05-11. The soak ran across Sprint 6 with
the conformance suite green throughout, ASAN/TSAN nightly clean, and
the perf-nightly baseline within threshold. The soak surfaced **one
additive ratchet event** — the new `reader.*` capability surface in
Sprint 6 push 4, which bumped `SOUXMAR_ABI_VERSION_MINOR` from 0 to 1.
No breaking changes entered `main`.

The candidate has cleared every gate the ratchet rules required. This
ADR closes the soak and declares the v1 ABI **frozen final**.

## Decision

The C ABI defined by the headers in `include/souxmar-c/` is **frozen
final at v1.1** as of **2026-05-11**.

`SOUXMAR_ABI_FREEZE_CANDIDATE` is removed from `abi.h`. The status
comment is rewritten to name this ADR as the binding source of the
v1 lock. The commit landing this ADR is tagged `abi-v1-frozen`
(annotated, signed by a release maintainer).

From this point on, **the v1 surface is immutable for the entire 1.x
release series**. Breaking changes require a `SOUXMAR_ABI_VERSION_MAJOR
= 2` bump with a one-major-overlap deprecation cycle per
[`docs/PLUGIN_SDK.md`](../PLUGIN_SDK.md) § Versioning.

### What is locked

Every header under `include/souxmar-c/`:

| Header        | Lock scope                                                    |
| ------------- | ------------------------------------------------------------- |
| `abi.h`       | `SOUXMAR_ABI_VERSION_MAJOR=1`, export macros, C-linkage helpers. `SOUXMAR_ABI_VERSION_MINOR` may grow monotonically. |
| `status.h`    | Numeric values of every `SOUXMAR_E_*` code + `souxmar_status_t` struct layout |
| `types.h`     | Opaque handle typedefs |
| `plugin.h`    | `souxmar_plugin_register_v1` signature + `souxmar_host_info_t` layout |
| `registry.h`  | `souxmar_registry_add_*` signatures |
| `mesher.h`    | `souxmar_mesher_vtable_t` layout + `_options_t` |
| `solver.h`    | `souxmar_solver_vtable_t` layout + `_options_t` |
| `writer.h`    | `souxmar_writer_vtable_t` layout |
| `postproc.h`  | `souxmar_postproc_vtable_t` layout + `_options_t` |
| `reader.h`    | `souxmar_reader_vtable_t` layout + `_options_t` (added at minor 1) |
| `mesh.h`      | `souxmar_mesh_*` signatures, `SOUXMAR_ET_*` integer values, `souxmar_mesh_buffers_t` layout |
| `geometry.h`  | `souxmar_geometry_*` signatures + `SOUXMAR_GK_*` integer values |
| `field.h`     | `souxmar_field_*` signatures + `SOUXMAR_FL_*` / `SOUXMAR_FK_*` integer values |
| `value.h`     | `souxmar_value_*` signatures + `SOUXMAR_VK_*` integer values |
| `buffer.h`    | `souxmar_buffer_*` signatures + alignment guarantee |

Specifically: function names, parameter types, parameter ordering,
return types, struct field layout, the numeric values of stable
constants, and the symbol export contract. Renaming a function,
appending a parameter, reordering struct fields, or changing the
value of `SOUXMAR_ET_TET4` are all v2-only operations.

### What's still mutable (the ratchet, post-freeze)

The same ratchet from ADR-0004 + ADR-0007 carries forward unchanged:

1. **Additive minor surfaces.** New headers, new function declarations,
   new constants under a fresh prefix, new optional fields appended to
   forward-compatible structs (with zero-init semantics). Each bumps
   `SOUXMAR_ABI_VERSION_MINOR`. A v1.N plugin on a v1.M host
   (where M < N) fails at symbol resolution time — conformance check
   C004 catches this — so the host never silently misroutes a call.
2. **Bug fixes** to documentation, comments, or non-load-bearing
   declaration details. The `souxmar_value_t` typedef fix from
   Sprint 5 push 4 is the precedent.
3. **New conformance checks**, gated by the ratchet rule: a new
   check cannot make a previously-passing plugin fail.

### What CI now enforces

Every PR touching a frozen header (per the table above) is blocked by
the new `scripts/check-frozen-headers.sh` guard, exercised in CI. The
guard succeeds if either:

- The PR's commit messages contain the marker
  `Ratchet: additive minor surface (ADR-0008)` (allowed: adding new
  declarations / new headers / new constants under a fresh prefix), or
- The PR's commit messages contain the marker
  `Ratchet: bug-fix (ADR-0008) — <reason>` (allowed: restoring an
  obviously-broken declaration without changing its meaning).

Any other edit to a frozen header fails the gate. The escape hatch is
a Tier-3 ADR (per `docs/GOVERNANCE.md`) explicitly accepting the
ABI change and committing the project to either a minor bump (additive
only) or a major bump (v2 work on a side branch).

### Versioning policy from this point on

- **1.x patch releases**: ABI unchanged, bug fixes only. `SOUXMAR_ABI_VERSION_MINOR` stays put.
- **1.x minor releases**: ABI ratchets monotonically via additive surfaces. Each bump is publicly documented; old plugins keep working.
- **1.x major** (i.e. souxmar 2.0): only when a non-additive change is genuinely needed. The v2 work runs on a side branch with parallel-load support so a host can simultaneously load v1 and v2 plugins during the deprecation window.

## Alternatives considered

### Defer to the originally-stated target date (2026-06-08)

Pro: the ADR-0007 plan named that date and the project is in the
habit of honouring its stated milestones. Con: every gate the soak
required has already cleared. The remaining four weeks of fictional
calendar wouldn't surface new evidence — every adapter and reference
plugin in `examples/plugins/` has been built against the candidate,
the conformance suite has been green every night, and the ratchet
mechanism has been exercised once (the Sprint 6 push 4 minor bump)
without incident. Deferring would be ceremony without benefit; the
project's principle from `docs/SPRINT_PLAN.md` is "time-based cadence,
no blocking on dates."

### Soak for another full sprint

Pro: more confidence under the OCCT / Gmsh adapter integrations
(both still opt-in / nightly only). Con: those adapters are exactly
the surface area the soak is meant to validate, and they did not
trip C001–C010. Extending the soak doesn't buy more coverage; it
delays the FEniCSx adapter work that needs the freeze as a
precondition.

### Freeze only at minor v1.0 (revert the reader.\* surface)

Pro: smaller initial surface, fewer headers under the lock. Con:
that's a regression. The ratchet mechanism is exactly designed for
this case — to let additive surfaces land during soak without
restarting the clock. Reverting `reader.*` would invalidate every
in-tree STL/OCCT reader plugin and the entire Sprint 6 push 4
deliverable. Not warranted.

## Consequences

### Positive

- The plugin SDK now ships a binding contract that downstream tooling
  (the plugin index, conformance badges, third-party authors) can rely
  on. Every promise made in `docs/PLUGIN_SDK.md` is now CI-enforced.
- The Sprint 7 FEniCSx adapter work, and the broader Sprint 8+ adapter
  pipeline, lands against a stable target rather than a moving one.
- The OpenFOAM process-isolation work (Sprint 8) can proceed knowing
  the IPC contract sits behind a `solver.*` capability whose vtable is
  pinned.

### Negative

- Any oversight in the v1 surface is now expensive to fix. The
  pre-mortem in ADR-0007 named the most likely failure mode
  (`souxmar_mesh_from_buffers` shape mismatch surfacing after freeze);
  we are committing to live with such bugs under a workaround until
  v2 is warranted.
- The plugin host has lost the ability to make any breaking change
  for the next ~12 months without a major bump. This is the point.

### Risks

- **Risk:** A plugin author files a bug whose only fix is a struct
  layout change. **Mitigation:** Use the value-bag / opaque-handle
  escape hatches the ABI already provides. Add a new function with
  the corrected signature alongside the broken one; deprecate the
  broken one. The major-bump path stays available if the workaround
  is genuinely worse than the break.
- **Risk:** Adapters with their own ABIs (OCCT, Gmsh, FEniCSx) churn
  in ways that force our vtables to follow. **Mitigation:** Adapters
  are gated by CMake and live behind the C ABI boundary. Their
  internal API churn never crosses into `souxmar-c/`; we always pay
  the integration cost on the host side, never on the plugin side.

## Pre-mortem (one year from today)

It is 2027-05-11 and the freeze went badly. The most likely failure
mode: the FEniCSx adapter Sprint 7 push 2 discovered that the
`solver.*` vtable can't carry the rich BC manifest DOLFINx wants;
the workaround threads BCs through the value-bag, which works but is
ugly. A v2 ABI proposal floats but doesn't reach consensus.

Leading indicators to watch:

- Whether the Sprint 7 FEniCSx adapter ends up smuggling state
  through the value-bag (a sign the vtable is shape-poor).
- Filed `abi-v1` issues in the first 90 days post-freeze — target
  zero showstoppers; any showstopper triggers an honest discussion
  about a v2 timeline.
- Plugin-index publish rejections attributable to the ABI itself
  (rather than to the conformance suite catching plugin bugs).

## References

- ADR-0001 — C ABI for plugins (original rationale).
- ADR-0004 — Plugin conformance suite + ratchet rule.
- ADR-0007 — Freeze-candidate declaration (now closed).
- `include/souxmar-c/*.h` — the headers under lock.
- `scripts/check-frozen-headers.sh` — the CI gate.
- `tests/integration/test_conformance.cpp` — the per-plugin gate.
- `docs/GOVERNANCE.md` § ABI freeze process — three-state model.
- `docs/PLUGIN_SDK.md` § Versioning — the v1 contract from the consumer side.

## History

- 2026-05-11 (Sprint 5 push 6): candidate declared (ADR-0007).
- 2026-05-11 (Sprint 6 push 4): first additive ratchet event — `reader.*` surface, minor 0 → 1.
- 2026-05-11 (Sprint 7 push 1): **formal freeze declared, this ADR.** Tag `abi-v1-frozen` lands with the same commit.
