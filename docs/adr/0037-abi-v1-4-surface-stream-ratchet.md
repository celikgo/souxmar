# ADR-0037: Surface-stream C ABI ratchet (v1.3 → v1.4)

- **Status:** Proposed
- **Date:** 2026-05-12 (Sprint 25 push 1)
- **Author:** celikgokhun
- **Deciders:** core, plugin-host, desktop, AI, platform, DX
- **Tier:** 3 (heavy / requires RFC — frozen-header ratchet)
- **Affects:** `include/souxmar-c/surface_stream.h` (new), `include/souxmar-c/abi.h`
  (`SOUXMAR_ABI_VERSION_MINOR` bump 3 → 4 + history line), conformance suite
  (new round-trip tests), `src/desktop/src-tauri/souxmar-bridge` (downstream
  consumer), in-core implementation backing the entry points.

## Context

Post-v1.0 Sprint 25 brings up the 3D viewport renderer (RFC-001) and needs a
renderer-friendly view onto an existing `souxmar_mesh_t`. The renderer cannot
re-use `mesh.h`'s cell-oriented accessors directly:

1. **It needs the outer shell, not the volumetric cells.** The user perceives
   the surface; the renderer walks `(positions, normals, indices)` triangles
   in SoA form. Asking the renderer to traverse cells and extract boundary
   faces on every redraw would burn the 16ms frame budget on bookkeeping
   instead of drawing.
2. **It needs per-triangle picking IDs.** `face_id` and `vertex_id` are
   carried alongside the geometry buffers so the React-side raycaster can
   resolve a screen-space hit back to the entities the Inspector and the
   agent's selection tools already use. There is no slot for these on the
   existing volumetric handle.
3. **It needs derived data that is cached at the host.** Face normals and
   the outer-shell extraction are computed once on stream open, cached keyed
   by mesh handle, and torn down on stream close. None of that belongs on
   the mesh handle itself — the mesh is the source of truth; the surface
   stream is a view.

The C ABI is frozen final at v1 (ADR-0008). Per the ratchet rules,
**additive minor surfaces** are allowed: new declarations and new opaque
handle types, no breaking changes to existing signatures. The
surface-stream surface is the textbook additive case — no existing function
moves; the new declarations live in a brand-new header
`include/souxmar-c/surface_stream.h`; meshes constructed before the new
header existed continue to load and run unchanged because nothing in this
header is required of plugins.

RFC-001 (`docs/rfcs/0001-viewport-renderer.md`) lays out the renderer
architecture across three layers (React, bridge, ABI) and names this ADR
as its ABI-ratchet companion: the RFC carries the design rationale and
alternatives at the architectural level; this ADR carries the binding
declaration that the ratchet has been considered, sized, and reviewed
under the ADR-0008 process.

## Decision

The C ABI gains **one new header** (`include/souxmar-c/surface_stream.h`),
**one new opaque handle type** (`souxmar_surface_stream_t`), and **eight
new function declarations**. `SOUXMAR_ABI_VERSION_MINOR` bumps from 3 to 4
with a new history line in `abi.h`. No existing declaration moves; no
struct layout changes; no element-type numeric values change. This is a
strict additive minor ratchet per ADR-0008.

### New surface (verbatim)

The complete header as it lands in PR 1 of Sprint 25:

