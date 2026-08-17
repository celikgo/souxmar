# Additive manufacturing

Fourteen capabilities across six plugins cover metal and polymer AM:
the process chain, the design-for-additive-manufacturing checks, and
the output writers.

## Which process are you on?

| Process | What souxmar does | Depth |
| ------- | ----------------- | ----- |
| **LPBF** (metal powder bed) | Build mesh → thermal history → melt pool + porosity risk → distortion → residual stress → DfAM → time/cost → report | Full chain. Every default is a 316L-on-a-400 W-machine number. |
| **DED / WAAM** | Time, energy, mass, cost with `process: ded_waam`; distortion and DfAM unchanged; the metal thermal chain reused with the arc read as a moving heat source | Partial — no arc physics, no bead geometry |
| **FFF** (polymer extrusion) | Interlayer thermal cycling → bond strength / Z-strength → DfAM → time/cost → G-code + CLI + report | Full chain, aimed at PEKK and PA12-CF |
| **SLS** (polymer powder bed) | Time, energy, mass, cost with `process: sls` | Partial — the polymer bonding chain is indicative only |

Not covered: binder jetting, material jetting, vat
photopolymerisation, sheet lamination, cold spray, hybrid
additive-subtractive.

## The capabilities

| Capability id | Kind | Gives you |
| ------------- | ---- | --------- |
| `mesher.am.layered` | mesher | Layer-aligned Hex8 build mesh; cell tag = layer index |
| `reader.lattice` | reader | Parametric strut lattice (cubic/bcc/fcc/octet/diamond) as Edge2 beams |
| `solver.am.thermal.lpbf` | solver | Nodal `temperature` (°C), one step per layer |
| `postproc.am.melt_pool` | postproc | Per-cell `[depth_m, normalised_enthalpy, porosity_risk]` |
| `solver.am.distortion.inherent_strain` | solver | Nodal `distortion_displacement` (m), one step per layer |
| `postproc.am.residual_stress` | postproc | Per-cell `[sigma_vm_Pa, sigma_vm_over_yield, layer_index]` |
| `solver.am.polymer.fff` | solver | Nodal `interface_temperature` (°C), one step per layer |
| `postproc.am.bond_strength` | postproc | Per-cell `[degree_of_healing, z_strength_fraction, seconds_above_tg]` |
| `solver.am.overhang` | solver | Per-cell `[min_downskin_tilt_deg, support_needed, downskin_area_m2]` |
| `solver.am.printability` | solver | Per-cell `[printability_score, limiting_factor_code, wall_thickness_proxy_m]` |
| `solver.am.buildtime` | solver | Per-cell `[layer_time_s, cumulative_time_s, layer_area_m2]` |
| `writer.am.gcode` | writer | FFF G-code from a planar slice |
| `writer.am.cli` | writer | Common Layer Interface ASCII |
| `writer.am.report` | writer | Markdown build report / traveller sheet |

Component order is part of the contract — a viewer colouring by
component `2` of `melt_pool` is looking at porosity risk, and that
will not change under you.

