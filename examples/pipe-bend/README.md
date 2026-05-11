# Pipe-bend — CFD end-to-end example

This is Sprint 8's runnable CFD example. It exercises the full critical
path that landed across Sprint 8:

```
YAML  →  parse  →  DAG validate
      →  C ABI dispatch into the mesher plugin
      →  C ABI dispatch into the CFD solver plugin
         (cfd-stub in default CI; openfoam-solver on the nightly matrix)
      →  C ABI dispatch into the writer plugin
      →  ParaView-readable .vtu on disk
```

## What this *is*

A three-stage pipeline declaring:

1. `mesh`  — invokes `reader.obj` against `pipe-bend.obj` (this directory).
   The fixture is a 12-vertex L-shaped duct: two unit cubes meeting at
   y=1, with named `usemtl` groups `inlet` (single quad at x=0),
   `outlet` (single quad at y=2), and `walls` (the remaining 8 quads).
   The reader fan-triangulates each quad into 2 Tri3 cells and preserves
   the `usemtl` material as a per-cell tag (Sprint 9 push 5): cells from
   the `inlet` group get tag 1, cells from `walls` get tag 2, cells from
   `outlet` get tag 3 — the tag-id assignment is source-order based and
   deterministic.
2. `solve` — invokes `solver.cfd.simple` for a steady inlet-magnitude
   1.5 m/s flow in the +x direction.
3. `write` — invokes `writer.vtu`, producing `pipe-bend.vtu`.

The `solver.cfd.simple` capability is served by **two** plugins:

| Plugin           | Build gate            | Behaviour                                                          |
| ---------------- | --------------------- | ------------------------------------------------------------------ |
| `cfd-stub`       | always on             | Closed-form uniform velocity field; microseconds; no I/O. Default CI. |
| `openfoam-solver`| `SOUXMAR_WITH_OPENFOAM=ON` + `simpleFoam` on `$PATH` | Generates a full OpenFOAM v12 case under `temp_directory_path()`, runs `simpleFoam` via subprocess per ADR-0009, reads back the final-timestep velocity. Nightly CFD-bearing matrix. |

Both serve the same capability id `solver.cfd.simple`, so swapping
between them is a build-time flag — the pipeline YAML doesn't change.

## What this *is not* (yet)

- **The mesh is a surface, not a volume.** `pipe-bend.obj` is a
  triangulated boundary surface (Tri3 cells); CFD needs a volume mesh
  (Tet4 / Hex8 / Prism6 / Pyramid5) to run through `openfoam-solver`.
  The chain is shape-agnostic against `cfd-stub` (the always-on default
  path — it just writes a uniform velocity at every node) but the
  nightly OpenFOAM path needs a tetrahedraliser in the chain. The
  natural choice is `gmsh-mesher` (opt-in via `SOUXMAR_WITH_GMSH=ON`,
  which embeds Gmsh as a library and exposes `mesher.tetra.gmsh`).
  The follow-on for full nightly CFD is gmsh-mesher's per-cell-tag →
  per-face-tag preservation: when Gmsh tetrahedralises the surface,
  each tet boundary face inherits the `souxmar_mesh_face_tag` of the
  surface Tri3 cell it descended from. Until that lands, the OpenFOAM
  matrix runs against synthetic Tet4 fixtures (the Sprint 8 push 2
  unit-tet path) rather than against real OBJ-driven geometry.
- **BC routing respects per-face tags end-to-end** (Sprint 9 push 3).
  The openfoam-solver groups boundary faces by `souxmar_mesh_face_tag`
  (the ABI v1.3 surface from ADR-0012) and emits one polyMesh patch
  per matching BC, with the corresponding `0/U` and `0/p` boundaryField
  entries. Untagged faces fall through to a legacy "walls" patch, so
  meshes built without per-face tags (or chains that lose the tag
  info during tetrahedralisation) still produce valid OpenFOAM cases.
