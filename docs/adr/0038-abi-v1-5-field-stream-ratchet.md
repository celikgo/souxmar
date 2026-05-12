# ADR-0038: Field-stream C ABI ratchet (v1.4 → v1.5)

- **Status:** Proposed
- **Date:** 2026-05-12 (Sprint 27 push 1)
- **Author:** celikgokhun
- **Deciders:** core, plugin-host, desktop, AI, platform, DX
- **Tier:** 3 (heavy / requires RFC — frozen-header ratchet)
- **Affects:** `include/souxmar-c/field_stream.h` (new), `include/souxmar-c/abi.h`
  (`SOUXMAR_ABI_VERSION_MINOR` bump 4 → 5 + history line), conformance suite
  (new round-trip tests), `src/desktop/src-tauri/souxmar-bridge` (downstream
  consumer), in-core implementation backing the entry points, `examples/plugins/vtu-reader`
  (new reader plugin landing in the same sprint).

## Context

Sprint 27 ships results-field visualization for the workbench (RFC-002). The renderer brought up by RFC-001 in Sprint 25 can show meshes but cannot show the scalar / vector / tensor data that the solver emits. The renderer-friendly view onto `souxmar_field_t` needs three properties that the existing `field.h` does not provide:

1. **Cached min/max + units.** Color-mapping requires the per-component range before the first frame draws; computing it on every viewport refresh is wasteful. The host caches min/max alongside the stream handle on first open.
2. **Float32 SoA reads.** Solver output is `f64`; the renderer consumes `f32` (Three.js `BufferGeometry`). Down-casting on the C side is one copy instead of two (one to JS, one on JS-side cast).
3. **A lifetime separate from the underlying field.** Open and close a "view onto this field"; the cache lives on the view, the field handle stays innocent of renderer state.

This is the same derived-view pattern ADR-0037 introduced for surface streams — applied to fields. RFC-002 carries the architectural rationale; this ADR carries the binding declaration that the ratchet has been considered, sized, and reviewed under the ADR-0008 process.

## Decision

The C ABI gains **one new header** (`include/souxmar-c/field_stream.h`), **one new opaque handle type** (`souxmar_field_stream_t`), and **seven new function declarations**. `SOUXMAR_ABI_VERSION_MINOR` bumps from **4** to **5** with a new history line in `abi.h`. No existing declaration moves; no struct layout changes. Strict additive minor ratchet per ADR-0008. Sits on top of ADR-0037's v1.4 surface-stream ratchet (which must land first).

### New surface (verbatim — lands in PR 1 of Sprint 27)

```c
/* SPDX-License-Identifier: Apache-2.0
 *
 * include/souxmar-c/field_stream.h — additive, v1.5 of the C ABI.
 *
 * Renderer-friendly view over an existing souxmar_field_t. The view
 * is *derived* and read-only; the host computes per-component min/max
 * on first open and caches alongside the field. Threading: not
 * thread-safe; marshal via the existing host dispatcher.
 */

#ifndef SOUXMAR_C_FIELD_STREAM_H
#define SOUXMAR_C_FIELD_STREAM_H

#include "abi.h"
#include "field.h"
#include "status.h"

SOUXMAR_C_BEGIN

typedef struct souxmar_field_stream_t souxmar_field_stream_t;

souxmar_field_stream_t* souxmar_field_stream_open(const souxmar_field_t* field);
void                     souxmar_field_stream_close(souxmar_field_stream_t* stream);

size_t  souxmar_field_stream_count(const souxmar_field_stream_t* s);
uint8_t souxmar_field_stream_components(const souxmar_field_stream_t* s);

souxmar_status_t souxmar_field_stream_range(const souxmar_field_stream_t* s,
                                             double* out_min,
                                             double* out_max);

const char* souxmar_field_stream_units(const souxmar_field_stream_t* s);

souxmar_status_t souxmar_field_stream_values(const souxmar_field_stream_t* s,
                                              float* out, size_t out_capacity);

SOUXMAR_C_END
#endif /* SOUXMAR_C_FIELD_STREAM_H */
```

### `abi.h` history entry

```
 *   v1.5  Sprint 27 push 1 — field-stream renderer surface
 *                            (souxmar-c/field_stream.h); ADR-0038.
```

`#define SOUXMAR_ABI_VERSION_MINOR 4` becomes `#define SOUXMAR_ABI_VERSION_MINOR 5`.

### Host-side implementation contract

