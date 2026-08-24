# am-lpbf-bracket — the metal-AM process chain, end to end

The flagship example for the manufacturing block (ADR-0044). Eleven stages,
eight capabilities, one 316L blank on a laser powder-bed machine:

```
mesher.am.layered                              layer-aligned Hex8 build mesh
  ├─ solver.am.thermal.lpbf                    layer-wise thermal history
  │    └─ postproc.am.melt_pool                melt-pool depth + porosity risk
  ├─ solver.am.distortion.inherent_strain      part-scale warp as it builds
  │    └─ postproc.am.residual_stress          von-Mises-equivalent stress
  └─ solver.am.overhang                        downskin tilt + support need
        ├─ writer.vtu × 3                      ParaView views
        └─ writer.am.report × 2                Markdown build reports
```

## What this models

A **60 × 20 × 20 mm 316L blank** — 24 cm³, 192 g — built in +Z off a
baseplate preheated to 80 °C, on a 400 W machine running a stock 316L
recipe: 195 W, 0.8 m/s, 0.11 mm hatch, 30 µm layer. That is 73.9 J/mm³ of
volumetric energy density, which is squarely in the dense-part window for
this alloy.

The mesh is 30 × 10 × 10 = **3000 Hex8 cells on 3751 nodes in 10 simulation
layers**, and every cell carries its layer index as its cell tag — which is
the point of `mesher.am.layered`. Downstream capabilities read that tag
directly instead of re-binning coordinates, so the layer structure is exact
rather than inferred.

Then, in order: how hot each layer gets and how it cools; whether the melt
pool is deep enough to fuse to the layer below without keyholing; how much the
part warps as it grows; what residual stress that warp implies; and which
cells sit over air.

## What this is *not*

**It is not a bracket.** The geometry is a rectangular blank. `mesher.am.layered`
is a *mesher*, and the C plugin ABI gives meshers no value bag — the entry
point receives only `target_size`, `optimize`, `element_order` and
`random_seed`, so there is no way to describe a shape to it from YAML. With no
upstream `geometry:` handle it produces its documented default build box. This
example is therefore a **process** study on a bracket-sized blank, not a shape
study. See "Putting your own geometry in" below.

**The overhang result on this geometry is trivially boring.** A voxelised box
has axis-aligned faces only. The layer-0 cells report a 0° downskin — that is
the baseplate interface, which `solver.am.overhang` has no way to recognise as
supported, so it flags `support_needed = 1` there. Every other cell reports the
"no downward boundary face" sentinel of 90° and `support_needed = 0`. Both
answers are correct and neither is useful. For a real overhang result see
[`examples/am-marine-propeller`](../am-marine-propeller), which runs the same
capability against a skewed, twisted blade.

**Nothing here solves a PDE.** Every model in the chain is closed-form or
heuristic: a Rosenthal moving point source for the thermal field, a
normalised-enthalpy scaling for the melt pool, a Stoney-type curvature
accumulation for the distortion. That is a deliberate architectural choice —
these capabilities exist to make process decisions cheap and fast, and to be
honest about it. `docs/MANUFACTURING.md` lists what each one ignores.

## Running it

```sh
# from a configured build: cmake --preset dev && cmake --build --preset dev
SOUXMAR=./build/dev/src/cli/souxmar
PLUGINS=./build/dev/examples/plugins

$SOUXMAR run examples/am-lpbf-bracket/pipeline.yaml --plugin-path $PLUGINS
```

One `--plugin-path` is enough: plugin discovery scans one level of
subdirectories under each search path, and every in-tree plugin builds into
`build/dev/examples/plugins/<plugin-dir>/` alongside a copy of its manifest.

This example needs no input file, so it runs from anywhere — and since reader
`path:` values now resolve against the directory the pipeline file lives in,
so do the other three AM examples. Output paths are unchanged: they still
resolve against your **working directory**, which is why the files below land
in `$PWD` rather than in the source tree.

Expected shape of the output:

