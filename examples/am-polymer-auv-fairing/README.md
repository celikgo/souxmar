# am-polymer-auv-fairing — the question every FFF part has to answer

Nine stages, six manufacturing capabilities, and one question: **is the weld
between layers good enough?**

```
reader.obj  fairing.obj
  ├─ solver.am.polymer.fff              interlayer thermal cycling
  │    └─ postproc.am.bond_strength     degree of healing / Z-strength
  ├─ solver.am.printability             composite DfAM score
  └─ solver.am.buildtime                per-layer time, energy, mass, cost
        ├─ writer.am.gcode              printable FFF G-code
        ├─ writer.vtu × 2
        └─ writer.am.report             build report
```

An FFF part is strong in-layer and weak between layers, and how weak depends
entirely on how hot the interface still is when the next road lands on it. That
is a thermal problem with a polymer-physics answer, and this pipeline walks the
whole way from process parameters to a Z-strength number — and then tells you
why not to trust the number.

## Status of these numbers — read this first

**The outputs of this pipeline are preliminary engineering estimates.** The
reptation reference pair in the `bond` stage is a **placeholder**, not a PEKK
rheology measurement, and the arithmetic below shows exactly how far the
resulting Z-strength estimate sits from measured FFF PEKK behaviour. This is a
subsea part; **souxmar is not a classification society and nothing here is
approval of any kind.**

## What this models

The **nose fairing of a 0.2 m diameter AUV**, printed nose-up in PEKK on a
heated-chamber machine with a 0.6 mm nozzle at a 0.4 mm layer.

| | |
| --- | --- |
| outer form | semi-ellipse, semi-axes 0.300 m axial × 0.100 m radial |
| length | 0.294 m (the nose is truncated) |
| nose window | 40 mm diameter flat annulus — a forward-looking sonar or altimeter aperture |
| wall | 3 mm, as a true normal offset of the outer meridian |
| laminate volume / mass | 446 cm³ / **0.580 kg** in PEKK (0.473 kg in PA12-CF) |
| mesh | 936 nodes, 936 quads → **1872 Tri3 cells**, watertight, oriented |
| print | **735 layers** at 0.4 mm, base flange on the bed |

The axis of revolution is +Z, base at z = 0, nose at z = 0.294. That is the
print orientation, and it is why the wall's tilt from the build plate degrades
steadily as you climb:

| z band (mm) | outer radius (mm) | wall tilt from plate |
| --- | --- | --- |
| 0 – 34 | 100.0 → 99.3 | 88.9° |
| 68 – 101 | 97.4 → 94.2 | 84.4° |
| 132 – 162 | 89.7 → 84.1 | 79.3° |
| 190 – 215 | 77.4 → 69.7 | 73.0° |
| 237 – 257 | 61.1 → 51.7 | 64.0° |
| 257 – 273 | 51.7 → 41.6 | 57.7° |
| 273 – 285 | 41.6 → 30.9 | 49.5° |
| 285 – 294 | 30.9 → 19.9 | **38.3°** |

Only the final band breaches the 45° threshold. That progression — fine for
90% of the height, then abruptly not — is the characteristic shape of a nose
cone printed base-down.

**And it is the *inner* skin that gets flagged, not the outer one.** This is
worth being precise about, because the intuition goes the wrong way. A facet is
a downskin only when its outward normal has a negative component along the
build direction. On a converging cone printed base-down:

* the **outer** skin's normals tip *outward and forward*, so **n·b > 0** — not
  downskins at all. Each new layer's outer edge sits inside the previous one,
  which is the self-supporting direction. Zero of the 864 outer facets are
  downskins.
* the **inner** skin's normals point *inward and backward*, so **n·b < 0** —
  every one of its 864 facets is a downskin, at the same tilt magnitudes as the
  table above. The 72 in the top band, at 38.4°, breach the threshold. That is
  physically right: the inside of a converging wall is a genuine unsupported
  overhang, and it is the classic place an FFF cone sags.
