# Cantilever beam — runnable end-to-end pipeline

This example is the canonical "does the souxmar pipeline actually work end
to end on my machine?" smoke check. It exercises the full critical path
that landed in Sprint 3:

```
YAML  →  parse  →  DAG validate  →  cache lookup
      →  C ABI dispatch into the mesher plugin
      →  C ABI dispatch into the writer plugin
      →  ParaView-readable .vtu on disk
```

## What this *is*

A tiny pipeline declaring two stages:

1. `mesh` — invokes the `mesher.tetra.hello` capability.
2. `write` — invokes `writer.vtu`, consuming the mesh from stage 1 and
   producing `cantilever.vtu` in the current working directory.

## What this *is not* (yet)

The `mesher.tetra.hello` plugin is a **placeholder** that emits a single
unit tetrahedron regardless of input. It exists so the rest of the pipe is
provable today, without waiting on the OpenCASCADE adapter or the native
tetrahedral mesher.

In Sprint 6 the geometry stage gains an OpenCASCADE-backed loader (`.step`
input) and the mesher swaps to `mesher.tetra.native`. The YAML at that
point will look like:

```yaml
- id: cad
  plugin: reader.cad.step
  input:
    path: cantilever.step

- id: mesh
  plugin: mesher.tetra.native
  input:
    geometry: { from: cad }
    target_size: 0.05
```

The `writer.vtu` stage and the `souxmar run` invocation stay identical.

## Running it

From a configured build (`cmake --preset dev && cmake --build --preset dev`):

```sh
# Path to the built CLI binary
SOUXMAR=./build/dev/src/cli/souxmar

# Plugin search root — the discovery layer scans its immediate subdirectories,
# each containing a souxmar-plugin.toml beside its built shared library.
PLUGINS=./build/dev/examples/plugins

$SOUXMAR run examples/cantilever-beam/pipeline.yaml --plugin-path $PLUGINS
```

Expected output:

```
Running pipeline pipeline.yaml (2 stages, 3 capabilities available)
  [OK      ] mesh   hash=...
  [OK      ] write  hash=...

pipeline ok (2 stages)
```

`cantilever.vtu` is now in your working directory. Open it in ParaView (or
inspect with `head` — it is plain XML for the small case).

## Re-running

The pipeline cache is keyed on inputs + plugin version + transitive
upstream hashes. A second `souxmar run` with no source changes will mark
both stages `[CACHED]` and skip dispatch entirely.

To force re-execution: `--no-cache`, or delete the on-disk cache directory
(see `docs/PLUGIN_SDK.md` for the lookup order).