```c
/* SPDX-License-Identifier: Apache-2.0
 *
 * include/souxmar-c/surface_stream.h — additive, v1.4 of the C ABI.
 *
 * Layers a renderer-friendly surface view on top of an existing
 * souxmar_mesh_t. The view is *derived*: nothing here mutates the
 * underlying mesh; the host computes face normals + an outer-shell
 * extraction on first open and caches the result keyed by mesh handle.
 *
 * Threading: not thread-safe; the renderer thread is expected to
 * marshal calls to the host worker via the existing dispatcher.
 */

#ifndef SOUXMAR_C_SURFACE_STREAM_H
#define SOUXMAR_C_SURFACE_STREAM_H

#include "abi.h"
#include "mesh.h"
#include "status.h"

SOUXMAR_C_BEGIN

typedef struct souxmar_surface_stream_t souxmar_surface_stream_t;

souxmar_surface_stream_t* souxmar_surface_stream_open(const souxmar_mesh_t* mesh);
void                       souxmar_surface_stream_close(souxmar_surface_stream_t* stream);

size_t souxmar_surface_stream_vertex_count(const souxmar_surface_stream_t* s);
size_t souxmar_surface_stream_triangle_count(const souxmar_surface_stream_t* s);

souxmar_status_t souxmar_surface_stream_bounds(const souxmar_surface_stream_t* s,
                                                double out_min[3],
                                                double out_max[3]);

souxmar_status_t souxmar_surface_stream_positions(const souxmar_surface_stream_t* s,
                                                   float* out, size_t out_capacity);
souxmar_status_t souxmar_surface_stream_normals(const souxmar_surface_stream_t* s,
                                                 float* out, size_t out_capacity);
souxmar_status_t souxmar_surface_stream_indices(const souxmar_surface_stream_t* s,
                                                 uint32_t* out, size_t out_capacity);
souxmar_status_t souxmar_surface_stream_face_ids(const souxmar_surface_stream_t* s,
                                                  uint32_t* out, size_t out_capacity);
souxmar_status_t souxmar_surface_stream_vertex_ids(const souxmar_surface_stream_t* s,
                                                    uint64_t* out, size_t out_capacity);

SOUXMAR_C_END
#endif /* SOUXMAR_C_SURFACE_STREAM_H */
```

### `abi.h` history entry

Added immediately under the existing v1.3 line:

```
 *   v1.4  Sprint 25 push 1 — surface-stream renderer surface
 *                            (souxmar-c/surface_stream.h); ADR-0037.
```

`#define SOUXMAR_ABI_VERSION_MINOR 3` becomes `#define SOUXMAR_ABI_VERSION_MINOR 4`.

### Host-side implementation contract

- **The stream holds an internal reference to the mesh.** Freeing the
  mesh while the stream is open is undefined behavior. Close the stream
  first. This matches the lifetime discipline established for every
  other derived-view handle in the v1 ABI.
- **Outer-shell extraction is cached.** First `souxmar_surface_stream_open`
  call on a given mesh handle computes the boundary-face set and per-vertex
  normals; subsequent opens on the same mesh re-use the cache. Cache
  invalidates on `souxmar_surface_stream_close` of the *last* open stream
  for that mesh (refcount), or on mesh free.
- **Out-buffer capacity is the count from the matching `_count` function.**
  Passing an undersized capacity returns `SOUXMAR_E_INVALID_ARGUMENT`.
  Passing an oversized capacity is permitted; only the count-many leading
  entries are written.
- **Float32 positions and normals are deliberate.** The renderer consumes
  Float32 (Three.js `BufferGeometry`); we down-cast from the mesh's
  doubles on copy so the renderer side does not redo the cast on every
  upload. The double-precision values remain available via `mesh.h`.
- **`vertex_ids` are `uint64_t`.** Matches the `souxmar_mesh_t` node
  numbering width. The desktop bridge packs them into two `uint32` lanes
  on the way to JavaScript because `Uint64Array` is not a Three.js
  `BufferAttribute` type, but that is a renderer-side detail; the ABI
  carries the full 64 bits.
- **`face_ids` are `uint32_t` per triangle.** The renderer broadcasts each
  triangle's id to its three vertices on the BufferAttribute upload; that
  broadcast is renderer-side, not ABI-side.
- **No vertex-ID stability promise across re-meshes.** The IDs returned
  by `souxmar_surface_stream_vertex_ids` are valid only for the mesh
  the stream was opened against. Consumers must re-fetch after a mesh
  regeneration. A stable-selection-IDs surface is deferred to S31
  (boundary conditions from the viewport) under its own RFC.

### What stays out of scope

- **No write paths.** The header is read-only — there is no
  `souxmar_surface_stream_set_*`. The surface stream is a derived view;
  mutations belong on `mesh.h`.
- **No multi-mesh streams.** One stream per mesh handle. Composite scenes
  (multiple bodies in one viewport) compose multiple streams on the
  renderer side, not the ABI side.
- **No field arrays.** Scalar / vector field data lives behind a separate
  forthcoming surface (RFC-002, Sprint 27). Conflating field rendering
  with surface streaming would force the v1.4 surface to grow before
  RFC-002's design is settled.
