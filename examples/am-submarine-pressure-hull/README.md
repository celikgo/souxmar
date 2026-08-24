# am-submarine-pressure-hull — a 300 m bay and the lattice that floats inside it

Nine stages, five manufacturing capabilities, and both of the block's new
geometry sources:

```
reader.obj      hull-ring.obj            faceted 1.0 m dia shell bay
reader.lattice  buoyancy-core.lattice    parametric octet strut lattice
  ├─ solver.marine.hydrostatic  × 2      pressure per depth load case
  └─ solver.marine.hull_collapse         collapse margin, analytical
        ├─ writer.am.cli                 Common Layer Interface slice file
        ├─ writer.vtu × 2
        └─ writer.marine.qualification_report   advisory dossier
```

## Status of these numbers — read this first

**The outputs of this pipeline are preliminary sizing estimates.**
`solver.marine.hull_collapse` is a set of textbook closed-form shell
expressions with two assumed knockdown factors. It is not a nonlinear buckling
analysis, it does not know the shape of your out-of-circularity, it does not
model the ring frames as structure, and it has no material nonlinearity.

**souxmar is not a classification society and nothing here is approval.** A
real pressure hull is signed off against a class rule — the naval and
underwater-vehicle rules of DNV, ABS, Lloyd's and their peers all prescribe
their own collapse formulations, their own tolerance-derived knockdowns and
their own testing regime — on a hull whose out-of-circularity has been
*measured*. `writer.marine.qualification_report` emits an evidence *checklist*
derived from publicly documented AM qualification practice so you can see what
that process asks for. Filling it in is not the same as passing it.

Nobody should get in a submarine on the strength of this file.

## What this models

One **0.5 m frame bay of a 1.0 m diameter, 12 mm wall 316L pressure hull rated
to 300 m**, checked at three depth factors — 1.0 operating, 1.5 test, 2.25
collapse — plus an **8%-dense octet lattice buoyancy core** sized to fit inside
the bore.

| | |
| --- | --- |
| hull outer diameter | 1.000 m exactly |
| wall thickness | 12 mm (R 0.500 / 0.488) |
| bay length | 0.500 m — also the ring-frame spacing |
| shell volume / mass | 18 594 cm³ / **148.6 kg** of 316L |
| shell mesh | 896 nodes, 896 quads → **1792 Tri3 cells**, watertight, oriented |
| lattice envelope | 0.30 × 0.30 × 0.40 m, centred on the hull axis |
| lattice | 6 × 6 × 8 octet cells → 1437 nodes, **7440 Edge2 struts** |

## Two frames, and the difference matters

The OBJ's cylinder axis is **+Z**. That is the *manufacturing* frame: +Z is the
deposition direction, so a slicer sees 0.5 m of stacked annuli, which is what
`writer.am.cli` wants.

In service the hull axis lies **horizontal**. So the two hydrostatic stages
declare `build_direction: [0.0, 1.0, 0.0]` — "up" is +Y, and the pressure
gradient runs over the 1.0 m of hull *diameter* rather than along the hull
axis. `solver.marine.hydrostatic` uses `build_direction` as the up-axis for
depth measurement, not as a build direction, and this is the example where the
two genuinely differ.

Get it backwards and you get a 0.5 m head gradient along the wrong axis, which
is 5 kPa out of 3 MPa — far too small to notice in a plot and completely wrong
as a model. Hence this section.

## The geometry

`hull-ring.obj` is produced by [`generate-geometry.py`](generate-geometry.py) —
Python 3 standard library only, no numpy, deterministic, reproduces the
committed file byte-for-byte:

```sh
python3 generate-geometry.py            # write hull-ring.obj
python3 generate-geometry.py --check     # verify only
```

It builds two concentric grids of 65 × 64 vertices and closes the annulus at
both ends, then verifies before writing: all face indices in range, no
degenerate triangles, **oriented 2-manifold** (every directed edge exactly
once, zero open edges), and **positive enclosed volume** by the divergence
theorem, which proves the winding points out of the metal. It triangulates the
way `obj-reader` does, so the counts it prints are the counts the reader
produces.

Two honest caveats about it:

* **It is faceted.** 64 circumferential facets on a 1 m circle gives a 49 mm
  chord against a 12 mm wall, so the facet chord is four wall thicknesses long.
  The faceted volume is 0.16% below the exact annulus (18 594 vs 18 623 cm³) —
  that is the inscribed-polygon deficit and it is the *good* news. The bad news
  is that a faceted shell is not a shell: any real buckling analysis on this
  mesh would find the facet edges, not the mode. Since the collapse capability
  here is analytical and never touches the mesh, the faceting costs nothing in
  this pipeline. It would matter enormously in a real one.