```
Running pipeline pipeline.yaml (11 stages, N capabilities available)
  [OK      ] build             hash=...
  [OK      ] thermal           hash=...
  [OK      ] meltpool          hash=...
  [OK      ] distortion        hash=...
  [OK      ] residual          hash=...
  [OK      ] overhang          hash=...
  [OK      ] write-temperature hash=...
  [OK      ] write-melt-pool   hash=...
  [OK      ] write-distortion  hash=...
  [OK      ] report            hash=...
  [OK      ] support-report    hash=...

pipeline ok (11 stages)
```

Five files land in `$PWD`: `bracket-temperature.vtu`,
`bracket-melt-pool.vtu`, `bracket-distortion.vtu`,
`bracket-build-report.md`, `bracket-support-report.md`.

A second `souxmar run` with no changes marks every stage `[CACHED]` — the
cache is keyed on inputs, plugin version and transitive upstream hashes.
`--no-cache` forces re-execution.

## Stage by stage

### 1. `build` — `mesher.am.layered`

`target_size: 0.002` does double duty: it is the voxel edge **and** the
simulation layer thickness. Over the default 60 × 20 × 20 mm box that gives
30 × 10 × 10 cells in 10 layers, well under the mesher's 200 000-cell cap.
`element_order` must be 1; `optimize` is accepted and ignored (documented in
the plugin header). Cell tag = layer index, 0 at the bottom. Boundary faces
carry the block's face-tag convention: 14 for the −Z baseplate interface, 15
for the +Z top, 10–13 for the sides.

### 2. `thermal` — `solver.am.thermal.lpbf`

Output: `temperature`, nodal scalar, **°C**, 10 time steps. Step *j* is the
state just after layer *j* is scanned — nodes in layer *j* at their Rosenthal
peak, nodes below decaying exponentially toward the baseplate temperature over
`interlayer_time`, nodes in layers not yet built sitting at
`preheat_temperature`. Scrub the time slider in ParaView to watch the build.

Note the **two** layer heights:

| key | value | meaning |
| --- | --- | --- |
| `layer_height` | 0.002 m | the *simulation* layer — matches the mesh |
| `process_layer_height` | 3.0e-5 m | the *machine* layer — sets energy density |

One simulation layer therefore lumps about 67 machine layers. That coarsening
is the central approximation of this example; see below.

### 3. `meltpool` — `postproc.am.melt_pool`

Output: `melt_pool`, **per cell**, vector, 1 step, components

| # | name | unit | meaning |
| --- | --- | --- | --- |
| 0 | `melt_pool_depth_m` | m | melt-pool depth from the Rosenthal isotherm |
| 1 | `normalised_enthalpy` | – | ΔH/h*, the keyholing scaling group |
| 2 | `porosity_risk` | 0..1 | combined lack-of-fusion + keyhole sub-scores |

Lack of fusion is flagged when the melt pool is shallower than the layer it has
to weld to; keyholing when normalised enthalpy exceeds the threshold documented
in the plugin source (the scaling follows the King / Hann normalised-enthalpy
work). Colour by component 2 to find cells at risk.

`postproc.*` stages require **both** `mesh:` and `field:` — the dispatcher
rejects a postproc with a missing `field:` rather than passing NULL, which is
why mesh-only analyses like `solver.am.overhang` register as solvers.

### 4. `distortion` — `solver.am.distortion.inherent_strain`

Output: `distortion_displacement`, nodal vector `[ux, uy, uz]`, **metres**, 10
steps — step *j* is the distortion after depositing layer *j*, so the field
animates the build. With `baseplate_clamped: true` the baseplate-layer nodes
stay at exactly zero at every step: this is the still-bolted-down part, not the
sprung-back one after cut-off.

`inherent_strain: 0.0` asks the solver to derive it as
`−cte · (T_melt − T_preheat) · strain_calibration`, which for these inputs is
`−1.6e-5 × 1320 × 0.30 = −6.3e-3`, i.e. −0.63%.