- **No streaming-chunk concept in the C ABI.** Chunking is a bridge-side
  concern (Tauri's JSON payload size). The C ABI returns the full SoA
  arrays in one call; the Rust bridge decides whether to forward them
  as a single payload or split them. Pushing chunking down to C would
  add ABI surface for what is a transport-layer problem.

## Alternatives considered

### Extend `mesh.h` with surface-view accessors instead of a new header

Pro: one header per concept ("everything mesh-shaped is in `mesh.h`"). Con:
the surface-stream concept is a *derived view* with its own lifetime — it
caches normals and an outer-shell extraction that the mesh handle does not
own. Mixing the two on the same header would imply that the cache lives
on the mesh handle, which would either bloat every mesh with renderer-only
state or introduce a "is this slot populated?" check on every mesh free.
A separate handle with a separate header keeps the concepts orthogonal
and lets non-renderer consumers (CLI, Python, solvers) stay innocent of
renderer state.

### Expose surface-streaming as a plugin capability instead of a host header

Pro: keeps `include/souxmar-c/` smaller; lets third parties supply
alternative surface extractors (e.g., a smoothing-aware one). Con: the
renderer is host-side, not a plugin — it is part of the desktop app's
contract with users. Routing renderer state through the plugin host
would add a confirmation prompt for what is read-only camera plumbing,
or force us to invent a "trusted-plugin" tier we deliberately do not
have. The plugin path also slows the load: every viewport open would
go through the dispatcher's plugin lookup. The renderer wants a direct
host call.

### Push surface extraction to the desktop side; ABI exposes raw cell connectivity only

Pro: zero ABI surface added; the desktop crate handles all of geometry
derivation in Rust / TypeScript. Con: outer-shell extraction on a
volumetric mesh is non-trivial (it's a face-hash-set walk over
millions of faces) and would burn 60ms+ of UI-thread time on a 250k-tri
cantilever. Doing it in C++ on the host side, behind a worker, is the
right place. Also: the agent might want to query "surface-only" metrics
later (e.g., surface area for a BC heat-flux integral) — re-using the
host's extraction is cheap, while duplicating it on the desktop side is
not.

### Add a streaming/cursor-based read API (`*_next_chunk`) at the C level

Pro: matches the bridge's chunked transport. Con: chunking is a transport
artefact (Tauri JSON size limits); the C ABI doesn't have that constraint.
Carrying chunking down to C would force every non-bridge consumer (CLI,
Python via pybind11, a future native C consumer) to learn the chunk
state-machine for no gain. Bulk SoA reads at C level + chunking at the
bridge level is the cleaner split.

### Skip the ratchet; carry surface data through the value-bag

Pro: zero ABI churn. Con: the value-bag is for command-level inputs and
outputs, not 250k-tri-per-frame buffers. Serialising the SoA arrays to
JSON every time the user rotates the camera is the kind of "ugly
workaround" the ABI v1 pre-mortem in ADR-0007 / ADR-0008 explicitly
warned us against — it's exactly the shape of bug the freeze was meant
to prevent.

## Consequences

### Positive

- **Sprint 25 PR 2 (bridge surface) has a contract to bind against** —
  the `surface_stream_open` / `surface_stream_chunk` Rust commands map
  one-for-one onto the C accessors plus the bridge-side chunker.
- **Sprint 26 mesh-viz features land additively** — render-mode switching,
  clipping plane, exploded view, color-by-quality all consume the same
  SoA from this header. No further ABI surface needed for S26.
- **Documented derived-view pattern.** This is the first time we add a
  derived-view handle on top of a frozen v1 type. The pattern (open /
  close, cached derived data on host, no write paths) is now precedent
  for future derived views — most likely a "shell" view for assemblies
  (post-v1.3) and a "section cut" view for clipping (S26 or later).
- **No plugin author migration.** Plugins built against v1.0–v1.3 link
  and load unchanged.

### Negative

- A frozen-header touch costs reviewer attention. This is the price the
  freeze-final commit (Sprint 7 push 1, ADR-0008) explicitly budgeted
  for — minor ratchets are expected, gated, and absorbed.
- **Float32 + Uint32 widths bake in a 4-billion-vertex ceiling on a
  single stream.** Real-world meshes are nowhere near that, and a future
  "huge mesh" mode could ship as a separate v1.N surface; we are not
  pre-paying that complexity here.
- **Cache lifetime adds host-side state we didn't have before.** The
  refcounted cache must clean up correctly on every error path. PR 1's
  test plan must include open / open / close / close in pathological
  orders, plus open-during-mesh-free (which is documented UB but must
  not crash the host in practice — it should `SOUXMAR_E_NOT_FOUND` or
  similar).

### Risks

- **Risk:** A future renderer wants a different SoA layout (e.g., AoS
  vertices for cache locality on instanced draws). **Mitigation:** the
  current SoA matches Three.js `BufferGeometry` directly, which is the
  consumer for the next 24 months. If a future renderer wants AoS, it
  joins the v1.4 surface — they coexist; the SoA path is not retired.
- **Risk:** Out-buffer sizing is error-prone (caller computes
  `3 * vertex_count`; can mismatch). **Mitigation:** PR 1's conformance
  tests include the under-/over-/exact-capacity matrix per accessor;
  the bridge's Rust wrapper does the computation once in safe code so
  TypeScript never sees the raw size math.
- **Risk:** Plugins built against v1.4 won't load on a v1.3 host.
  **Mitigation:** Conformance check C004 already catches the
  "v1.N plugin on v1.M host where M<N" case at registration time —
  the same mechanism that gated every prior ratchet (ADR-0012, ADR-0006).
  No new check required.
- **Risk:** The cache key (mesh handle pointer) is invalidated by a mesh
  reallocate. **Mitigation:** souxmar `Mesh` allocates once and never
  re-bases (verified in `souxmar::core::Mesh`'s test suite); if a future
  v2 introduces reallocating semantics, the cache becomes part of that
  operation's contract.

## Pre-mortem (one year from today)

It is 2027-05-12 and the surface-stream ratchet went badly. Most likely
failure mode: the chunking layer we deliberately kept *out* of the C ABI
turned out to be the dominant cost, and we found ourselves wanting a
streaming-cursor API at the C level after all — for Python bindings that
wanted to consume large meshes without materialising the whole SoA. The
fix would land as v1.5 — additive, non-breaking, alongside the existing
bulk accessors. Either way the v1.4 surface stays valid.

Less-likely failure mode: the outer-shell extraction's cache became a
memory-leak source under chat-driven rapid mesh regeneration; the user
ran `mesher.run` ten times in a row and the host process grew by
several hundred megabytes. The cache invalidation logic is the obvious
place to look; PR 1's test plan must include rapid-regen scenarios.

Leading indicators to watch:

- Any v1.N plugin (N≥4) reporting `SOUXMAR_E_ABI_MISMATCH` against a
  v1.4 host — the conformance check should make this impossible; an
  occurrence means the check has a hole.
- `souxmar_surface_stream_open` followed by mesh free without
  `souxmar_surface_stream_close` showing up in valgrind / ASan runs.
- Bridge-side timing logs showing the C accessors as the hot path
  rather than the JSON encoding — that flips the chunk-encoding
  decision RFC-001 deferred to PR 2.

## References

- ADR-0008 — ABI v1 final freeze + ratchet rules (the gating ADR).
- ADR-0012 — per-face-tag ratchet (v1.2 → v1.3); structural precedent
  for this ADR.
- ADR-0006 — bulk-buffer ABI; precedent for "bulk SoA reads at C level,
  chunking at the bridge level."
- ADR-0016 — `BridgeFeatureSet` contract; this ratchet pairs with the
  `viewport_renderer` flag flip in PR 2.
- RFC-001 (`docs/rfcs/0001-viewport-renderer.md`) — the gating RFC. This
  ADR cannot move past Proposed until RFC-001 is Accepted.
- `include/souxmar-c/abi.h` — the file under ratchet; the `MINOR` bump
  lands here.
- `include/souxmar-c/mesh.h` — the existing handle this ratchet layers
  on top of.
- `scripts/check-frozen-headers.sh` — the CI gate that accepts PR 1 via
  the `Ratchet: additive minor surface (ADR-0008)` commit marker.

## History

- 2026-05-11 (Sprint 6 push 4): first minor ratchet — `reader.*`
  capability surface (v1.0 → v1.1).
- 2026-05-11 (Sprint 7 push 3): second minor ratchet — mmap-backed
  buffer protocol (v1.1 → v1.2).
- 2026-05-11 (Sprint 9 push 2): third minor ratchet — per-face-tag
  surface, ADR-0012 (v1.2 → v1.3).
- 2026-05-12 (Sprint 25 push 1): **fourth minor ratchet — surface-stream
  renderer surface, this ADR** (v1.3 → v1.4). Proposed; gates on
  RFC-001 acceptance.
