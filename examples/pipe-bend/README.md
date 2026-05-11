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

1. `mesh`  — invokes `mesher.tetra.hello` (placeholder unit-tet today).
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

- **The geometry is a placeholder unit tet.** `mesher.tetra.hello`
  doesn't read a CAD file. A follow-on push wires `reader.obj` (Sprint 8
  push 3) to a `pipe-bend.obj` fixture under this directory and swaps
  the `mesh` stage to consume it. The solver + writer stages stay
  identical — that's the point of the in-process pipeline contract.
- **BC routing is uniform-wall only.** The Sprint 8 push 4 CFD-aware
  tools (`apply_inlet` / `apply_wall` / `apply_outlet`) stage BCs on
  the session bag, but `openfoam-solver` currently writes a single
  `walls` boundary patch (every boundary face → no-slip wall). The
  patch-level BC routing requires per-face tag exposure on the C ABI
  (additive minor) and lands in Sprint 9.
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
2. The Tet4 → polyMesh translator (push 6 of Sprint 8) walks the
   mesh's Tet4 cells, deduplicates faces by canonical sorted-vertex
   key, partitions internal from boundary, and emits the OpenFOAM
   polyMesh format. (The unit-tet placeholder from `mesher.tetra.hello`
   produces 1 cell, 4 boundary faces, 0 internal faces.)
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
