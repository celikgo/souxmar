# Thermal-fin — second runnable example

A 4-stage pipeline exercising Sprint 5's full capability chain:

```
mesh        (mesher.tetra.hello)
 ↓
heat        (solver.heat.linear)         — 5-step time series, scalar nodal Field
 ↓
magnitude   (postproc.scalar_magnitude)  — Field-in / Field-out via the
 ↓                                          new postproc.* C ABI (ADR-0005)
write       (writer.vtu)                 — ParaView-readable mesh on disk
```

This is the souxmar v1 capability surface end to end. The same YAML
runs unchanged once the Sprint 6 Gmsh adapter lands — the only
difference is `mesher.tetra.hello` becomes a real `mesher.tetra.gmsh`
on a CAD-loaded fin geometry.

## What this *is*

A demonstration that:

- A multi-step temperature Field round-trips the C ABI cleanly.
- The new `postproc.*` namespace dispatches Field-in / Field-out
  operations (the missing piece pre-Sprint-5).
- The pipeline runner's stage-graph + cache + dispatcher hold up
  across the four-namespace chain.

## What this *is not* (yet)

The mesher is `mesher.tetra.hello`, which produces a unit tet
regardless of input. The "thermal fin" geometry is therefore
metaphorical — the *plumbing* is what we're exercising. Sprint 6
swaps in:

- `reader.cad.step` + `mesher.tetra.gmsh` for the geometry → mesh path
- A real heat-conduction solver (FEniCSx adapter, Sprint 8) that
  honors boundary conditions instead of the current analytic toy

The pipeline YAML structure stays identical across both transitions.

## Running it

From a configured Sprint 5 build:

```sh
# Path to the built CLI
SOUXMAR=./build/dev/src/cli/souxmar

# Plugin search root (the dispatcher walks its immediate subdirs)
PLUGINS=./build/dev/examples/plugins

$SOUXMAR run examples/thermal-fin/pipeline.yaml --plugin-path $PLUGINS
```

Expected output:

```
Running pipeline pipeline.yaml (4 stages, N capabilities available)
  [OK      ] mesh        hash=...
  [OK      ] heat        hash=...
  [OK      ] magnitude   hash=...
  [OK      ] write       hash=...

pipeline ok (4 stages)
```

`thermal-fin.vtu` is now in your working directory.

## Inspecting the Field via the agent

The agent's `read_geometry_summary` + `mesh` + `solve` + `query_field`
+ `compute_field` chain is the same sequence the pipeline above runs,
but exposed to an LLM. From the CLI:

```sh
# Inspect the field that the heat solver produced
$SOUXMAR agent invoke query_field --input '{}'
```

The `query_field` tool reports min / max / mean / count / location /
kind over the current session's field handle. The `compute_field`
tool dispatches `postproc.*` capabilities the same way the
`magnitude` pipeline stage does.

See `docs/AI_INTEGRATION.md` for the full agent surface.

## What to vary

The interesting knobs in `pipeline.yaml`:

| Stage  | Field            | Effect                                                      |
| ------ | ---------------- | ----------------------------------------------------------- |
| `heat` | `num_time_steps` | Number of time slices stored in the output Field            |
| `heat` | `dt`             | Time-step size                                              |
| `heat` | `tau`            | Relaxation constant — smaller → faster approach to steady   |
| `heat` | `t_steady`       | Steady-state temperature target                             |
| `write`| `path`           | Where the VTU lands                                         |

The pipeline cache is keyed on these inputs. Change a value, the
affected stage + everything downstream re-runs; unchanged stages hit
the in-memory cache (and, with `--cache-dir`, the on-disk cache too).

## Comparing to the cantilever example

| Aspect                | `cantilever-beam`        | `thermal-fin`               |
| --------------------- | ------------------------ | --------------------------- |
| Stages                | 2 (mesh + write)         | 4 (mesh + heat + post + write) |
| Capability namespaces | `mesher.*`, `writer.*`   | + `solver.*`, `postproc.*`  |
| Exercises Field?      | No                       | Yes (5-step time series)    |
| Goal                  | First end-to-end demo    | Sprint 5 v1 capability tour |
