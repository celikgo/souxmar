# am-marine-propeller — printing a bronze blade, then asking the sea about it

Nine stages, six manufacturing capabilities, one nickel-aluminium-bronze
propeller blade:

```
reader.obj  propeller-blade.obj
  ├─ solver.am.distortion.inherent_strain   deposition warp, layer by layer
  ├─ solver.am.overhang                     downskin tilt + support need
  ├─ solver.am.printability                 composite DfAM score
  └─ solver.marine.corrosion                25-year seawater exposure
        ├─ writer.vtu × 2
        ├─ writer.am.report                 build report
        └─ writer.marine.qualification_report   advisory dossier
```

## Status of these numbers — read this first

**The outputs of this pipeline are preliminary engineering estimates.** Every
model in the chain is closed-form or heuristic. The distortion model has one
uncalibrated free parameter. The corrosion rates are indicative literature
values, not design values. The printability score is a documented weighted
heuristic, not a manufacturability guarantee.

**souxmar is not a classification society and this pipeline produces no
approval of any kind.** `writer.marine.qualification_report` emits a
*checklist* derived from publicly documented AM qualification practice — the
sort of thing DNV-ST-B203 and the ABS additive-manufacturing guidance set out
— so that you can see what evidence a real qualification would require. It
does not assess that evidence, and a filled-in dossier is not a certificate.
Marine AM parts get qualified by a class society on real coupons, real NDT and
a real machine-qualification record. Use this to prepare for that conversation,
not to skip it.

## What this models

One blade of a **0.6 m diameter, four-blade NAB propeller** for a small
workboat, deposited **root-down in +Z** on a wire-arc (WAAM) cell at a 2 mm
bead layer, then put in seawater for 25 years.

The blade:

| | |
| --- | --- |
| diameter | 0.600 m (root at r/R = 0.30, i.e. r = 90 mm) |
| pitch ratio P/D | 0.90, so the section twist runs 43.7° at the root to 16.0° at the tip |
| skew | 22° cumulative at the tip |
| rake | 12 mm aft |
| chord | 108 mm at the root, 153 mm peak at r/R ≈ 0.65, 27 mm at the tip |
| max section thickness | 19.4 mm at the root, 2.4 mm at the tip |
| volume / mass | 264 cm³ / **2.01 kg** per blade (8.0 kg for four, plus the hub) |
| mesh | 672 nodes, 700 quads → **1340 Tri3 cells**, watertight, oriented |

Frame, in metres: **+X** is the shaft axis (thrust direction), **+Z** is the
blade reference line at the root so the blade grows outward in +Z, **+Y** is
tangential and is where skew displaces the sections. The blade therefore sits
root-down for a `[0, 0, 1]` build direction, which is how you would actually
deposit it.

## The geometry, and why it is generated rather than committed by hand

`propeller-blade.obj` is produced by
[`generate-geometry.py`](generate-geometry.py) in this directory — Python 3
standard library only, no numpy, fully deterministic, and it reproduces the
committed OBJ byte-for-byte:

```sh
python3 generate-geometry.py            # write propeller-blade.obj
python3 generate-geometry.py --check     # verify only
```

It lofts 21 radial sections of 32 points each, wraps each section onto the
cylinder of its own radius (a propeller section is a curve on a cylinder, not a
flat rib), and closes the ends with root and tip caps. The sections use a NACA
4-digit thickness form with the closed-trailing-edge coefficient (−0.1036, not
−0.1015) so the loft is watertight, over a parabolic mean line.

The script verifies its own output before writing and refuses to write if any
check fails:

* every face index in range, no degenerate triangles;
* **oriented 2-manifold** — every directed edge appears exactly once and every
  undirected edge exactly twice, i.e. zero open edges;
* **positive enclosed volume** by the divergence theorem, which proves the
  winding is consistently outward.

It triangulates exactly the way `obj-reader` does (fan from the first vertex)
so the counts it prints are the counts the reader will produce.