* the **base flange**, 72 facets with normal −Z, is a 0° downskin and is
  flagged too. That is the bed contact, which `solver.am.overhang` has no way
  to recognise as supported — the same quirk as layer 0 in
  [`examples/am-lpbf-bracket`](../am-lpbf-bracket).

144 of 1872 cells flagged, then: 72 real ones inside the nose and 72 spurious
ones on the bed.

## The geometry

`fairing.obj` is produced by [`generate-geometry.py`](generate-geometry.py) —
Python 3 standard library only, no numpy, deterministic, reproduces the
committed file byte-for-byte:

```sh
python3 generate-geometry.py            # write fairing.obj
python3 generate-geometry.py --check     # verify only
```

Two details worth knowing:

* The inner surface is a **true normal offset** of the outer meridian, not a
  radial offset, so the wall is 3 mm everywhere including through the nose
  curvature. The offset uses the analytic ellipse tangent rather than a finite
  difference, which keeps the base annulus exactly planar at z = 0 — that face
  is the bed contact, and a 60 µm slope on it would be a real printing problem.
* The ellipse's radius of curvature at the nose vertex is b²/a = 33 mm,
  comfortably larger than the 3 mm offset, so the offset does not
  self-intersect. Truncating at z = 0.294 keeps it well clear.

The script verifies before writing: all face indices in range, no degenerate
triangles, **oriented 2-manifold** with zero open edges, and **positive
enclosed volume** by the divergence theorem. It triangulates the way
`obj-reader` does, so its counts are the reader's counts.

The OBJ names its surfaces (`skin_outer`, `skin_inner`, `base_flange`,
`nose_window`) with `g` groups rather than `usemtl`, because `obj-reader` maps
`usemtl` names to per-cell tags and every AM capability reads a non-negative
cell tag as a **layer index** — a tagged fairing would be simulated as a
four-layer part instead of the 735 layers this example is about. The OBJ header
says so.

**What the geometry is not:** a hydrodynamic form. A real AUV nose is a Myring
or semi-ellipsoidal profile fitted to a drag target, and it carries mounting
bosses, a window seat, penetrator holes and an internal frame. This is the
outer mould line plus a constant wall.

## Running it

```sh
cd examples/am-polymer-auv-fairing
souxmar run pipeline.yaml --plugin-path ../../build/dev/examples/plugins
```

A reader's `path:` resolves against the YAML's own directory, so `fairing.obj`
is found beside the pipeline and this runs from anywhere. Output paths still
resolve against your **working directory** — hence `$PWD` below. One
`--plugin-path` is enough; discovery scans one level of subdirectories under
it.

Four files land in `$PWD`:

| file | what |
| --- | --- |
| `fairing.gcode` | 735-layer FFF G-code. **Read it before running it.** |
| `fairing-printability.vtu` | per-cell printability triple |
| `fairing-buildtime.vtu` | per-cell time / cumulative-time / area triple |
| `fairing-build-report.md` | build report around the bond-quality field |

## Stage by stage

### 1. `fairing` — `reader.obj`

936 quads fan-triangulate into 1872 Tri3 cells on 936 nodes.

### 2. `fff` — `solver.am.polymer.fff`

Output: `interface_temperature`, nodal scalar, **°C**, one step per print layer
— 735 of them.

PEKK on a heated-chamber machine: 380 °C nozzle, 140 °C bed, **120 °C
chamber**, Tg 162 °C. The chamber temperature is the whole reason a machine
like this exists: it is what keeps the interface above Tg long enough to weld,
and it is why PEKK printed in an open-air machine delaminates.

The model is lumped-capacitance cooling,
`T(t) = T_env + (T_dep − T_env)·exp(−t/τ)` with `τ = ρ·c·V/(h·A)` and the
characteristic length taken from `layer_height` / `road_width`; the interface
temperature when the next road lands is the mean of the fresh road and the
substrate.

