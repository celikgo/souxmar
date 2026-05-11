# ADR-0006: Memory-mapped large-buffer protocol for ABI mesh transfer

- **Status:** Accepted (v1 + v2 implemented as of Sprint 7 push 3)
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

### v2 implementation (Sprint 7 push 3 — ABI minor v1.2)

The first additive-minor ratchet event post-freeze. ABI v1.1 → v1.2.
The accessor surface from v1 is unchanged; new entries are added
alongside:

```c
#define SOUXMAR_BUFFER_FLAG_READONLY  ((uint32_t)1u << 0)
#define SOUXMAR_BUFFER_FLAG_CREATE    ((uint32_t)1u << 1)

souxmar_buffer_t* souxmar_buffer_new_mmap(const char* path,
                                          size_t      size_bytes,
                                          uint32_t    flags);

int               souxmar_buffer_is_mmap(const souxmar_buffer_t*);
```

`flags == 0` is the common "open existing RW file" path. `READONLY`
opens for read-only mapping (and `souxmar_buffer_data` returns NULL
— callers use `_data_const`). `CREATE | flags` creates the file if
missing; the host `ftruncate`s the file to `size_bytes` so the
mapping lands a full region of writable bytes.

Backing detection: the kind discriminator lives in the
`BufferHeader::reserved` field (renamed `kind` internally) — v1
zero-initialised it, so the v1 binary layout is unchanged. Plugins
that consume a buffer through `_data` / `_data_const` cannot tell
the difference; `_is_mmap` is the explicit hook for tooling that
wants to know.

`souxmar_buffer_free` detects the kind and either:
- heap-backed → `free()` the malloc base (v1 behaviour, unchanged), or
- mmap-backed → `munmap()` (or `UnmapViewOfFile`) + `close()` (or
  `CloseHandle`) for the underlying fd / HANDLE.

The forward-compatibility contract from v1 holds: a v1.1 plugin that
only ever calls `souxmar_buffer_new` keeps working unchanged on a v1.2
host. A v1.2 plugin that calls `_new_mmap` against a v1.1 host fails
cleanly at symbol resolution time (conformance check C004).

### v2 mesh-ingest integration

`souxmar_mesh_from_buffers` (ADR-0006 v1) reads buffer data via
`souxmar_buffer_data_const`, which transparently returns either the
heap data slot or the mmap region. The bulk-mesh ingest path is
out-of-core capable without a single line of change — that's the
whole point of the v1 → v2 forward-compatibility plan.

### Deferred to a later sprint

- `souxmar_buffer_share(buf, &fd_or_handle)` — exposing the underlying
  file descriptor (POSIX) or `HANDLE` (Windows) so a subprocess plugin
  can map the same region. Subprocess plugins aren't a Sprint 7
  deliverable; this lands when the OpenFOAM adapter does in Sprint 8+.
- POSIX `shm_open` / Windows anonymous mappings for in-memory shared
  buffers between cooperating in-process plugins. The current v2 path
  always backs onto a real file; an anonymous-mapping flag is the
  natural next ratchet.

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
- 2026-05-11 (Sprint 7 push 3): v2 mmap-backed implementation lands as
  the first post-freeze additive-minor ratchet event. ABI v1.1 → v1.2.
  Subprocess fd/HANDLE sharing deferred to Sprint 8+ alongside the
  OpenFOAM subprocess plugin work.
