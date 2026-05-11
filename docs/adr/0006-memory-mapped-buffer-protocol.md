# ADR-0006: Memory-mapped large-buffer protocol for ABI mesh transfer

- **Status:** Accepted (v1 implemented; mmap backing reserved for v2)
- **Date:** 2026-05-11
- **Author:** souxmar core team
- **Deciders:** core, plugin-host
- **Tier:** 2 (standard)
- **Affects:** ABI, plugin SDK, performance

## Context

The Sprint 5 plan reserves this ADR for the "memory-mapped large-buffer
protocol." The driving observation: the per-element mesh-construction
path through the C ABI

```
souxmar_mesh_t* m = souxmar_mesh_new();
for (...) souxmar_mesh_add_node(m, p);
for (...) souxmar_mesh_add_cell(m, type, nodes, n, tag, NULL);
```

is dominated by per-call ABI overhead at scale. A 1M-node / 6M-cell
mesh is ~7M ABI calls; at ~40-50 ns per call (function pointer indirect
+ argument marshaling + atomic counter touch in `Mesh::add_node`) that's
roughly 280-350 ms of overhead before the underlying `std::vector::push_back`
work. For Gmsh- or OpenCASCADE-derived meshes routinely producing 10M+
elements, this is the difference between "interactive" and "go get
coffee."

The fix is to let plugins write nodes / cell types / connectivity into
**host-owned bulk buffers**, then hand the whole package to the host
in a single ABI call. The buffer type itself becomes a stable handle
the host can choose to back differently — heap today, mmap tomorrow.

## Decision

We add `souxmar_buffer_t` (an opaque handle in `souxmar-c/buffer.h`)
plus `souxmar_mesh_from_buffers()` (in `souxmar-c/mesh.h`). Sprint 5
push 4 ships **v1: heap-backed**. The handle is forward-compatible
with the v2 mmap-backed implementation; no plugin-side change is
needed when v2 lands.

### v1 contract

```c
souxmar_buffer_t* souxmar_buffer_new(size_t bytes);
void              souxmar_buffer_free(souxmar_buffer_t*);
void*             souxmar_buffer_data(souxmar_buffer_t*);          /* mutable view */
const void*       souxmar_buffer_data_const(const souxmar_buffer_t*);
size_t            souxmar_buffer_size(const souxmar_buffer_t*);
size_t            souxmar_buffer_alignment(void);                  /* >= 16 */
```

Memory model: the host allocates; the plugin (or host) writes; either
side may free. Pointer returned by `souxmar_buffer_data` is valid
until the matching `souxmar_buffer_free` call.

### Mesh bulk constructor

```c
typedef struct souxmar_mesh_buffers {
  const souxmar_buffer_t*  node_coords;        /* 3*num_nodes doubles */
  size_t                   num_nodes;
  const souxmar_buffer_t*  cell_types;         /* num_cells uint16_t */
  const souxmar_buffer_t*  cell_connectivity;  /* flat uint64_t */
  const souxmar_buffer_t*  cell_offsets;       /* num_cells+1 uint64_t */
  size_t                   num_cells;
  const souxmar_buffer_t*  cell_tags;          /* num_cells int32_t (NULL = untagged) */
} souxmar_mesh_buffers_t;

souxmar_mesh_t* souxmar_mesh_from_buffers(
    const souxmar_mesh_buffers_t* buffers,
    souxmar_status_t*             out_status);
```

The host validates every shape invariant (sizes match counts, offsets
monotonic, per-cell node count matches its element type, node indices
in range) before touching the data. `out_status` carries a structured
reason on failure.

### v2 rollout (future, post-freeze)

The accessor surface above does not change. The implementation grows
an alternative backing:

1. `SOUXMAR_BUFFER_FLAG_SHARED` flag accepted by `souxmar_buffer_new` →
   the buffer is mmap'd into a temporary file rather than the heap.
2. New accessor `souxmar_buffer_share(buf, &fd_or_handle)` → returns
   the underlying file descriptor (POSIX) or `HANDLE` (Windows) so a
   subprocess plugin can map the same region.
3. The backing flag is stored in the existing `BufferHeader::reserved`
   field (currently 0) — already structurally reserved for this exact
   purpose.