**`layer_time: 45.0` is a constant, and the real one is not.** 45 s is about
1.3 m of perimeter plus infill at 30 mm/s near the base. As the cross-section
shrinks towards the nose the real layer time falls, which means less dwell,
which means a *hotter* interface — so this constant **under**-estimates bond
quality at the tip. Stage 5 computes the real per-layer time; compare the two.

### 3. `bond` — `postproc.am.bond_strength`

Output: `bond_quality`, **per cell**, vector, 1 step:

| # | name | unit | meaning |
| --- | --- | --- | --- |
| 0 | `degree_of_healing` | 0..1 | reptation healing `D_h = min(1, (t_weld/t_rep)^¼)` |
| 1 | `z_strength_fraction` | 0..1 | interlayer strength as a fraction of in-layer |
| 2 | `seconds_above_tg` | s | how long this cell's interface stayed weldable |

The ¼ exponent is Wool & O'Connor's polymer-welding law as applied to FDM by
Yang & Pitchumani (2002); `t_rep` is Arrhenius-shifted in the interface
temperature.

### The hand check

Worth doing, because it shows what the answer hangs on. Arithmetic for these
inputs, using the plugin's own definitions (its source is the authority — this
is a sanity check over one cooling window, not a reimplementation of the
non-isothermal integral it evaluates over the whole 735-step history):

```
characteristic length  L_c = h·w/(w + 2h) = (0.4 × 0.6)/(0.6 + 0.8) mm  = 171 µm
Biot number            Bi  = h_conv·L_c/k = 20 × 1.71e-4 / 0.25         = 0.014
                             ≪ 0.1, so the lumped-capacitance assumption holds
time constant          τ   = ρ·c·L_c/h_conv = 1300 × 1900 × 1.71e-4 / 20 = 21.2 s
substrate at 45 s      120 + (380 − 120)·e^(−45/21.2)                    = 151 °C
interface temperature  (380 + 151)/2                                     = 266 °C
seconds above Tg       τ·ln((266 − 120)/(162 − 120))                     = 26 s
t_rep at 266 °C        2.0 s × exp[(80 kJ/mol / R)(1/539 K − 1/613 K)]   = 17.5 s
t_rep at Tg = 162 °C   the same, at 435 K                                = 1227 s
reduced time           ξ = ∫ dt / t_rep(T(t)) over the T > Tg window      = 0.28
degree of healing      D_h = min(1, ξ^¼)                                 = 0.73
z-strength fraction    0.85 · D_h                                        = 0.62
```

Two things fall out of that.

**The reduced-time integral is dominated by its first few seconds.** `t_rep`
goes from 17.5 s at the interface temperature to 1227 s at Tg — a factor of 70
over a 100 °C drop. Welding happens in the first moments after the road lands
and then effectively stops, long before the interface actually cools through Tg.
Which means `seconds_above_tg` (component 2) is *not* a proxy for bond quality:
26 s above Tg bought 0.28 of reduced time, and most of that came from the first
five.

**The result lands close to measurement, and partly by luck.** 0.62 against the
0.70 measured Z-strength fraction for FFF PEKK in
[`examples/materials/am-marine.toml`](../materials/am-marine.toml) is better
agreement than this chain deserves, because two of its inputs are placeholders
(below) and the 0.85 factor is itself a heuristic. Note also that
`z_strength_fraction` is **capped at 0.85** by construction — the plugin
multiplies the degree of healing by a documented interlayer-contact fraction, on
the grounds that a reptation law describes interdiffusion across an interface
that is *in contact*, and says nothing about the voids left by laying round-ish
beads next to each other. So this pipeline can never report a fully strong
interface, which is correct, and the exact ceiling is a judgement call.

Read `degree_of_healing` as "did the interface stay weldable long enough" — a
genuine and useful process question, and the one the chamber temperature
answers. Do not put `z_strength_fraction` into a stress calculation. Use a
measured one.

### And be honest about the reference pair