* **It contains no ring frames.** A ring-stiffened hull is shell plus internal
  T-frames, and the frames are what set the interframe collapse mode. They are
  represented here as a *number* — `unsupported_length: 0.5`, the frame spacing
  — because `solver.marine.hull_collapse` is analytical and reads the frame
  spacing as an input rather than measuring it off the mesh. So the OBJ is one
  bay's shell and the frames live in the YAML, and you have to keep the two in
  step by hand.

Like the propeller example, the OBJ names its surfaces (`shell_outer`,
`shell_inner`, `frame_land_aft`, `frame_land_fwd`) with `g` groups and **not**
`usemtl`: `obj-reader` turns `usemtl` names into per-cell tags, and every AM
capability reads a non-negative cell tag as a *layer index*, so a tagged shell
would be simulated as a four-layer part. The OBJ header spells this out.

## The buoyancy core

`buoyancy-core.lattice` is a spec file for `reader.lattice`, not a mesh. The
format is UTF-8 text, `#` comments, one `key = value` per line, with numbers,
bare strings and `[a, b, c]` lists — and every key in it is also a YAML input
key for the stage, where a YAML value **overrides** the file.

`reader.lattice` exists as a *reader* rather than a mesher for a hard
architectural reason: the C plugin ABI gives meshers no value bag. A mesher's
entry point sees only `target_size`, `optimize`, `element_order` and
`random_seed`, so a fully parametric generator cannot be a mesher. Readers do
get the value bag, so parametric generation lives behind `reader.*`.

What the file asks for, and what it produces:

| | |
| --- | --- |
| envelope | 0.30 × 0.30 × 0.40 m, inset from the 0.976 m bore, z = 0.05 … 0.45 |
| unit cell | `octet` — stretch-dominated, so stiffness scales linearly with density rather than quadratically |
| cell size | 50 mm, pinned as 6 × 6 × 8 cells |
| relative density | 0.08 |
| output | 1437 nodes, **7440 Edge2 struts**, 263.0 m of total strut length |
| derived strut diameter | **3.73 mm**, from ρ* = (πd²/4)·L/V |

Strut tags carry the strut *family* (0 X-axial, 1 Y-axial, 2 Z-axial, 3
body-diagonal, 4 face-diagonal), which is why the lattice can be coloured by
topology in ParaView.

### Does it float?

That is the whole question for a buoyancy module, so here is the arithmetic:

| quantity | value |
| --- | --- |
| envelope volume | 0.036 m³ |
| seawater displaced at 1027 kg/m³ | **37.0 kg** |
| lattice mass at 8% × 7990 kg/m³ | **23.0 kg** |
| net, before the sealed skin | **+14.0 kg** |

So yes, marginally, and only because the relative density is low. At the
reader's default 15% the same block weighs 43 kg and sinks. The buoyancy comes
from the **air trapped in the cells**, which means the module needs a sealed
skin that this example does not model; the lattice's job is to carry the
external pressure so that skin does not have to. Subtract the skin's mass from
the +14 kg before believing anything.

To sweep the density without touching the spec file, add the override to the
stage:

```yaml
  - id: core
    plugin: reader.lattice
    input:
      path: buoyancy-core.lattice
      relative_density: 0.10        # overrides the 0.08 in the file
```

Note also that a 0.30 × 0.30 × 0.40 m envelope is outside most 250–300 mm
powder-bed build volumes. In practice you would print this as four stacked
100 mm modules, or on a large-format machine. The example keeps it as one block
because `reader.lattice` is parametric and splitting it adds nothing.

And the relative-density → strut-diameter relation ignores node overlap, so it
**over-predicts density above about 0.30**. At 0.08 that is not a concern; if
you sweep upward, it becomes one.

## Running it

```sh
cd examples/am-submarine-pressure-hull
souxmar run pipeline.yaml --plugin-path ../../build/dev/examples/plugins
```

Reader `path:` values resolve against the YAML's own directory, so both of
this example's inputs — `hull-ring.obj` and `buoyancy-core.lattice` — are
found beside the pipeline and it runs from anywhere. Output paths still
resolve against your **working directory**, hence `$PWD` below. One
`--plugin-path` is enough; discovery scans one level of subdirectories under
it.

Four files land in `$PWD`:

| file | what |
| --- | --- |
| `hull-ring-pressure.vtu` | nodal pressure, Pa, 3 steps |
| `buoyancy-core-pressure.vtu` | the same on the lattice (Edge2 cells — set Wireframe) |
| `hull-ring.cli` | 250-layer Common Layer Interface ASCII slice file |
| `hull-qualification-dossier.md` | advisory dossier around the collapse-margin field |

## Stage by stage

### 1–2. `ring` / `core` — `reader.obj`, `reader.lattice`

