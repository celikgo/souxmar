---
name: simulating-additive-manufacturing
description: Use when setting up, running or interpreting an additive-manufacturing analysis in souxmar — metal powder-bed fusion (LPBF), wire-arc DED, or polymer extrusion (FFF/FDM). Covers build orientation, layer resolution, thermal history, melt-pool and porosity risk, inherent-strain distortion, residual stress, interlayer bond strength, printability checks, build time and cost, slicing to G-code or CLI, and what every one of those models is not. Triggers on "additive manufacturing", "AM", "3D print", "LPBF", "powder bed", "DED", "WAAM", "FFF", "FDM", "slicer", "G-code", "overhang", "support structures", "build orientation", "distortion", "residual stress", "printability", "build time".
---

# Simulating additive manufacturing

souxmar's manufacturing block is eight in-tree plugins providing eighteen
capabilities. Every one is a **closed-form or heuristic engineering model**
with a cited source in its header. They are screening and preliminary-sizing
aids: they will tell you that an orientation needs three times the support
area of another, or that a recipe sits in the lack-of-fusion corner of the
process window. They will not tell you a part's distortion to the micron.

Read [`docs/MANUFACTURING.md`](../../docs/MANUFACTURING.md) for the full
capability table with every input key, unit and default. This skill is the
operational walkthrough.

## When to use this skill

- Setting up an AM analysis from a STEP/STL/OBJ part or a build box.
- Choosing a build orientation, or arguing about one.
- Interpreting a `temperature`, `melt_pool`, `distortion_displacement`,
  `residual_stress`, `interface_temperature`, `bond_quality`, `overhang`,
  `printability` or `buildtime` field.
- Producing a build report, a G-code file or a CLI layer stack.
- Reviewing someone else's AM pipeline for the mistakes listed below.

## When NOT to use this skill

- Marine / subsea service assessment of an AM part — that is
  [`assessing-marine-am-parts`](../assessing-marine-am-parts/SKILL.md).
- Machining, casting or forming. Nothing here models subtractive or
  formative processes.
- Anything that needs a calibrated thermomechanical solve. Read
  [`docs/rfcs/0012-am-process-simulation.md`](../../docs/rfcs/0012-am-process-simulation.md)
  for that roadmap, and do not present these outputs as its substitute.

## The five rules that prevent most wrong answers

1. **A `postproc.*` stage needs `field: {from: ...}` as well as `mesh:`.**
   The dispatcher rejects it otherwise. This is why mesh-only analyses
   (`solver.am.overhang`, `solver.am.printability`, `solver.am.buildtime`)
   are `solver.*` capabilities — see
   [ADR-0044](../../docs/adr/0044-manufacturing-capability-namespaces.md).
2. **`layer_height` is the *simulation* layer, not the machine layer.**
   A 1 mm simulation layer lumps ~33 machine layers of 30 µm. Anything you
   pass per-layer must be lumped to match — most importantly
   `interlayer_time`. Passing the machine's 10 s recoat time for a 5 mm
   super-layer models depositing 5 mm of metal every 10 s, which heats the
   substrate to melting and pins the reported peak at the vaporisation cap.
3. **Layer index comes from the cell tag first.** `mesher.am.layered` writes
   cell tag = layer index; every downstream AM capability uses it and only
   falls back to binning the centroid along `build_direction` by
   `layer_height` when the mesh is untagged. A mesh from `reader.obj` is
   untagged, so its `layer_height` must be right.
4. **Meshers get no value bag.** `souxmar_mesher_mesh_fn` takes only
   `souxmar_mesher_options_t`, so `mesher.am.layered` is driven by
   `target_size` (the voxel edge *and* the layer thickness) plus the input
   geometry's bounding box. Fully parametric generation goes through
   `reader.*` — that is why the lattice generator is `reader.lattice` and
   reads a spec file.
5. **Temperatures are °C, everything else is strict SI** (m, kg, s, W, Pa, J).
   A `layer_height` of `0.2` is 200 mm, not 200 µm.

## Metal: the LPBF chain