**`strain_calibration: 0.30` is not calibrated.** The inherent-strain method is
a scaling law whose one free parameter has to be fitted against a *measured*
part on your machine with your parameter set. Until it is, read the distortion
magnitudes as relative, not absolute. This is the largest single uncertainty in
the pipeline and it is a factor, not a percentage.

### 5. `residual` — `postproc.am.residual_stress`

Output: `residual_stress`, **per cell**, vector, 1 step:

| # | name | unit | meaning |
| --- | --- | --- | --- |
| 0 | `sigma_vm_Pa` | Pa | von-Mises-equivalent residual stress |
| 1 | `sigma_vm_over_yield` | – | `[0] / yield_strength`, so bounded by 1; 1.0 means "at yield, plastically relieved" |
| 2 | `layer_index` | – | which simulation layer the cell is in |

`yield_strength: 5.0e8` is as-built LPBF 316L measured **in-layer (XY)**. The
Z-direction value is about 5% lower — see
[`examples/materials/am-marine.toml`](../materials/am-marine.toml) — and this
stage does not apply that knockdown, so component 1 is optimistic for a
Z-loaded feature by roughly that much.

### 6. `overhang` — `solver.am.overhang`

Output: `overhang`, **per cell**, vector, 1 step:

| # | name | unit | meaning |
| --- | --- | --- | --- |
| 0 | `min_downskin_tilt_deg` | ° | 90 when the cell has no downward boundary face |
| 1 | `support_needed` | 0/1 | 1 iff any downward face tilts below the threshold |
| 2 | `downskin_area_m2` | m² | downward-facing boundary area on this cell |

Definitions, verbatim from the capability contract: for an outward unit face
normal **n** and unit build direction **b**, the face is downward iff
**n·b** < 0; its tilt from the build plate is `acos(|n·b|)` in degrees;
`support_needed = 1` iff any downward boundary face tilts less than
`overhang_threshold_deg`. The solver finds boundary faces itself — a face is on
the boundary when no other cell shares its node set.

### 7–9. `write-*` — `writer.vtu`

Three ParaView files. The VTU writer is field-agnostic and takes one field, so
it is one stage per field you want to look at.

* `bracket-temperature.vtu` — nodal °C, 10 steps.
* `bracket-melt-pool.vtu` — the per-cell melt-pool triple.
* `bracket-distortion.vtu` — nodal displacement vectors in metres, 10 steps.
  Use Warp By Vector with a large scale factor; the raw magnitudes are tens of
  microns at this calibration.

### 10–11. `report` / `support-report` — `writer.am.report`

`writer.am.report` renders exactly **one** field: it dispatches on the field's
name and emits the matching section. Getting both a residual-stress summary and
a support summary therefore takes two stages with two paths. Both always
render the mesh summary (node and cell counts, element histogram, bounding
box), the process parameters, cost and time totals re-derived from the rate
keys, and a `Traceability` section carrying an FNV-1a 64-bit digest of the node
coordinates and connectivity. **That digest is a non-cryptographic content
digest, not a signature** — it tells you two reports came from the same mesh,
and nothing about who made it.

No wall-clock, no absolute paths and no environment values appear in the
output; determinism is a gate in this repo.

The arithmetic the cost section is re-deriving, so you can check it:

| quantity | value | how |
| --- | --- | --- |
| machine layers | 667 | 20 mm ÷ 30 µm |
| scan length per machine layer | 3.64 m | (20 × 20 mm) ÷ 0.11 mm hatch |
| time per machine layer | 12.6 s | 3.64 m ÷ 0.8 m/s + 8 s recoat |
| total build time | ≈ 2.3 h | 667 × 12.6 s |
| energy | ≈ 6 kWh | 195 W while scanning + 2.5 kW machine overhead |
| mass | 192 g | 24 cm³ × 7990 kg/m³ |
| cost | ≈ 120 | 2.3 h × 45 + 0.192 kg × 75 |

Currency is whatever you put in `material_cost_per_kg` and
`machine_rate_per_hour`; souxmar does not know or care which.

## What the model approximates — be clear about this

