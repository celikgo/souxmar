# ADR-0012: Per-face-tag C ABI ratchet (v1.2 → v1.3)

- **Status:** Accepted
- **Date:** 2026-05-11 (Sprint 9 push 2)
- **Author:** souxmar core team
- **Deciders:** core, plugin-host, adapters, DX
- **Tier:** 3 (heavy / requires RFC — frozen-header ratchet)
- **Affects:** `include/souxmar-c/mesh.h`, `include/souxmar/core/mesh.h`,
  `include/souxmar/core/element_type.h`, conformance suite (informational),
  `openfoam-solver`, mesh-quality postproc, reader plugins (downstream).

## Context

The Sprint 8 retro named this ratchet as the most likely Sprint 9
event. Three downstream consumers want per-face tags on the C ABI:

1. **`openfoam-solver` boundary-patch routing.** Sprint 8 push 4 staged
   `apply_inlet` / `apply_wall` / `apply_outlet` BCs on
   `session_state.boundary_conditions`, each keyed by a mesh tag.
   Sprint 8 push 6's `write_polymesh_from_mesh` produces a single
   "walls" boundary patch covering every boundary face — there is no
   way to discriminate `inlet` from `outlet` because the C ABI only
   exposes per-*cell* tags (`souxmar_mesh_cell_tag`). Per-face tags
   let the translator partition boundary faces by tag and emit one
   `boundary` patch per tag.
2. **Mesh-quality postproc per-tag diagnostics.** The Sprint 6 push 1
   `mesh-quality` postproc reports global Jacobian / aspect-ratio /
   dihedral statistics. With per-face tags it can report "face-tag
   `inlet` has aspect-ratio outliers" — a much more actionable signal
   for the agent to surface in chat.
3. **Reader-plugin tag preservation.** Sprint 8 push 3's `obj-reader`
   silently discards Wavefront OBJ `usemtl` group names because the
   C ABI has no slot for them. Same for Sprint 8 push 3's
   `blender-reader` (named collections), Sprint 6 push 4's
   `occt-reader` (face-level OCCT entity ids), and the planned
   `stl-reader` future work (per-solid names in ASCII STL). All four
   readers currently throw away rich source-file metadata.

The C ABI is frozen final at v1 (ADR-0008). Per the ratchet rules,
**additive minor surfaces** are allowed: new declarations, new
optional struct fields, no breaking changes to existing signatures.
The per-face tag surface is the textbook additive case — no existing
function moves; new declarations join `mesh.h`; meshes constructed
without face tags continue to report -1 (untagged) for every face,
which is the documented sentinel and matches the absence-of-storage
semantics.

The Sprint 8 retro proposed this surface shape:

```c
int32_t souxmar_mesh_face_tag(const souxmar_mesh_t* mesh,
                              uint64_t cell_index,
                              uint8_t  local_face_index);

souxmar_status_t souxmar_mesh_set_face_tag(souxmar_mesh_t* mesh,
                                           uint64_t cell_index,
                                           uint8_t  local_face_index,
                                           int32_t  tag);
```

This ADR accepts that shape with one addition (`souxmar_mesh_cell_face_count`
so plugins can iterate without computing face counts from element type)
and one new manifest constant (`SOUXMAR_FACE_UNTAGGED = -1` — matches
the existing untagged-cell convention).

## Decision

The C ABI gains **three new function declarations** in
`include/souxmar-c/mesh.h` and **one new constant**. `SOUXMAR_ABI_VERSION_MINOR`
bumps from 2 to 3. No existing declaration moves; no struct layout
changes; no SOUXMAR_ET_* numeric value changes. This is a strict
additive minor ratchet per ADR-0008.

### New surface (verbatim)

```c
/* Sentinel for an untagged face. Equals the untagged-cell convention. */
#define SOUXMAR_FACE_UNTAGGED ((int32_t)-1)

/* Number of faces (sides) on a cell, derived from its element type.
 * Returns 0 if the cell index is out of range or the cell's element
 * type has no face concept (Vertex, Edge*). Face indices are local
 * to the cell and run 0 .. souxmar_mesh_cell_face_count(mesh, cell) - 1. */
size_t souxmar_mesh_cell_face_count(const souxmar_mesh_t* mesh,
                                    uint64_t              cell_index);

/* Returns the tag stored for (cell_index, local_face_index), or
 * SOUXMAR_FACE_UNTAGGED if the slot is untagged, the cell index is
 * out of range, or the local face index exceeds the cell's face
 * count. Sparse storage: only explicitly-tagged faces consume space
 * on the host. */
int32_t souxmar_mesh_face_tag(const souxmar_mesh_t* mesh,
                              uint64_t              cell_index,
                              uint8_t               local_face_index);

/* Sets the tag stored for (cell_index, local_face_index). Setting
 * SOUXMAR_FACE_UNTAGGED clears the slot. Returns
 * SOUXMAR_E_NOT_FOUND if the cell index is out of range,
 * SOUXMAR_E_INVALID_ARGUMENT if the local face index exceeds the
 * cell's face count. */
souxmar_status_t souxmar_mesh_set_face_tag(souxmar_mesh_t* mesh,
                                           uint64_t        cell_index,
                                           uint8_t         local_face_index,
                                           int32_t         tag);
```