```yaml
version: 1
stages:
  - id: build          # layer-aligned Hex8, cell tag = layer index
    plugin: mesher.am.layered
    input: { target_size: 0.002 }

  - id: thermal        # nodal °C, one time step per layer
    plugin: solver.am.thermal.lpbf
    input:
      mesh: { from: build }
      laser_power: 195.0          # W
      scan_speed: 0.8             # m/s
      hatch_spacing: 1.1e-4       # m
      layer_height: 0.002         # m  — must match the mesh
      process_layer_height: 3.0e-5
      interlayer_time: 660.0      # s  — lumped: 66 machine layers x 10 s

  - id: meltpool       # cell vector: depth / normalised enthalpy / porosity risk
    plugin: postproc.am.melt_pool
    input:
      mesh:  { from: build }
      field: { from: thermal }
      laser_power: 195.0
      layer_height: 0.002

  - id: distortion     # nodal vector, metres, one step per layer
    plugin: solver.am.distortion.inherent_strain
    input:
      mesh: { from: build }
      layer_height: 0.002
      baseplate_layers: 1
      baseplate_clamped: true
      strain_calibration: 0.30    # UNCALIBRATED — see below
```

Then `solver.am.overhang` for support need, `solver.am.buildtime` for time and
cost, and `writer.am.report` for a Markdown build sheet. A full worked example
is [`examples/am-lpbf-bracket`](../../examples/am-lpbf-bracket).

### Reading the outputs

| Field | Component | Read it as |
| --- | --- | --- |
| `temperature` | – | Peak at the freshly-scanned layer, exponential decay below, preheat above. Saturates at the documented 2861 °C vaporisation cap in the keyhole regime — that is the model being honest, not a bug. |
| `melt_pool` | `[0]` depth m | Tens of µm to ~1 mm. Below `process_layer_height` means lack of fusion. |
| | `[1]` ΔH/hs | Above ~30 is the published keyhole onset. |
| | `[2]` porosity risk | `max(lack-of-fusion, keyhole)` — the two failure modes sit at opposite ends of the window. |
| `distortion_displacement` | `[ux,uy,uz]` m | Tens of µm to a few mm on a normal part. Exactly zero on clamped baseplate nodes at every step. |
| `residual_stress` | `[0]` Pa | Capped at yield (ideal-plastic). With 316L defaults the locked-in strain is ~3× the elastic capacity, so most of the part reads exactly yield — read that as "this recipe locks in more than the alloy can carry elastically". |
| | `[1]` utilisation | `[0]/yield`, bounded by 1. |
| `overhang` | `[0]` tilt ° | `acos(\|n·b\|)` for downward boundary faces: 0° is a flat downskin, 90° a vertical wall. |
| | `[1]` support needed | 1 when any downward face is below `overhang_threshold_deg`. |

### Calibration

`strain_calibration` (default 0.30) is the fraction of free thermal strain that
ends up locked in. **It is a placeholder until you fit it.** Print a
cantilever or bridge coupon in your machine and alloy, measure the released
distortion, and scale `strain_calibration` until the model matches. Report the
calibration alongside any result you show someone else. The same applies to
`absorptivity` in the thermal model — fit it to a measured single-track
cross-section, not to a handbook value.

## Polymer: the FFF chain

`solver.am.polymer.fff` produces the interlayer temperature history;
`postproc.am.bond_strength` turns it into a degree of healing and a
Z-direction strength fraction via the reptation model. The ordering that
matters: a hot chamber and a short layer time give near-full healing, a cold
chamber and a long layer time give poor healing — and poor healing is where
extruded parts actually fail. `writer.am.gcode` slices and emits G-code;
`writer.am.cli` emits a Common Layer Interface layer stack for PBF machines.
Worked example: [`examples/am-polymer-auv-fairing`](../../examples/am-polymer-auv-fairing).

## Choosing a build orientation

Use the `set_build_orientation` agent tool, or run `solver.am.overhang` and
`solver.am.buildtime` for each candidate and compare:

- downskin area needing support (`overhang` component `[2]` where `[1]` is 1),
- build height along the candidate axis — it sets the recoat-time floor,
- for polymer, whether the load path runs across layer interfaces, where
  `bond_quality[1]` is your strength knockdown.

Orientation is usually a trade between support area and height. Say which you
optimised.

## Review checklist

- [ ] `layer_height` consistent between the mesher, the solvers and the report.
- [ ] `interlayer_time` lumped to match the simulation layer.
- [ ] Every `postproc.*` stage has both `mesh:` and `field:`.
- [ ] `build_direction` the same in every stage.
- [ ] `strain_calibration` and `absorptivity` either calibrated or explicitly
      flagged as uncalibrated wherever the numbers are presented.
- [ ] Material numbers traceable — take them from
      [`examples/materials/am-marine.toml`](../../examples/materials/am-marine.toml),
      which carries a source for every value, and remember those are reference
      values for setting up an analysis, not design allowables.
- [ ] The claim you are making is one the model can carry.
