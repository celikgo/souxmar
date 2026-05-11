# Your first pipeline

A 10-minute walkthrough of running the cantilever-beam example from
download to a viewable `.vtu` file.

## What you'll do

1. Install souxmar (CLI path — desktop app's workbench-shell story
   is still scaffolding at v0.9.0).
2. Run the canonical cantilever-beam pipeline.
3. Open the resulting `.vtu` in ParaView (or use the Python loader
   in `pysouxmar`).

Total wall-clock: ~10 minutes assuming the install is done.

## Step 1: Verify the install

```sh
souxmar version
# souxmar 0.9.0 (ABI v1)

souxmar plugin list
# dev.souxmar.examples.hello-mesher (0.9.0)
#   capabilities:
#     - mesher.tetra.hello
# dev.souxmar.examples.vtu-writer (0.9.0)
#   capabilities:
#     - writer.vtu
# … (and other in-tree plugins)
```

If `plugin list` is empty, set `$SOUXMAR_PLUGIN_PATH` to the
plugins directory the install put on disk — see [the install
guide](/guide/install) § "Plugin search path."

## Step 2: Pick the example

souxmar ships several runnable examples in the source tree under
`examples/`. The simplest is `cantilever-beam`. If you installed
from a binary release, the example lives at:

| Platform | Path                                                         |
| -------- | ------------------------------------------------------------ |
| macOS    | `/Applications/souxmar.app/Contents/Resources/examples/cantilever-beam` |
| Linux    | `/usr/share/souxmar/examples/cantilever-beam`                |
| Windows  | `C:\Program Files\souxmar\examples\cantilever-beam`          |

If you built from source it's at `examples/cantilever-beam` in
your checkout.

## Step 3: Run it

```sh
cd <path-to-cantilever-beam>
souxmar run pipeline.yaml
```

What this does, stage by stage:

1. **mesher.tetra.hello** generates a unit tetrahedron (4 nodes,
   1 cell). It's the smallest possible mesh — proves the pipeline
   wiring without any heavy meshing dependency.
2. **writer.vtu** serialises that mesh to `cantilever.vtu` in
   the current directory.

You'll see output like:

```text
[ OK     ] mesh        plugin=hello-mesher       12.4ms
[ OK     ] write       plugin=vtu-writer          3.2ms  -> cantilever.vtu
pipeline ok (2 stages)
```

The second run uses the disk cache — the `write` stage outputs are
content-hashed, so re-running with the same inputs hits the cache:

```sh
souxmar run pipeline.yaml
# [ OK     ] mesh        plugin=hello-mesher       12.6ms
# [CACHED  ] write       plugin=vtu-writer           0ms  -> cantilever.vtu
```

## Step 4: Open the result

::: code-group

```sh [ParaView]
# If ParaView is installed:
paraview cantilever.vtu
```

```python [pysouxmar]
# In a Python REPL:
import pysouxmar
mesh = pysouxmar.read_vtu("cantilever.vtu")
print(f"{mesh.num_nodes} nodes, {mesh.num_cells} cells")
# 4 nodes, 1 cells
```

```sh [meshio]
# Quick stats without ParaView:
pip install meshio
python -c "import meshio; m = meshio.read('cantilever.vtu'); \
           print(m); print('cells:', sum(len(c.data) for c in m.cells))"
```

:::

## What's next

- Swap in a real geometry source: drop in an OBJ file via
  `reader.obj` (always-on, Sprint 8 push 3) or wire in OCCT for
  STEP / IGES (`-DSOUXMAR_WITH_OPENCASCADE=ON`).
- Try the swap-mesher exercise:
  [`examples/swap-mesher/`](https://github.com/souxmar/souxmar/tree/master/examples/swap-mesher)
  shows the one-line `mesher.tetra.grid` → `mesher.tetra.gmsh` swap
  through the same pipeline.
- Run the mesh-comparison study:
  [`examples/mesh-comparison/`](https://github.com/souxmar/souxmar/tree/master/examples/mesh-comparison)
  compares both meshers against the same geometry, renders an
  HTML quality report.
- Read the [concepts page](/guide/concepts) for the pipeline's
  data model + the stage/plugin separation that makes swaps
  one-liners.

## Troubleshooting

::: details The mesher dispatch fails with `SOUXMAR_E_MISSING_CAPABILITY`

`souxmar` couldn't find a plugin claiming the capability the
pipeline asked for. Two common causes:

- The plugin isn't installed. Run `souxmar plugin list` to confirm.
- The plugin is installed at a non-default path. Add
  `--plugin-path <dir>` to the `souxmar run` command, or set
  `$SOUXMAR_PLUGIN_PATH`.

:::

::: details The `write` stage fails with `permission denied`

The pipeline writes `cantilever.vtu` to the current directory.
Either `cd` to a writable directory, or override the output path
in the `write:` stage's `path:` input.

:::
