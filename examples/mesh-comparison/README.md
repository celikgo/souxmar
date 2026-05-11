# mesh-comparison — running two meshers against the same geometry

The fourth in-tree example (Sprint 10 push 11). The point of this
study is *empirical*: a user who can choose between
`mesher.tetra.grid` (always-on, structured) and `mesher.tetra.gmsh`
(opt-in, Gmsh-backed) gets a side-by-side mesh-quality
comparison from a single `python compare.py` invocation. No
hand-curated screenshots, no "this one is better trust me"
prose — just the per-cell quality histograms and a summary table.

This is a study, not a swap-test. The swap-test
(`examples/swap-mesher/`) proves the two meshers are *interchangeable*
under the same pipeline; this example proves they're not *identical*
and gives the user a tool for picking between them on their geometry.

## What it produces

Running `compare.py` executes both pipelines against the same input
geometry (`cube.step`), gathers per-cell quality metrics from the
resulting VTUs, and writes:

* `report.md` — a markdown summary with per-metric histograms
  rendered as inline base64 PNGs (or as ASCII bars when matplotlib
  is missing).
* `metrics-grid.json` and `metrics-gmsh.json` — the raw per-cell
  arrays the report was rendered from. Consume these from a
  notebook for deeper analysis.

The report's summary table answers four questions:

| Question                          | Source           |
| --------------------------------- | ---------------- |
| Which mesher produced more cells? | `cell_count` in each metrics file |
| Which had a better worst-case Jacobian? | `jacobian` p1 (1st-percentile) |
| Which had a tighter quality spread? | `quality` IQR |
| Which ran faster?                 | Wall-clock timing of each `souxmar run` |

## Files

* `pipeline-grid.yaml` — runs `mesher.tetra.grid` against
  `cube.step`. Always-on; runs without Gmsh.
* `pipeline-gmsh.yaml` — runs `mesher.tetra.gmsh`. Requires Gmsh
  (`-DSOUXMAR_WITH_GMSH=ON` at configure time + a Gmsh install on
  the host).
* `compare.py` — driver. stdlib + optional matplotlib. Runs both
  pipelines via `souxmar run`, parses the VTU output of each,
  emits the report.
* `cube.step` — a tiny STEP body. **Not committed** for the same
  reason `examples/swap-mesher/cube.step` isn't — binary CAD
  fixtures violate the repo-hygiene rules. Generate one via the
  one-line OCCT script in
  `examples/swap-mesher/README.md` § "Generating cube.step".

## Running the study

```bash
# 1. Configure with the required adapters.
cmake --preset dev \
  -DSOUXMAR_BUILD_EXAMPLES=ON \
  -DSOUXMAR_WITH_OPENCASCADE=ON \
  -DSOUXMAR_WITH_GMSH=ON
cmake --build --preset dev

# 2. Drop a cube.step here. See examples/swap-mesher/README.md if
#    you need one.
cp /path/to/cube.step examples/mesh-comparison/cube.step

# 3. Run the study.
cd examples/mesh-comparison
python compare.py \
  --souxmar    ../../build/dev/src/cli/souxmar \
  --plugin-dir ../../build/dev/examples/plugins
```

The script prints progress to stdout and writes `report.md` +
`metrics-*.json` to the cwd. The whole study takes < 30 s on a
modern desktop.

## Why this is a fourth example, not a doc

Documentation about mesh-quality trade-offs is everywhere on the
internet. What's missing from the open-source CAE world is an
*end-to-end runnable study* — the same geometry, the same metric
definitions, a one-command apples-to-apples comparison. This
example *is* that study, parameterised so the user can swap in
their own geometry and get a meaningful answer about which
mesher fits their case.

In Sprint 10 push 11 this study runs the two in-tree meshers; the
follow-up (Sprint 11+) extends it to any conforming `mesher.tetra.*`
plugin so a third-party mesher author can drop their plugin into
the comparison without rewriting the script. The plugin marketplace
(Sprint 16) surfaces this report as the canonical "does this
mesher actually deliver?" evidence.

## Caveats this study does NOT compensate for

* **Same target_size doesn't mean same cell count.** The grid
  mesher's target_size sets cell edge length; Gmsh's interprets
  it as a characteristic length that respects features. Expect
  Gmsh to produce 20-40% more cells at the same nominal target
  size for any geometry with non-trivial curvature.
* **Quality metric definitions are convention-dependent.** This
  study uses souxmar's `postproc.mesh_quality` convention
  (Jacobian normalised to [0, 1], aspect ratio = longest /
  shortest edge per cell). Gmsh's GUI uses different normalisations
  for the same metric names; numbers will diverge from what Gmsh
  reports natively.
* **Runtime depends on Gmsh's parallelism settings.** Gmsh defaults
  to single-threaded on most builds; set `OMP_NUM_THREADS` if you
  want the comparison to reflect Gmsh's actual capability.

These caveats are reproduced verbatim in the generated `report.md`
header so a user who skips this README still sees them.