Three of these are `solver.*` while doing no solving: `overhang`,
`printability` and `buildtime` analyse a mesh and produce a field,
and the `postproc.*` dispatch path requires an upstream field. See
[ADR-0044](https://github.com/souxmar/souxmar/blob/master/docs/adr/0044-manufacturing-capability-namespaces.md)
— "solver" here is a vtable shape, not a claim.

## Run the metal chain

```bash
souxmar run examples/am-lpbf-bracket/pipeline.yaml \
    --plugin-path build/dev/examples/plugins
```

The pipeline shape:

```yaml
version: 1
stages:
  - id: build
    plugin: mesher.am.layered
    input:
      target_size: 0.002        # voxel edge AND simulation layer, metres

  - id: thermal
    plugin: solver.am.thermal.lpbf
    input:
      mesh: { from: build }
      laser_power: 195.0
      scan_speed: 0.8
      hatch_spacing: 1.1e-4
      layer_height: 0.002       # must match the mesh
      process_layer_height: 3.0e-5   # the machine's powder layer
      interlayer_time: 12.5
      baseplate_layers: 1

  - id: meltpool
    plugin: postproc.am.melt_pool
    input:
      mesh:  { from: build }
      field: { from: thermal }  # postproc.* REQUIRES a field
      laser_power: 195.0        # repeat the process block — a postproc
      scan_speed: 0.8           # cannot see the stage before it
      hatch_spacing: 1.1e-4
      layer_height: 0.002
      process_layer_height: 3.0e-5
```

Three things trip people up:

1. **Two layer heights.** `layer_height` is the simulation layer —
   one field step, and it must match the mesh. `process_layer_height`
   is the machine's powder layer and sets the energy density.
2. **`postproc.*` needs `field: {from: …}`.** A missing field is a
   dispatch error, not a NULL.
3. **Process inputs are repeated per stage.** No plugin reads a
   material file; every property is an explicit input with a default.

## DfAM triage on real geometry

Point the three checks at a real surface mesh, not at a voxel box —
`mesher.am.layered` meshes a bounding box, so its axis-aligned faces
make the overhang answer degenerate.

```yaml
  - id: part
    plugin: reader.stl
    input: { path: bracket.stl }

  - id: overhang
    plugin: solver.am.overhang
    input:
      mesh: { from: part }
      build_direction: [0.0, 0.0, 1.0]
      overhang_threshold_deg: 45.0

  - id: printability
    plugin: solver.am.printability
    input:
      mesh: { from: part }
      min_wall_thickness: 6.0e-4
      machine_build_volume: [0.25, 0.25, 0.30]
      corrosion_allowance: 0.001   # subsea part: extra wall for 25 years

  - id: cost
    plugin: solver.am.buildtime
    input:
      mesh: { from: part }
      process: lpbf
      material_cost_per_kg: 75.0
      machine_rate_per_hour: 45.0
```

A face is downward iff `n·b < 0`, and its tilt from the build plate
is `acos(|n·b|)`; `support_needed` is 1 when any downward face tilts
below the threshold. The 45° default is the powder-bed rule of
thumb, not a material property.

## Slice to G-code

```yaml
  - id: gcode
    plugin: writer.am.gcode
    input:
      mesh: { from: part }
      path: fairing.gcode
      layer_height: 3.0e-4
      road_width: 6.0e-4
      nozzle_temperature: 400.0
      bed_temperature: 140.0
      print_speed: 0.04
      infill_spacing: 0.003
```

Single perimeter, scanline infill, no offset compensation, no
support generation, no bridging logic, no collision check. It is a
geometrically faithful but mechanically naive toolpath — **read it
before you feed it to a machine.**

## Calibrate before you quote

| Constant | Default | Fit it against |
| -------- | ------- | -------------- |
| `strain_calibration` | `0.30` | Tip deflection of a printed twin-cantilever bridge coupon, cut free. Re-fit whenever `layer_height` changes. |
| `absorptivity` | `0.35` | One measured single-track melt-pool cross-section. |
| `reptation_time_reference`, `activation_energy` | `2.0` s, `8.0e4` J/mol | Z-oriented tensile coupons at a small `layer_time` matrix |

Until then the numbers are relative. The step-by-step procedures are
in
[`docs/MANUFACTURING.md`](https://github.com/souxmar/souxmar/blob/master/docs/MANUFACTURING.md#calibration).

## From chat

```
propose_am_setup        propose a runnable pipeline from process + material + machine
check_printability      run the DfAM check and summarise blockers
set_build_orientation   score candidate orientations, stage the winner
estimate_build_cost     time / energy / mass / cost
```

`set_build_orientation` prompts once per session because it changes
what every downstream AM answer computes; the other three are
read-only and silent.

## Next

- [Marine and subsea](/manufacturing/marine)
- [`docs/MANUFACTURING.md`](https://github.com/souxmar/souxmar/blob/master/docs/MANUFACTURING.md)
  — every input key, unit, default and output component, plus the
  fidelity table.
- [RFC-0012](https://github.com/souxmar/souxmar/blob/master/docs/rfcs/0012-am-process-simulation.md)
  — where the closed-form models break and what replaces them.
