# stl-cube

The first souxmar pipeline driven by a real input file. Sprint 6 push 4 lands the `reader.*` C ABI surface (ABI v1.1) and the always-on `reader.stl` plugin; this example exercises both.

Pipeline:

```
reader.stl   →   postproc.mesh_quality   →   writer.vtu
   ↓                       ↓                       ↓
  Tri3 mesh         per-cell quality field      cube.vtu
```

What you get when you run it:

- `cube.stl` (the input) — 12 ASCII facets forming a unit cube.
- `reader.stl` deduplicates the facet vertices into **8 nodes, 12 Tri3 cells**.
- `postproc.mesh_quality` writes a `mesh_quality` vector field (signed area / edge ratio / min dihedral [NaN for 2D]).
- `writer.vtu` emits `cube.vtu` — drop it into ParaView and colour by the field components.

## Run it

```bash
cmake --preset dev -DSOUXMAR_BUILD_EXAMPLES=ON
cmake --build --preset dev

cd examples/stl-cube
souxmar --plugin-path <build>/examples/plugins run pipeline.yaml
```

You should see:

```
[OK     ] read     hash=<hex>
[OK     ] quality  hash=<hex>
[OK     ] write    hash=<hex>
```

and a `cube.vtu` file next to `pipeline.yaml`.

## What's next

This is the cantilever-beam upgrade path. The OCCT-backed STEP reader (`reader.step`, opt-in via `-DSOUXMAR_WITH_OPENCASCADE=ON`) lands the same Sprint 6 push but is gated behind an external OpenCASCADE install. Once OCCT is on your machine, swap `reader.stl` for `reader.step` and the rest of the pipeline keeps working with no other changes — that's the whole point of the namespace contract.