### It uses `g` groups, not `usemtl` — on purpose

The OBJ names its four surfaces `blade_back`, `blade_face`, `blade_root` and
`blade_tip` with OBJ `g` group directives. It would be more natural to use
`usemtl`, the way [`examples/pipe-bend`](../pipe-bend) does, and it would be
wrong here:

`obj-reader` maps each unique `usemtl` name to a sequential integer **per-cell
tag**. Every AM capability resolves layers by rule 1 of the contract's layer
resolution: *if the cell tag is non-negative, that value **is** the layer
index*. A `usemtl`-tagged blade would therefore be simulated as a **four-layer
part** with the whole suction side in layer 1 — silently, with no error.
Leaving the cells untagged (tag −1) sends every AM capability down its
documented fallback instead: bin the cell centroid along `build_direction` by
`layer_height`. `g` is ignored by `obj-reader`, so the group names survive for
Blender and MeshLab without touching the tags.

If you need surface groups *and* layer resolution on the same part, tag the
geometry in a separate reader stage and keep the AM chain on the untagged mesh.

### What the geometry is not

Not a hydrodynamic design. The chord, thickness, camber and skew distributions
are plausible round numbers for a wide-bladed workboat propeller, tabulated in
the script so you can read and argue with them, but nothing was lifted from a
published open-water series and no thrust, torque or efficiency claim is
implied. There is no hub, no fillet, no blade-root radius and no trailing-edge
detail. It exists to give the manufacturing capabilities a geometry with real
twist, real skew and a genuinely difficult tip.

## Running it

```sh
cd examples/am-marine-propeller
souxmar run pipeline.yaml --plugin-path ../../build/dev/examples/plugins
```

`path:` values in a pipeline resolve against your **working directory**, not
against the directory the YAML lives in, so this example has to be run from
here (or you edit `path: propeller-blade.obj` to something absolute). One
`--plugin-path` is enough — discovery scans one level of subdirectories under
it, and every in-tree plugin builds into
`build/dev/examples/plugins/<plugin-dir>/` next to a copy of its manifest.

Four files land in `$PWD`:

| file | what |
| --- | --- |
| `propeller-distortion.vtu` | nodal displacement vectors, metres, ~101 steps |
| `propeller-overhang.vtu` | per-cell overhang triple |
| `propeller-build-report.md` | build report rendered around the printability field |
| `propeller-qualification-dossier.md` | advisory qualification dossier |

## Stage by stage

### 1. `blade` — `reader.obj`

700 `f` lines fan-triangulate into 1340 Tri3 cells on 672 nodes. The mesh is a
**surface**, not a volume: every capability downstream of here is documented to
handle Tri3/Quad4 surface meshes, and where a volume would change the answer
(per-layer cross-sectional area in `solver.am.buildtime`, for instance) the
capability says how it degrades.

### 2. `distortion` — `solver.am.distortion.inherent_strain`

NAB elastic and thermal properties: E = 120 GPa, ν = 0.32, CTE = 16.2e-6 /K,
liquidus 1060 °C. `preheat_temperature: 150.0` is doing duty as the WAAM
**interpass** temperature, and it is deliberately low: NAB's κ-phase coarsens
if the interpass runs hot, which costs you both mechanical properties and the
protective-film behaviour that is the whole reason to use the alloy.

The blade spans z = 0.081 … 0.283 m, so a 2 mm layer height bins it into
**101 deposition layers** and the field carries one step per layer. Scrub it
and watch the tip walk sideways as the blade grows — that lateral drift is the
practical consequence of depositing a skewed blade root-first.

Derived inherent strain: `−cte · (T_melt − T_preheat) · strain_calibration` =
`−1.62e-5 × 910 × 0.30` = **−4.4e-3**, i.e. −0.44%.

**`strain_calibration: 0.30` is not calibrated.** Fit it against a measured
blade off your own cell before you believe a magnitude. Until then the field is
useful for *where* and *which way*, not *how much*.