v1 plugins that use only `souxmar_buffer_new` / `_free` / `_data` work
unchanged in v2.

## Alternatives considered

### Pass raw pointers across the ABI

```c
souxmar_mesh_from_raw(const double* coords, size_t num_nodes,
                      const uint16_t* types, ..., souxmar_status_t*);
```

Pro: no new opaque type. Con: ownership is unclear ("does the host
take ownership? must I keep this alive until when?"), and v2 mmap
backing requires *some* handle anyway. Adding the indirection now
keeps v2 a non-event.

### Shared-memory POSIX/Windows objects from day 1

The "real" mmap protocol with `shm_open` + fd passing or
`CreateFileMapping`. Pro: production-ready immediately. Con: the
cross-platform surface is large (POSIX shm semantics differ from
Windows file mappings; subprocess plugins aren't a Sprint 5
requirement). v1 ships the API; v2 ships the wire.

### Variable-batch per-element setters

```c
souxmar_mesh_add_nodes_batch(mesh, coords, n);
souxmar_mesh_add_cells_batch(mesh, types, conn, offsets, n);
```

Pro: smallest API surface. Con: still has per-batch ABI overhead, and
the natural batch size is "all of them" — at which point the buffer
abstraction is the right shape to start with.

## Consequences

### Positive

- Plugins targeting bulk mesh construction win 10–100× on construction
  time at scale (the benchmark in `benchmarks/bench_mesh_construction.cpp`
  pins down the actual numbers per-platform).
- Existing per-element `souxmar_mesh_add_node` / `add_cell` is
  unchanged — small plugins keep working identically.
- The API is forward-compatible with the v2 mmap-backed implementation.
  No second ABI break.
- The buffer handle composes with future bulk APIs (Field bulk
  construction, geometry bulk import) by the same pattern.

### Negative

- Two paths to construct a mesh now exist (per-element + bulk). Plugin
  authors have to pick. The plugin SDK doc explains when to prefer
  which (rule of thumb: > 10k elements ⇒ bulk).
- The bulk descriptor's size validation surface is non-trivial — six
  size checks + two structural checks + per-cell type check. All
  covered by `tests/unit/test_c_abi_buffer.cpp`.
- The v1 heap-backed implementation gives no cross-process benefit. A
  subprocess-plugin-author hoping for shared-memory wins has to wait
  for v2 (or use the per-element path with the `guard_call` overhead
  swamping the difference anyway).

### Risks

- **Risk:** Plugin authors mis-author the offsets buffer (off-by-one,
  forget the terminator, non-monotonic). **Mitigation:** Host
  validates all invariants before writing the Mesh; `out_status`
  carries a precise message; unit tests cover every malformed-buffer
  shape.
- **Risk:** Pre-freeze ABI churn — adding new fields to
  `souxmar_mesh_buffers_t` post-freeze is a struct-layout change.
  **Mitigation:** The struct is appended to in source-only-compatible
  ways (new optional pointer fields tail-padded; old plugins zero them
  out automatically when they zero-init the struct). v1 ships a
  deliberately-minimal field set so the freeze surface stays small.
- **Risk:** v2 mmap implementation introduces fd lifetime bugs.
  **Mitigation:** Defer to v2; out of scope here.

## References

- `include/souxmar-c/buffer.h` — public buffer accessors.
- `include/souxmar-c/mesh.h` — `souxmar_mesh_buffers_t` + `souxmar_mesh_from_buffers`.
- `src/core/c_abi_buffer.cpp` — heap-backed v1 implementation.
- `src/core/c_abi_mesh.cpp` — bulk constructor + validation.
- `tests/unit/test_c_abi_buffer.cpp` — buffer round-trip + bulk-mesh validation tests.
- `benchmarks/bench_mesh_construction.cpp` — per-element vs bulk timing.
- ADR-0001 — original C ABI for plugins.
- ADR-0004 — plugin conformance suite (constrains the ratchet rule
  this design honors).

## History

- 2026-05-11: Proposed, accepted (Sprint 5 push 4). v1 heap-backed
  implementation lands. v2 mmap-backed deferred to a post-freeze sprint.