### Host-side implementation contract

- **Storage is sparse.** A `std::unordered_map<{cell, local_face}, tag>`
  on `Mesh::Impl` records only explicit non-default tags. Untagged
  faces — the vast majority on real meshes — consume no per-face
  storage. A boundary mesh with N tagged faces uses O(N) bytes, not
  O(num_cells × max_faces) bytes.
- **Default-tag invariant.** A fresh mesh, or a mesh constructed via
  `souxmar_mesh_from_buffers` without a face-tag descriptor, reports
  `SOUXMAR_FACE_UNTAGGED` for every face. Existing plugins that don't
  call the new accessors observe no behavioural change.
- **Local face index range.** Per element type, from
  `souxmar_mesh_cell_face_count`:

  | Element type            | Face count |
  | ----------------------- | ---------- |
  | Vertex, Edge2, Edge3    | 0          |
  | Tri3, Tri6              | 3 (edges)  |
  | Quad4, Quad8, Quad9     | 4 (edges)  |
  | Tet4, Tet10             | 4 (faces)  |
  | Hex8, Hex20, Hex27      | 6 (faces)  |
  | Prism6, Prism15         | 5 (faces)  |
  | Pyramid5, Pyramid13     | 5 (faces)  |

  (For 2D elements the "face" is a 1-D edge; this is the standard FEM
  side-set convention and matches Gmsh / VTK / Exodus II semantics.)
- **`uint8_t` for local face index.** Maximum face count among standard
  elements is 6 (hex family); a `uint8_t` is sufficient with comfortable
  headroom. Matches the Sprint 8 retro's named shape.

### What stays out of scope

- **The bulk path doesn't carry face tags.** `souxmar_mesh_buffers_t`
  is not extended in this ratchet. Per-face tags is a sparse attribute;
  the imperative `souxmar_mesh_set_face_tag` is sufficient for the
  three named consumers. A bulk `face_tags` buffer (an optional
  parallel pair of `face_keys[n]` (cell_idx, face_idx packed) + `face_tag_values[n]`
  buffers) is a candidate for a future minor (v1.4) when a real
  consumer needs it — most likely a future production-grade mesher
  that wants to set thousands of face tags at once. We are not adding
  the bulk path speculatively.
- **No iteration helper.** Downstream consumers walk
  `0..num_cells × cell_face_count` themselves; this is what
  `openfoam-solver` already does to enumerate boundary faces, and
  adding a `souxmar_mesh_iter_tagged_faces(...)` callback API speculatively
  would add ABI surface no concrete consumer asks for.

## Alternatives considered

### Add `face_tags` to `souxmar_mesh_buffers_t` instead

Pro: consistent with `cell_tags`; a single bulk-build call sets every
tag at once. Con: face tags are sparse — a typical industrial mesh has
millions of cells and thousands of tagged boundary faces. A
`num_cells × max_faces` parallel buffer would waste 99.5%+ of its slots
on `-1` sentinels. A sparse format (parallel `face_keys[n]` + `face_tag_values[n]`)
is the right shape, but no current consumer has a use case that
benefits enough over the imperative path. Defer.

### Extend `souxmar_mesh_add_cell` with a `const int32_t* face_tags` parameter

Pro: tags every face at cell-creation time. Con: this is a
*signature change* to an existing C ABI function — not an additive
ratchet. Would require a v2 ABI. A non-starter.

### Skip the ratchet; thread face tags through the value-bag

Pro: zero ABI churn. Con: the Sprint 8 retro's openfoam-solver patch
routing has to round-trip face tags through the agent's session_state
JSON, costing one serialization + one parse per BC application. The
host-side cost is also wrong: the mesh handle is the natural home for
mesh metadata. Threading through the value-bag is the kind of "ugly
workaround" the ABI v1 pre-mortem in ADR-0007 / ADR-0008 explicitly
warned us against.

### Add face tags only on a new opaque handle type (`souxmar_mesh_tags_t`)

Pro: keeps `mesh.h` lean; tags are a separate concept. Con: every
consumer would have to load and pass two handles. The mesh handle is
already the home for cell tags, node coordinates, connectivity — face
tags are the same kind of attribute. Adding a parallel handle would be
gratuitous separation.

## Consequences

### Positive