Output: `distortion_displacement`, nodal vector `[ux, uy, uz]`, **metres**,
101 steps. `baseplate_clamped: true` holds the root layer at exactly zero
throughout, i.e. the blade is still on the plate.

### 3. `overhang` — `solver.am.overhang`

Definitions, verbatim from the contract: for an outward unit face normal **n**
and unit build direction **b**, the face is downward iff **n·b** < 0; its tilt
from the build plate is `acos(|n·b|)` in degrees; `support_needed = 1` iff any
downward boundary face of the cell tilts below `overhang_threshold_deg`. A
horizontal down-facing facet is 0° (fully unsupported), a vertical wall is 90°
(self-supporting), and the comparison is strict, so a facet exactly on the
threshold is not flagged.

Output: `overhang`, **per cell**, vector, 1 step:

| # | name | unit | meaning |
| --- | --- | --- | --- |
| 0 | `min_downskin_tilt_deg` | ° | 90 = this cell has no downward boundary face |
| 1 | `support_needed` | 0/1 | breaches the 45° threshold |
| 2 | `downskin_area_m2` | m² | downward-facing area on this cell |

**The result is that root-down needs almost no support, and that is the point.**
Applying the facet test above to the committed OBJ, in the `[0, 0, 1]`
orientation:

| surface group | Tri3 cells | downskin facets | flagged (<45°) | min tilt |
| --- | --- | --- | --- | --- |
| `blade_back` (suction) | 640 | 70 | 0 | 64.9° |
| `blade_face` (pressure) | 640 | 514 | 0 | 65.5° |
| `blade_root` (cap) | 30 | 30 | **18** | 9.1° |
| `blade_tip` (cap) | 30 | 0 | 0 | — |

Not one blade-surface facet falls below 45°; the shallowest is 64.9°. The only
flagged cells are on the root cap — and that cap *is* the baseplate interface,
which `solver.am.overhang` has no way to recognise as supported, exactly as in
[`examples/am-lpbf-bracket`](../am-lpbf-bracket). So the honest answer for this
part in this orientation is **no supports needed**.

That is not luck. A propeller section lies on a cylinder of radius *r*, so its
surfaces are nearly *parallel* to the radial direction — and the radial
direction is the build direction when you stand the blade on its root. Blade
surfaces become near-vertical walls, and near-vertical walls are
self-supporting. Printing a blade root-first is the right answer for a reason
that this capability can quantify.

Which makes the obvious experiment worth running. Change `build_direction` on
this stage and re-run:

| `build_direction` | downskin facets | flagged (<45°) | min tilt on blade surfaces |
| --- | --- | --- | --- |
| `[0, 0, 1]` root down | 614 | **18** (root cap only) | 64.9° |
| `[0, 0, -1]` tip down | 726 | 57 | 9.4° |
| `[0, 1, 0]` blade laid flat | 666 | 84 | 1.9° |
| `[1, 0, 0]` chordwise up | 683 | **601** | 4.9° |

Laid flat or built chordwise, most of the blade is unsupported. Root-down beats
the worst orientation by a factor of 33 in flagged facets. (These counts come
from an independent application of the documented facet test to the committed
OBJ; the plugin is the authority, and its boundary-face detection on a surface
mesh is what actually produces the field.)

45° is the conventional self-supporting limit for arc deposition. It is a
convention, not a physical constant: a well-tuned cell manages 30°, a badly
tuned one struggles at 50°. Change the threshold and re-run — that is the point
of having it as an input.

### 4. `printability` — `solver.am.printability`

Output: `printability`, **per cell**, vector, 1 step:

| # | name | unit | meaning |
| --- | --- | --- | --- |
| 0 | `printability_score` | 0..1 | 1 = trivially printable |
| 1 | `limiting_factor_code` | – | 0 none, 1 overhang, 2 thin wall, 3 build-volume fit, 4 aspect ratio / height, 5 corrosion allowance |
| 2 | `wall_thickness_proxy_m` | m | cell-geometry thickness estimate |