Two independent geometry sources feeding two independent branches that never
merge. The DAG handles that fine; there is no requirement that a pipeline be a
single chain.

### 3–4. `hydro` / `core-hydro` — `solver.marine.hydrostatic`

Output: `hydrostatic_pressure`, nodal scalar, **Pa**, one step per entry in
`depth_factors`.

`depth_factors: [1.0, 1.5, 2.25]` gives operating / test / collapse. The
topmost node along `build_direction` sits at `design_depth × factor` and deeper
nodes see proportionally more:
`p = ρ·g·(depth_of_top + (z_top − z_node))`, a positive magnitude, compressive
on the wetted surface.

| load case | depth | pressure at the crown |
| --- | --- | --- |
| operating (×1.0) | 300 m | **3.02 MPa** |
| test (×1.5) | 450 m | **4.54 MPa** |
| collapse (×2.25) | 675 m | **6.80 MPa** |

The field spans 1.0 m of head across the hull diameter, about 10.1 kPa, which
is 0.33% of the operating pressure. It is in there because it is real, not
because it changes an answer.

`seawater_density: 0.0` asks the solver to derive density from salinity and
temperature — the one-atmosphere EOS-80 / Millero & Poisson fit, which at
35 psu and 6 °C gives **1027.5 kg/m³**. Its validity band and its error are
documented in the plugin source; inputs are clamped to that band, so a nonsense
YAML value cannot produce a negative density.

`include_atmospheric: false`, so the field is **gauge** pressure. That is what
a structural check wants: the hull interior sits at 1 atm and only the
differential loads the shell. Set it true if you want absolute pressure for a
gas-law calculation.

**Why `core-hydro` uses `design_depth: 300.35` and not 300.0.** Each
hydrostatic stage measures depth from *its own mesh's* top. The hull crown is at
y = +0.5 m; the core's crown is at y = +0.15 m, so the core sits 0.35 m deeper
in the same water column. Shifting `design_depth` by that 0.35 m keeps the two
fields on one consistent datum. Leave both at 300.0 and the two meshes silently
float at different depths — a mistake that produces perfectly plausible
numbers.

### 5. `collapse` — `solver.marine.hull_collapse`

Output: `collapse_margin`, **per cell**, vector, 1 step — and the *same triple
on every cell*, because the numbers come from closed-form shell expressions,
not from the mesh. The mesh is only there to give the field somewhere to live.

| # | name | unit | meaning |
| --- | --- | --- | --- |
| 0 | `collapse_pressure_Pa` | Pa | predicted collapse pressure after knockdowns |
| 1 | `margin` | – | collapse ÷ factored design pressure |
| 2 | `governing_mode_code` | – | 0 interframe elastic instability, 1 membrane yield, 2 general instability, 3 sphere elastic buckling |

**Do the check by hand.** You should, and here it is for these inputs:

*Membrane yield* — the algebra is unambiguous:

```
p_y = 2·σ_y·t/D = 2 × 500 MPa × 0.012 / 1.0 = 12.0 MPa
```

*Interframe elastic instability*, Windenburg–Trilling (1934):

```
p_wt = 2.42·E·(t/D)^2.5 / [ (1−ν²)^0.75 · ( L/D − 0.45·(t/D)^0.5 ) ]
     = 2.42 × 1.9e11 × 1.577e-5 / [ 0.9406 × (0.5 − 0.0493) ]
     = 17.11 MPa
```

Now the knockdowns, and note that **they are not applied to both modes
equally** — out-of-roundness knocks down *buckling*, not yield strength, so the
imperfection factor touches the elastic modes only:

```
p_wt  17.11 MPa × 0.75 (imperfection) × 0.90 (AM anisotropy) = 11.55 MPa
p_y   12.00 MPa                       × 0.90 (AM anisotropy) = 10.80 MPa
```

Yield is lower after knockdown, so **yield governs**:
`collapse_pressure_Pa` ≈ **1.08e7** and `governing_mode_code` = **1**.

```
p_design = 1027.5 × 9.80665 × 300 = 3.02 MPa (gauge, atmospheric excluded)
margin   = 10.80 / (3.02 × 1.5)   ≈ 2.4
```

A 12 mm 316L bay at 1 m diameter is comfortable at 300 m, and it is
yield-limited rather than buckling-limited — which tells you something useful:
thickening the wall buys margin proportionally, whereas reducing the frame
spacing buys almost nothing (it only helps the mode that is not governing).
That is the kind of question this capability is for.

**Argue with both knockdowns.** `imperfection_knockdown: 0.75` stands in for
out-of-circularity and residual stress; a class rule would *derive* it from a
measured tolerance rather than assume it, and 0.75 is neither conservative nor
generous, it is a placeholder. `am_anisotropy_knockdown: 0.90` is the as-built
Z-direction penalty for LPBF/DED 316L — see
[`examples/materials/am-marine.toml`](../materials/am-marine.toml), where that
number lives with its own source line.