- **Per-patch BC routing in `openfoam-solver`.** The next push (Sprint
  9 push 3, per the plan) can land the actual translator change:
  group boundary faces by tag, emit one `boundary` patch per tag,
  with patch type derived from the staged BC `type` field
  (`inlet` / `wall` / `outlet`).
- **Mesh-quality per-tag diagnostics.** A follow-on can add a
  `--per-tag` mode to the postproc.
- **Reader-plugin tag preservation.** `obj-reader` can stop discarding
  `usemtl` groups; `occt-reader` can preserve OCCT face entity ids;
  `blender-reader` can preserve named-collection metadata. All four
  readers gain a way to surface source-file structure without
  re-engineering their output paths.
- **Documented sparse storage cost.** The sentinel-equals-untagged
  invariant means the change is observably free for any mesh whose
  consumer doesn't opt in.

### Negative

- A frozen-header touch costs reviewer attention. This is the price
  the freeze-final commit (Sprint 7 push 1, ADR-0008) explicitly
  budgeted for — minor ratchets are expected, gated, and absorbed.
- The `souxmar_mesh_face_tag` accessor returns an `int32_t` directly
  (not a status struct + out-parameter pair) — matching the existing
  `souxmar_mesh_cell_tag` shape so plugins don't need to learn a
  second idiom for the same concept. The status-pair accessor is the
  setter; the getter follows the cell-tag precedent.

### Risks

- **Risk:** A future mesh reorder (re-indexing cells) would invalidate
  the sparse-map keys. **Mitigation:** souxmar `Mesh` is append-only
  by design — cell indices never change after assignment. If a future
  v2 introduces compact / re-index semantics, the face-tag remap
  becomes part of that operation's contract.
- **Risk:** Local face index ordering for a given element type must
  match the FEM convention plugins expect. **Mitigation:** The
  convention is the standard Gmsh / VTK / OpenFOAM side-set ordering
  — the Sprint 8 push 6 openfoam-solver translator already encodes
  the Tet4 face table verbatim (`kTetFaces[4][3]` in
  `openfoam_solver.cpp`). The natural follow-on (Sprint 9 push 3, per-patch
  BC routing) will centralise the per-element-type face-node table
  as a host-side `souxmar::core::face_node_table()` helper; this
  push deliberately keeps to the imperative-accessors scope so the
  surface change is reviewable in isolation. Future element-type
  additions to the freeze document their face-ordering convention
  alongside that helper.
- **Risk:** Plugins built against v1.3 won't load on a v1.2 host.
  **Mitigation:** Conformance check C004 already catches the
  "v1.N plugin on v1.M host where M<N" case at registration time —
  the same mechanism that caught the Sprint 6 push 4 `reader.*`
  ratchet event. No new check required.

## Pre-mortem (one year from today)

It is 2027-05-11 and the per-face-tag surface went badly. Most likely
failure mode: a future production-grade mesher produces a mesh with
millions of tagged boundary faces, the sparse-map storage cost becomes
non-trivial, and we end up needing the bulk-path face-tag descriptor
this ADR deferred. The deferred work would land as v1.4 — additive,
non-breaking, same shape as the v1.2 `souxmar_buffer_new_mmap` ratchet.
Either way the v1.3 surface stays valid.

Less-likely failure mode: the FEM-side face-ordering convention turns
out to disagree with OpenFOAM's polyMesh convention for some element
type (most likely Pyramid5 or Prism6). The Sprint 8 push 6 translator
already established Tet4 face ordering; the mixed-element follow-on
(Sprint 9 push 4) is where this would surface. If it does, the fix is
documentation — clarify the per-element-type convention; the C ABI
surface itself is convention-agnostic.

## References

- ADR-0008 — ABI v1 final freeze + ratchet rules (the gating ADR).
- ADR-0006 — bulk-buffer ABI (the precedent for "what stays imperative
  vs. what gets a bulk path").
- `docs/retros/sprint-08.md` § One ADR-worthy decision surfaced — names
  this ratchet exactly.
- `include/souxmar-c/mesh.h` — the header under ratchet.
- `examples/plugins/openfoam-solver/openfoam_solver.cpp` — the
  immediate downstream consumer (Sprint 9 push 3 will land per-patch
  routing on top of this surface).
- `scripts/check-frozen-headers.sh` — the CI gate that accepts this
  PR via the `Ratchet: additive minor surface (ADR-0008)` marker.

## History

- 2026-05-11 (Sprint 6 push 4): first minor ratchet — `reader.*`
  capability surface (v1.0 → v1.1).
- 2026-05-11 (Sprint 7 push 3): second minor ratchet — mmap-backed
  buffer protocol (v1.1 → v1.2).
- 2026-05-11 (Sprint 9 push 2): **third minor ratchet — per-face-tag
  surface, this ADR** (v1.2 → v1.3).