The score is `1 − (0.40·p_overhang + 0.30·p_thin + 0.15·p_aspect +
0.15·p_corr)`, clamped to [0, 1], behind a hard build-volume-fit gate, and
`limiting_factor_code` names the largest *weighted* term (0 when every penalty
is zero). Which means you have to know which terms are even live on this mesh:

| term | on this part | why |
| --- | --- | --- |
| build-volume gate | **passes** | the blade's 0.078 × 0.183 × 0.202 m envelope fits inside `machine_build_volume: [0.6, 0.6, 0.6]` — a mid-size robotic cell |
| `p_overhang` | **live, and ≈ 0** | nothing below 45° except the root cap (above) |
| `p_thin` | **skipped** | see below |
| `p_aspect` | **0** | slenderness = 0.202 / 0.078 = 2.6 against the 8:1 rule of thumb |
| `p_corr` | **skipped** | it depends on the same wall-thickness proxy as `p_thin` |

**`wall_thickness_proxy_m` reads 0 on a surface mesh, and the thin-wall and
corrosion-allowance penalties are skipped with it.** The proxy is
`2·V_cell / A_boundary(cell)`, which needs cell *volume* — a Tri3 surface mesh
carries no thickness information at all, so the plugin reports 0 and skips
those terms rather than inventing a thickness. So `min_wall_thickness: 0.004`
and `corrosion_allowance: 0.001` are declared in this pipeline **for the
record** and have no effect on the field. They are still worth stating: they
document the process intent, they are what the report prints, and they start
mattering the moment you feed a volume mesh through the same stage.

The consequence is that `printability_score` comes back at or near **1.0 with
`limiting_factor_code` 0 almost everywhere**, with the root-cap cells penalised
by the overhang term. That is the correct answer for this mesh and this
orientation, and it is *also* a warning: the score cannot see that the blade
tip is 2.4 mm thick against a 4–6 mm bead width, because the mesh does not
carry a thickness. A real WAAM blade is deposited oversize and machined back
precisely because of that tip, and no stage in this pipeline knows.

The score is a **documented weighted heuristic**. Read the weights in the
plugin source before you quote a number; nothing about it is a guarantee.

### 5. `corrosion` — `solver.marine.corrosion`

Output: `corrosion`, **per cell**, vector, 1 step:

| # | name | unit | meaning |
| --- | --- | --- | --- |
| 0 | `thickness_loss_mm` | mm | general loss over `service_life_years` |
| 1 | `pitting_risk` | 0..1 | localised-attack sub-score |
| 2 | `galvanic_risk` | 0..1 | couple sub-score from the galvanic series |

The modelling choices here are the interesting part:

* **`alloy: NAB`** — the composition inputs (`chromium_pct`, `molybdenum_pct`,
  `nitrogen_pct`) are left to the plugin's alloy table. PREN is meaningless for
  a bronze (no chromium), so read component 1 as **selective phase attack
  risk** — κ-phase dealloying — rather than as chloride pitting.
* **`mating_alloy: "316L"` with `area_ratio_cathode_anode: 0.15`** — NAB is
  less noble than passive stainless, so the blade is the *anode* against a
  stainless shaft. A small cathode against a large anode is the favourable
  configuration, hence 0.15. Reverse that ratio (a big stainless cathode, a
  small bronze fitting) and the same couple eats the bronze.
* **`cathodic_protection: true`** — the blade is bonded into the hull's
  protection system through a shaft earthing brush, which is standard practice
  precisely so the propeller is not left outside it.
* **`flow_velocity: 8.0`** is the representative *relative flow over the
  section*, not ship speed. NAB tolerates that; 90-10 copper-nickel in the same
  place would erode, which is the point of having a velocity input.