- **The stream holds an internal reference to the field.** Freeing the field while the stream is open is undefined behavior. Close the stream first.
- **min/max + units are cached on open.** First `souxmar_field_stream_open` call on a given field handle computes the per-component min/max and resolves the units string; subsequent opens on the same field re-use the cache.
- **Range is reported in f64.** Source precision is preserved on `souxmar_field_stream_range` so the legend doesn't suffer the lossy down-cast that the SoA buffer does.
- **Values are reported in f32 SoA.** `out_capacity` must be ≥ `count * components`. Passing an undersized capacity returns `SOUXMAR_E_INVALID_ARGUMENT`.
- **Units is a borrowed C string.** Owned by the stream; valid until close. Empty string ("") if the field has no unit attached. (See ADR-NNNN-field-units-accessor — RFC-002's Open Question 1 — for how the units accessor lands on `field.h`; if that ADR slips, this surface returns "" for now.)

### What stays out of scope

- **Write paths.** The surface is read-only. Solver plugins continue to write fields via the existing `field.h`.
- **Time-series.** Carried separately by ADR-0042 (RFC-006); this ratchet is single-frame only.
- **Tensor → scalar derivations.** Computing σ_vm from a stress tensor lives in the reader plugin (RFC-002 Open Question 2), not the field-stream surface.

## Alternatives considered

### Extend `field.h` with stream accessors

Pro: one header per concept. Con: the field handle is already the source of truth for solver-emitted data; layering renderer-only state (cached min/max) on top would either bloat every field with renderer cache, or introduce conditional cache logic on every read. The derived-view pattern (ADR-0037) keeps the concepts orthogonal.

### Render directly from the on-disk VTU in JavaScript

Bundle `vtk.js`'s VTU reader; skip the bridge for results. Rejected per RFC-002 §Alternatives A — doubles bundle size, stalls on large files, and still leaves the agent's "what's the peak stress?" question needing an in-core path.

### Bulk-buffer ABI for fields (`souxmar_field_buffers_t` style)

Mirror ADR-0006's mmap-backed buffer protocol. Rejected because field arrays for a frame are typically ≤ 50 MB on the v1 target meshes; the SoA copy cost is acceptable and the host already owns the cache. Re-evaluate if a future "huge field" scenario surfaces (time-series at very high res — that's ADR-0042's territory).

### Skip the ratchet; carry field data through the value-bag

Same shape as ADR-0037's value-bag rejection. The value-bag is for command IO, not 50 MB/frame buffers.

## Consequences

### Positive

- **Sprint 27 PR 2 (bridge surface) has a contract to bind against** — `field_stream_open` / `field_stream_chunk` Rust commands map directly onto the C accessors plus the bridge-side chunker.
- **`viz.list_fields` agent tool has an obvious backing.** The summary call (`souxmar_field_stream_open` + `souxmar_field_stream_range` + `souxmar_field_stream_units`) is one C call per field.
- **Reader-plugin coupling stays clean.** `reader.vtu` produces `souxmar_field_t` instances; the field-stream surface consumes them without the reader knowing the renderer exists.
- **No plugin author migration.** Plugins built against v1.0–v1.4 link and load unchanged.

### Negative

- **Second additive surface in the post-v1.0 block.** Following ADR-0037, this is the second derived-view header. The pattern is now precedent; future viewers (assemblies, section cuts) will follow it.
- **Float32 lossy on the SoA path.** Acceptable for visualization (8-bit-per-channel display is the real floor), but the legend's min/max must come from the f64 `range` accessor to avoid stripey low-dynamic-range fields. Documented in the header.
- **Units string lifetime is borrowed.** A subtle UB if the consumer outlives the stream. Documented; PR 1's tests must include the "use after close" guard.

### Risks

- **Risk:** Tensor fields produce six components — the agent tool surface assumes flat names (`σ_vm`, `σ_max`). Mitigation: tensor → scalar derivations happen in the reader plugin per RFC-002 Open Q2, so the field handle the stream wraps is always scalar-or-vector at this surface.
- **Risk:** Cache invalidation under field-value mutation. Mitigation: fields are conventionally write-once in souxmar (solvers emit; readers parse; consumers read). If a future plugin starts mutating field values, the cache contract must be revisited — at that point a `_invalidate()` entry point joins this surface.
- **Risk:** Plugin C004 skew. Mitigation: same gate as ADR-0037 — C004 already covers the version-skew matrix.

## Pre-mortem (one year from today)

It is 2027-05-12 and the field-stream ratchet went badly. Most likely failure mode: a user opens a thermal problem with a temperature delta of 0.001 K over a 300 K baseline; the f32 down-cast produces visible color banding and a forum thread mistakes it for solver discretization error. We add a high-precision streaming mode in v1.5.1 but the legend story is already entrenched. The fix would land additively — a new `souxmar_field_stream_values_f64` accessor — without breaking the f32 path.

Less-likely failure mode: the reader plugin (`reader.vtu`, lands the same sprint) produces a field with the wrong units string on cell-located VTU output; consumers misread "Pa" as "MPa" for three months before someone catches it. Mitigation: conformance tests verify units round-trip on the cantilever golden file.

Leading indicators:

- Any user-reported "colors look stripey" issue on fields with < 0.5% dynamic range relative to baseline.
- Conformance VTU corpus all of the same units convention.
- `viz.list_fields` p95 latency above 100ms on the cantilever — implies the in-core min/max compute isn't actually cached.

## References

- ADR-0008 — ABI v1 final freeze + ratchet rules (the gating ADR).
- ADR-0037 — surface-stream ratchet (v1.3 → v1.4); precedes this one.
- ADR-0006 — bulk-buffer ABI; precedent for "bulk SoA reads at C level."
- RFC-002 (`docs/rfcs/0002-field-stream-protocol.md`) — the gating RFC. This ADR cannot move past Proposed until RFC-002 is Accepted.
- `include/souxmar-c/abi.h` — file under ratchet; the MINOR bump lands here.
- `include/souxmar-c/field.h` — existing handle this ratchet layers on top of.
- `scripts/check-frozen-headers.sh` — CI gate that accepts PR 1 via the `Ratchet: additive minor surface (ADR-0008)` commit marker.

## History

- 2026-05-11 (Sprint 6 push 4): first minor ratchet — `reader.*` (v1.0 → v1.1).
- 2026-05-11 (Sprint 7 push 3): second — mmap-backed buffer protocol (v1.1 → v1.2).
- 2026-05-11 (Sprint 9 push 2): third — per-face-tag surface, ADR-0012 (v1.2 → v1.3).
- 2026-05-12 (Sprint 25 push 1): fourth — surface-stream renderer, ADR-0037 (v1.3 → v1.4). Proposed.
- 2026-05-12 (Sprint 27 push 1): **fifth minor ratchet — field-stream renderer surface, this ADR** (v1.4 → v1.5). Proposed; gates on RFC-002 acceptance.
