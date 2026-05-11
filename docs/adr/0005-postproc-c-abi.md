# ADR-0005: Postproc capability — new C ABI surface, not a solver extension

- **Status:** Accepted
- **Date:** 2026-05-11
- **Author:** souxmar core team
- **Deciders:** core, plugin-host, AI
- **Tier:** 2 (standard)
- **Affects:** ABI, plugin SDK, AI tools

## Context

The Sprint 4 push 3 agent tool catalogue named `compute_field` as one of
the v1 tools (per docs/AI_INTEGRATION.md): "Calls postproc.* plugin
(e.g. von Mises from stress)." The realization in Sprint 5 push 2
shipped it as a stub returning `NOT_AVAILABLE` because no input-field
path existed across the C ABI: the solver vtable takes only mesh +
value bag + options + out_field — there is no `input_field` parameter
through which a postproc plugin could read its source data.

Sprint 5 push 3 lands the real implementation. We have three credible
designs; the ADR records why we picked the dedicated-vtable option.

The freeze candidacy of `include/souxmar-c/` (ADR-0004) is the
constraint that disqualifies the obvious "just add an input field to
the solver vtable" approach: any change to the solver function pointer
signature is an ABI break for every solver plugin in existence (today
that's exactly zero, but conceptually any plugin already targeting
solver.* would have to recompile).

## Decision

We add a new C ABI capability surface: **postproc**. New header
(`include/souxmar-c/postproc.h`) declaring `souxmar_postproc_vtable_t`
with `compute_fn(mesh, input_field, value_bag, options, &out_field,
user_data)`. New registry entry `souxmar_registry_add_postproc(...)`.
New dispatcher route in `RegistryDispatcher` for the `postproc.*`
capability-id namespace. The first reference postproc plugin
(`postproc.scalar_magnitude`) ships in the same push.

The solver C ABI is unchanged. Existing solver plugins (zero today,
some tomorrow) continue to compile and run unmodified.

## Alternatives considered

### Add an `input_field` parameter to the solver vtable

The most "obvious" option — postproc IS a solver that happens to read
a field. Rejected: any change to a function-pointer signature in a
v-table is an ABI break. We are 1–2 sprints away from declaring v1
frozen; adding a breaking change now is the wrong precedent for the
ratchet policy in ADR-0004 to absorb. Even if the cost today is zero
plugins, the lesson "we'll just break things before freeze" is
expensive in the long run.

### Smuggle the input field through the value bag as a synthetic key

Repurpose `souxmar_value_t` to carry an opaque field handle under a
reserved key like `__postproc_input_field__`. Rejected: Value is
strongly typed (null/bool/number/string/list/map/stage) and a plugin
would need a host-side helper to extract a handle from it. That helper
is itself new ABI surface — the only difference is whether the surface
is dressed as a vtable parameter or a value-bag accessor. The vtable
parameter is the clearer contract.

### Subprocess postproc (no C ABI at all)

Run postproc plugins in a subprocess and serialize the field over a
pipe. Rejected as a v1 path: subprocess plugins are great for hostile
isolation (Sprint 7+ hardening) but the per-call cost is enormous
relative to today's in-process pattern, and we don't have the
mmap-buffer infrastructure yet to make the serialization affordable
for large fields (ADR-0006 is the reserved slot for that work).

## Consequences

### Positive

- Existing solver plugins are untouched. The ABI ratchet stays clean.
- Postproc has a clear, narrow contract: field-in / field-out, with
  the mesh as context. Plugins that don't need the input field (rare —
  arguably misnamed as postproc anyway) belong in `solver.*`.
- The agent tool `compute_field` becomes real without any ABI change
  to the existing surfaces.
- `RegistryDispatcher`'s namespace routing extends linearly: every new
  capability kind is a new prefix + a new dispatch helper, with the
  same code shape.

### Negative

- We've added a fourth namespace (`postproc.*`) to the routing table
  and a fourth variant to `CapabilityEntry::payload`. The match
  statements in `dispatch_*`, `find_*`, and `add_*` grow proportionally
  — a real (modest) source of new test surface.
- Naming is now load-bearing. A plugin author who registers
  `postproc.heat_relaxation` cannot mid-version decide to call it
  `solver.heat_relaxation` without a deprecation cycle.

### Risks

- **Risk:** A capability authored as `postproc.*` later turns out to
  need iterative refinement (multiple field-in / field-out steps), at
  which point the synchronous `compute_fn` is the wrong shape.
  **Mitigation:** Adding a streaming variant `compute_stream_fn` is an
  additive change to the postproc vtable, same ratchet rule as ADR-0004.
- **Risk:** Future ABI revisions want to merge `solver.*` and
  `postproc.*`. **Mitigation:** This is a v2-ABI conversation. We
  document the postproc/solver distinction now; v2 is free to
  reorganize.

## References

- `include/souxmar-c/postproc.h` — vtable + options struct.
- `include/souxmar-c/registry.h` — `souxmar_registry_add_postproc`.
- `src/pipeline/registry_dispatcher.cpp` — `dispatch_postproc`.
- `examples/plugins/scalar-magnitude/` — first reference postproc plugin.
- `src/ai/tools/compute_field.cpp` — agent tool consuming this ABI.
- ADR-0001 — original C ABI for plugins.
- ADR-0004 — plugin conformance suite + ABI freeze process.

## History

- 2026-05-11: Proposed, accepted (Sprint 5 push 3).