`reptation_time_reference: 2.0` at `reptation_reference_temperature: 340.0` is a
**placeholder**. It is not a PEKK rheology measurement. It is a pair chosen so
the Arrhenius shift lands in a plausible range for a 380 °C nozzle, and the
contract's own default (2.0 s at 260 °C) is *worse* for PEKK because PEKK is not
even molten at 260 °C. `activation_energy: 8.0e4` is the right order for a
PAEK-family melt and no more than that.

Fit both against short-beam or tensile-Z coupons before quoting a number to
anyone. The healing exponent is literature; the reference pair is yours to
measure.

### 4. `printability` — `solver.am.printability`

Output: `printability`, **per cell**, vector, 1 step:

| # | name | unit | meaning |
| --- | --- | --- | --- |
| 0 | `printability_score` | 0..1 | 1 = trivially printable |
| 1 | `limiting_factor_code` | – | 0 none, 1 overhang, 2 thin wall, 3 build-volume fit, 4 aspect ratio / height, 5 corrosion allowance |
| 2 | `wall_thickness_proxy_m` | m | cell-geometry thickness estimate |

`min_wall_thickness: 0.0012` is two 0.6 mm roads — the thinnest wall this
nozzle can actually build. The fairing's 3 mm wall clears that by 2.5×, so the
score should be driven by the overhang term instead, and only in the last z
band (see the tilt table above). `machine_build_volume: [0.3, 0.3, 0.4]` is a
mid-size heated-chamber machine and the part fits with room to spare.

`corrosion_allowance: 0.0` because a thermoplastic in seawater does not
corrode. It creeps and it absorbs water, and this capability models neither.

The score is a **documented weighted heuristic**, not a manufacturability
guarantee. Read the weights in the plugin source.

### 5. `buildtime` — `solver.am.buildtime`

Output: `buildtime`, **per cell**, vector, 1 step: `[0] layer_time_s` for this
cell's layer, `[1] cumulative_time_s`, `[2] layer_area_m2`. `process: fff`
selects the extrusion branch, where layer time comes from
layer area ÷ (`road_width` × `print_speed`) rather than from a hatch and a scan
speed. `process_layer_height` equals `layer_height` because in FFF the machine
layer **is** the simulation layer — no coarsening, unlike
[`examples/am-lpbf-bracket`](../am-lpbf-bracket), where one simulation layer
lumps 67 machine layers.

**Surface-mesh caveat.** For a surface mesh, per-layer area is the
`|n·b|`-weighted *projected* triangle area, not a solid cross-section. For a
3 mm-walled shell that projected footprint is small, so read the totals as a
**lower bound**. The volume-based estimate, which is the one you can check by
hand:

| quantity | value | how |
| --- | --- | --- |
| extrusion cross-section | 0.24 mm² | 0.6 mm road × 0.4 mm layer |
| deposited path length | 1859 m | 446 cm³ ÷ 0.24 mm² |
| print time | **17.2 h** | 1859 m ÷ 30 mm/s |
| filament consumed | 186 m | 446 cm³ ÷ (π/4 × 1.75 mm²) |
| energy | ≈ 22 kWh | 17.2 h × (60 W hot end + 1.2 kW chamber/bed/motion) |
| cost | ≈ 836 | 17.2 h × 25 + 0.58 kg × 700 |

`material_cost_per_kg: 700.0` is an indicative PEKK filament price and it is
the number most likely to be wrong for you by a factor of two. Get a quote.
`laser_power: 60.0` is the contract's name for process heat-source power; an FFF
hot end draws 40–60 W, and the chamber, bed and motion system are in
`machine_power_overhead`.

### 6. `gcode` — `writer.am.gcode`

**READ THE G-CODE BEFORE YOU RUN IT.** This is a demonstration slicer, not
PrusaSlicer. What it does: a `;` header listing every parameter, then
`M104`/`M140`/`G28`/`G90`/`M82`, then per layer a `G1 Z…` plus one perimeter
pass per closed contour and alternating ±45° scanline infill, with `E` computed
from extruded volume ÷ filament cross-section, retraction on travel, and a
trailer that turns the heaters off.