- **The cfd-stub field is uniform.** It does not solve any PDE; it
  just produces `(magnitude · direction)` at every node. Good enough
  for the agent eval suite and for the conformance gate; not good
  enough for any actual CFD insight. Use the OpenFOAM matrix for
  numerical results.

## Running it — default CI shape (no external deps)

From a configured build (`cmake --preset dev && cmake --build --preset dev`):

```sh
SOUXMAR=./build/dev/src/cli/souxmar
PLUGINS=./build/dev/examples/plugins

$SOUXMAR run examples/pipe-bend/pipeline.yaml --plugin-path $PLUGINS
```

Expected output:

```
Running pipeline pipeline.yaml (3 stages, N capabilities available)
  [OK      ] mesh   hash=...
  [OK      ] solve  hash=...
  [OK      ] write  hash=...

pipeline ok (3 stages)
```

`pipe-bend.vtu` is now in your working directory. Every node carries
a uniform `(1.5, 0, 0)` velocity vector — that's the cfd-stub
contract.

## Running it — nightly OpenFOAM shape

```sh
# Configure with OpenFOAM enabled. `simpleFoam` must be on $PATH —
# source OpenFOAM's bashrc before reconfiguring:
source /opt/openfoam12/etc/bashrc
cmake --preset dev -DSOUXMAR_WITH_OPENFOAM=ON
cmake --build --preset dev

$SOUXMAR run examples/pipe-bend/pipeline.yaml --plugin-path $PLUGINS
```

`openfoam-solver` is registered for the same `solver.cfd.simple`
capability. The pipeline runner picks it (capability registration is
last-wins; configuring with OpenFOAM keeps both plugins loadable, and
discovery order chooses openfoam-solver in this build).

What happens under the hood:

1. The plugin generates a full OpenFOAM case under
   `/tmp/souxmar-openfoam-XXXX/` —
   `constant/polyMesh/{points,faces,owner,neighbour,boundary}`,
   `system/{controlDict,fvSchemes,fvSolution}`, `0/{U,p}`,
   `constant/{transport,turbulence}Properties`.
2. The polyMesh translator (Sprint 8 push 6, generalised in Sprint 9
   push 4 to all linear 3D element types — Tet4 / Hex8 / Prism6 /
   Pyramid5) walks the mesh's cells, looks up each cell's per-element
   face table, deduplicates faces by canonical sorted-vertex key
   (with the vertex count threaded through the key so tris and quads
   never collide), partitions internal from boundary, and emits the
   OpenFOAM polyMesh format. Higher-order variants (Tet10, Hex20,
   etc.) are rejected with a clean diagnostic — polyMesh doesn't
   carry mid-edge nodes natively. (The unit-tet placeholder from
   `mesher.tetra.hello` produces 1 cell, 4 triangular boundary faces,
   0 internal faces.)
3. `simpleFoam -case /tmp/souxmar-openfoam-XXXX/` runs as a subprocess
   per ADR-0009 with a mandatory 1-hour wall-clock timeout. Stdout +
   stderr are stream-capped at 64 KiB per stream and surface in the
   audit log.
4. The final-timestep velocity is read back into a souxmar Field.
5. The work directory is removed.

## Re-running

The pipeline cache is keyed on inputs + plugin version + transitive
upstream hashes. A second `souxmar run` with no source changes marks
all three stages `[CACHED]` and skips dispatch entirely.

`--no-cache` forces re-execution.

## References

- [`examples/plugins/cfd-stub`](../plugins/cfd-stub) — the always-on solver.
- [`examples/plugins/openfoam-solver`](../plugins/openfoam-solver) — the opt-in adapter.
- [ADR-0009](../../docs/adr/0009-openfoam-process-isolation.md) — subprocess-only invocation of GPL OpenFOAM.
- [`docs/SPRINT_PLAN.md` § Sprint 8](../../docs/SPRINT_PLAN.md) — the sprint plan that landed this work.