**And note what is missing from the mode list.** General instability of a
*ring-stiffened* cylinder — the mode where the frames and shell buckle together
over several bays — is frequently the governing mode for a real framed hull,
and this capability does not evaluate it: it would need the frame area, the
frame second moment of inertia and the bulkhead spacing, none of which are
inputs. `governing_mode_code` 2 is the *unstiffened* long-cylinder result and is
only evaluated for `hull_type: cylinder`. So a margin of 2.4 here means "passes
the two modes that were checked", and general instability **must** be checked
separately.

### 6. `slice` — `writer.am.cli`

Common Layer Interface ASCII: `$$HEADERSTART`, `$$ASCII`, `$$UNITS/…`,
`$$VERSION/200`, `$$LABEL/…`, `$$DATE/010100`, `$$DIMENSION/…`, `$$LAYERS/n`,
`$$HEADEREND`, `$$GEOMETRYSTART`, then per layer `$$LAYER/z` and
`$$POLYLINE/id,dir,n,x1,y1,…`, then `$$GEOMETRYEND`. CLI is the format metal
machines actually eat, and unlike G-code it carries contours rather than tool
moves, so it is the honest output for a metal slice.

`$$DATE` is fixed at `010100` because determinism is a gate in this repo and a
wall-clock read would break it. `units: 1.0` is millimetres per CLI unit, so
the part comes out in millimetres: a 500 mm stack.

The slicer walks the mesh's triangulated boundary with planes at layer
mid-heights and chains the segments into closed contours; for this geometry
that is two concentric 64-gons per layer, outer then inner. Contours are sorted
by (min y, min x) of their lowest point so the file is byte-reproducible.

**`layer_height: 0.002` is a preview resolution, not a process resolution.** A
real 40 µm slice of a 0.5 m tall part is 12 500 layers and a very large file;
2 mm gives 250 layers and runs instantly. And note that a 1.0 m diameter part
is far outside any powder-bed machine — this is a wire-arc or DED part, and
2 mm is a plausible bead layer for that process, so the preview resolution is
not as fictional as it sounds.

### 7–8. `write-*` — `writer.vtu`

`hull-ring-pressure.vtu` and `buoyancy-core-pressure.vtu`. The lattice VTU
carries Edge2 cells, which ParaView renders as lines: set the representation to
Wireframe and colour by pressure.

### 9. `dossier` — `writer.marine.qualification_report`

`application: hull`, `criticality: 1` (the top of the writer's 1..3 scale)
because a pressure-hull failure at depth is not survivable, and
`redundancy: false` for the same reason — there is no second hull. The dossier
renders the collapse-margin field as its simulation evidence, plus the
qualification-evidence checklist and the advisory-only disclaimer.

## What this pipeline ignores

* **Buckling as a real analysis.** No eigenvalue extraction, no imperfection
  shape, no geometrically nonlinear collapse, no frame tripping, no
  general-instability mode across multiple bays. Three of the four
  `governing_mode_code` values come from closed-form expressions that assume
  the mode they name.
* **The frames, the end closures and the penetrations.** A hull is 12 mm of
  plate *plus* frames, bulkheads, hatches, hull penetrators and cable glands,
  and the penetrations are where hulls actually leak. None of them are here.
* **Cyclic depth loading.** A submarine dives repeatedly. That is a fatigue
  problem, at as-built AM surface quality, and there is no fatigue data in this
  repo — see the end of `am-marine.toml` for why not.
* **Corrosion.** No corrosion stage in this pipeline. 316L is marginal in
  stagnant seawater, and a flooded free-flood space around this bay is exactly
  the stagnant-chloride crevice case it does worst in. Add a
  `solver.marine.corrosion` stage (see
  [`examples/am-marine-propeller`](../am-marine-propeller)) before believing a
  25-year life.
* **The lattice's actual strength.** `reader.lattice` produces a beam mesh; no
  stage in this pipeline puts a structural load through it. The hydrostatic
  field on the core tells you the pressure the module sees, not whether the
  struts hold.

## References

* [`examples/materials/am-marine.toml`](../materials/am-marine.toml) — 316L properties, the `am_anisotropy_knockdown` value, and the stagnant-seawater warning.
* [`examples/am-marine-propeller`](../am-marine-propeller) — the other marine example: corrosion and galvanic coupling.
* [`examples/am-lpbf-bracket`](../am-lpbf-bracket) — the metal AM process chain.
* `docs/MARINE.md` — the marine capability reference, including the collapse formulations and their citations.
* `docs/MANUFACTURING.md` — `reader.lattice`, `writer.am.cli` and the rest of the AM surface.