What it does **not** do: multiple perimeters, nozzle-width offset
compensation, support generation, bridging logic, cooling-fan control, seam
placement, start/end scripts for your machine, or any collision check. Feed it
to a machine unsupervised and you will crash the nozzle. Its value is that it
is a *real* slicer output you can inspect, diff and reason about — determinism
is a gate here, so contours are sorted by (min y, min x) of their lowest point,
scanlines run in increasing coordinate order, and quads split into triangles by
a fixed rule.

Two geometry consequences on this part: the fairing is 36-sided
circumferentially, so every sliced contour is a visibly faceted 36-gon; and
because the shell has both an outer and an inner skin, each layer produces two
concentric contours whose enclosed region is the wall.

0.294 m at a 0.4 mm layer is 735 layers, well inside `max_layers: 5000`.

### 7–8. `write-*` — `writer.vtu`

`fairing-printability.vtu` (colour by component 1 with a categorical map) and
`fairing-buildtime.vtu` (colour by component 1 to see where the hours go).

### 9. `report` — `writer.am.report`

Rendered around the bond-quality field, because degree of healing is the number
that decides whether this part is worth printing at all. Plus the mesh summary,
process parameters, cost/time totals and a `Traceability` section carrying an
FNV-1a 64-bit content digest of the mesh — a digest, not a signature. No
wall-clock, no absolute paths, no environment values.

## Swapping the material

PA12-CF is the cheaper substitute for this part: about a sixth of the filament
cost, prints on a machine without a heated chamber, and gives up roughly a
third of the strength and most of the temperature capability. Its process
window and its numbers are in
[`examples/materials/am-marine.toml`](../materials/am-marine.toml).

If you swap it in, **change the bond-strength threshold too**. PA12 is
semicrystalline with a Tg around 50 °C, but interdiffusion needs the interface
above the *crystallisation* temperature, nearer 150 °C.
`postproc.am.bond_strength` takes its threshold as
`glass_transition_temperature`, so leaving it at PA12's real Tg of 50 °C makes
the model report healing that does not happen. This is the sharpest edge in the
polymer chain; the material file flags it in as many words.

## What this pipeline ignores

* **Creep.** A polymer under sustained load keeps deforming, and for a subsea
  part held at pressure for months that — not static strength — is the design
  driver. Nothing in this chain models it, and there is no creep-rupture data
  in the material file.
* **Water uptake.** PEKK takes up around 0.2%, which is why it is the right
  choice here; PA12-CF takes up around 1% and swells a few tenths of a percent
  with it. No stage knows about either.
* **External pressure.** This is a fairing, not a pressure housing, and there
  is no hydrostatic stage in this pipeline. If your fairing is a pressure
  boundary, see
  [`examples/am-submarine-pressure-hull`](../am-submarine-pressure-hull).
* **Crystallinity.** PEKK's stiffness and its solvent resistance depend on the
  cooling rate, so an as-printed part and an annealed part are different
  materials. The thermal solve produces the history that would tell you, and
  nothing consumes it for that purpose.
* **Warping and bed adhesion**, the two things most likely to actually ruin
  this print. The distortion capability
  (`solver.am.distortion.inherent_strain`) is metal-oriented and is not in this
  chain.

## References

* [`examples/materials/am-marine.toml`](../materials/am-marine.toml) — PEKK, PA12-CF, PEEK, PVDF and HDPE, with process windows, Z-strength fractions and the semicrystalline-Tg warning.
* [`examples/am-lpbf-bracket`](../am-lpbf-bracket) — the metal counterpart of this chain, including the layer-coarsening discussion.
* [`examples/am-submarine-pressure-hull`](../am-submarine-pressure-hull) — `writer.am.cli`, the metal sibling of `writer.am.gcode`.
* `docs/MANUFACTURING.md` — the capability reference, including every formula and every omission.