**The 67:1 layer coarsening.** The simulation layer is 2 mm; the machine layer
is 30 µm. The thermal history you get is the history of a 2 mm-thick lump, not
of 67 individual 30 µm passes. The real part sees 67 rapid heat-and-cool
cycles per lump, and each cycle is what actually accumulates plastic strain.
The coarse model captures the *layer-scale* thermal gradient and misses the
*pass-scale* one entirely.

You cannot fix this by refining, either: `target_size: 0.0005` gives
192 000 cells — right at the mesher's 200 000-cell cap — for a still-40:1
coarsening, and a nodal scalar field of 8.1 M values (~65 MB) per solve. Going
to the machine layer height would need roughly 48 million cells. Layer-scale
coarsening is not a shortcut in this example; it is what part-scale AM
simulation *is*, in every commercial code as well as this one.

**`interlayer_time: 12.5 s` is the machine's per-layer cycle, not the
simulation bin's.** Honestly scaled to the 2 mm bin it would be about 840 s,
at which point everything below the top layer has cooled to the baseplate and
the field carries no gradient at all. Using the machine number (which is also
what the contract's own defaults do: 1 mm layer, 10 s) keeps a visible
sub-surface gradient. Read that gradient as "where the gradient is steep", not
as a temperature you could measure with a thermocouple.

**The other approximations, briefly.**

* Rosenthal assumes a semi-infinite medium, constant properties, no latent
  heat, no melt-pool convection and a point source. All four are wrong in
  detail; the combination is standard practice for scoping.
* The melt-pool porosity score is a *risk indicator* built from two
  dimensionless groups, not a predicted porosity fraction. It will not tell
  you about spatter, denudation, or gas entrapped in the powder.
* Distortion accumulation is a Stoney-type curvature model on a growing
  substrate — a 1-D beam-bending argument applied cell-wise. It has no
  contact, no plasticity, no support-structure stiffness and no cut-off
  spring-back.
* Residual stress is read back from the distortion field elastically, then
  capped at yield (an ideal-plastic radial return) — there is no real yield
  surface in the solve, no hardening and no redistribution. With the 316L
  defaults the locked-in strain (6.3e-3) is about three times the alloy's
  elastic capacity (1.9e-3), so most of the part reports exactly yield. Read
  that as "this recipe locks in more strain than 316L can carry elastically",
  not as a measured stress map.
* No powder-bed thermal properties, no shielding-gas flow, no recoater
  interaction, no support structures anywhere in the chain.

## Putting your own geometry in

`mesher.am.layered` voxelises the **bounding box** of an upstream `geometry:`
handle when one is supplied. Only a true CAD geometry counts — in-tree that
means `reader.step` (the OpenCASCADE-backed `occt-reader`, opt-in via
`-DSOUXMAR_WITH_OPENCASCADE=ON`). `reader.obj` and `reader.stl` return a
*mesh*, not a geometry, so they cannot feed a mesher's `geometry:` input; the
dispatcher will tell you so.

```yaml
  - id: cad
    plugin: reader.step
    input:
      path: bracket.step

  - id: build
    plugin: mesher.am.layered
    input:
      geometry: { from: cad }
      target_size: 0.002
      element_order: 1
```

Everything downstream is unchanged. Be aware that you get the bounding box's
voxelisation, not the part's — this mesher does not do inside/outside
classification. If you want the process capabilities to run on the actual
shape, read a surface mesh with `reader.obj` and skip the mesher, as the other
three AM examples do; you lose the exact layer tags and fall back to centroid
binning, which is the documented alternative.

## References

* [`examples/materials/am-marine.toml`](../materials/am-marine.toml) — where the 316L numbers come from.
* [`examples/am-marine-propeller`](../am-marine-propeller) — the same distortion and overhang capabilities on real geometry.
* [`examples/am-polymer-auv-fairing`](../am-polymer-auv-fairing) — the polymer counterpart of this chain.
* `docs/MANUFACTURING.md` — the capability reference, including every formula and every omission.
* `docs/adr/0044-manufacturing-capability-namespaces.md` — why all eighteen capabilities fit under the existing five plugin kinds with no ABI change.