* **`as_built_surface: true`** — as-deposited WAAM roughness. Machine the blade
  and this should change.

For a NAB blade in clean flowing seawater with protection you should expect a
sub-millimetre general loss over 25 years. The rates in the plugin's table are
**indicative literature values, not design values**, and the plugin source is
the authority on which ones it uses.

### 6–7. `write-*` — `writer.vtu`

`propeller-distortion.vtu` (Warp By Vector, big scale factor) and
`propeller-overhang.vtu` (threshold on component 1 for a support map).

### 8. `report` — `writer.am.report`

The build report, rendered around the printability field, plus the mesh
summary, the process parameters, cost/time totals and a `Traceability` section
carrying an FNV-1a 64-bit content digest of the mesh — a digest, not a
signature.

Two key names read oddly for a wire-arc process and the pipeline comments say
so inline: `laser_power: 3000.0` is the contract's name for process
heat-source power, which for a WAAM cell is arc power; and
`process_layer_height` equals `layer_height` because a deposition bead **is**
the simulation layer. Unlike the LPBF example there is no layer coarsening
here, which makes this the more trustworthy of the two thermal-history
geometries.

The arithmetic behind the totals:

| quantity | value | how |
| --- | --- | --- |
| deposited mass | 2.01 kg | 264 cm³ × 7600 kg/m³ |
| deposition time | 0.67 h | 2.01 kg ÷ 3.0 kg/h |
| energy | ≈ 6.0 kWh | 0.67 h × (3 kW arc + 6 kW cell overhead) |
| cost | ≈ 117 | 0.67 h × 85 + 2.01 kg × 30 |

Add 30–50% to the mass and the time for the machining allowance that a real
blade is deposited with, which this pipeline does not model.

### 9. `dossier` — `writer.marine.qualification_report`

A Markdown dossier: part identification, criticality-class rationale, the
qualification-evidence checklist (feedstock traceability, machine
qualification, witness coupons, NDT method and coverage, mechanical test
matrix, build-direction property declaration, post-processing record), the
simulation evidence found in the supplied field, and a prominent advisory-only
disclaimer.

`criticality: 1` is the top of the writer's 1..3 scale: losing a blade on a
single-screw vessel is a loss-of-propulsion event, hence `redundancy: false`.
`class_framework: generic` — putting a class rule number in there changes the
wording and changes nothing about the disclaimer.

## What this whole pipeline ignores

* **The hydrodynamic loads.** There is no thrust, no torque, no blade bending,
  no cavitation and no unsteady wake load anywhere in this chain. Nothing here
  says the blade is strong enough; it says things about how it prints and how
  it corrodes. Structural adequacy needs a separate load case — see
  [`examples/cantilever-beam`](../cantilever-beam) for the elasticity path.
* **Cavitation erosion**, which is the failure mode that actually retires
  propellers, and which NAB is chosen for. `solver.marine.corrosion` has a
  flow-velocity input but no cavitation model.
* **Heat treatment.** As-deposited NAB should be temper-annealed (~675 °C is
  standard practice for the cast alloy) to break up the κ-phase distribution.
  Neither the distortion nor the corrosion stage knows whether that happened.
* **Machining.** The as-deposited blade is oversize. Every geometric result
  here is against the final surface.
* **The other three blades and the hub.** One blade, no assembly, no
  interaction.

## References

* [`examples/materials/am-marine.toml`](../materials/am-marine.toml) — where the NAB numbers come from, including the ASTM B148 C95800 attribution and the κ-phase warning.
* [`examples/am-submarine-pressure-hull`](../am-submarine-pressure-hull) — the other marine example: hydrostatic load and collapse margin.
* [`examples/am-lpbf-bracket`](../am-lpbf-bracket) — the same distortion capability with exact layer tags from `mesher.am.layered`.
* `docs/MARINE.md` — the marine capability reference, including the galvanic series and its source.
* `docs/MANUFACTURING.md` — the AM capability reference.
