# Manufacturing

This document is the domain guide for souxmar's additive-manufacturing (AM) capability block: what processes it covers, what each capability computes, every input key with its unit and default, every output field with its component order, and — most importantly — **what each model is and is not allowed to be used for**.

The marine / subsea half of the same block lives in [`MARINE.md`](MARINE.md). The architectural decision that put these capabilities under the existing five plugin namespaces instead of a new one is [ADR-0044](adr/0044-manufacturing-capability-namespaces.md). The physics roadmap — what replaces the closed-form models, and how it gets validated — is [RFC-0012](rfcs/0012-am-process-simulation.md).

## Status and scope

Every capability described here ships as an **in-tree example plugin** under `examples/plugins/`, built against `souxmar::public_headers` only, loaded through the same C ABI a third-party plugin uses. There is no privileged path: `solver.am.thermal.lpbf` is dispatched exactly the way `solver.elasticity.linear` is.

All of them are **closed-form or heuristic models**. None of them is a finite-element solve, and none of them is a qualification tool. The [fidelity table](#fidelity-what-each-capability-actually-is) below states, per capability, which category it is in and what it must not be used for. Read it before you quote a number from this block to anyone.

The AM process vocabulary used here (powder-bed fusion, directed-energy deposition, material extrusion) is the vocabulary of the ASTM F42 / ISO 52900 terminology standard, cited by name as publicly documented practice. Nothing in souxmar reproduces, implements, or demonstrates compliance with those documents.

## Unit convention

Strict SI throughout — metres, kilograms, seconds, watts, pascals, joules, newtons — with **one documented exception: temperatures are degrees Celsius**, both in YAML input keys and in output fields. Plugins convert to kelvin internally wherever an absolute temperature is physically required (enthalpy, Arrhenius shifts) and say so in a source comment.

Consequences worth internalising:

- `layer_height: 0.001` is one millimetre, not one metre and not one mil.
- `melt_temperature: 1400` is 1400 °C.
- A temperature *difference* needs no conversion — a Celsius interval is already a kelvin interval, which is why `cte · (T_melt − T_preheat)` is written with Celsius inputs and is still correct.
- Money is unitless: `material_cost_per_kg` and `machine_rate_per_hour` are in whatever currency you keep your rates in. souxmar never converts currency.

Determinism is a gate, not an aspiration: the same pipeline produces byte-identical output on Linux, macOS and Windows. Every plugin in this block iterates and accumulates in index order, uses ordered containers only, has no wall-clock or environment reads, and uses fixed iteration counts for every root-find rather than a tolerance exit whose iteration count could differ per platform. If you fork one of these plugins, keep that property — `auditing-determinism` explains why.

## Process taxonomy

Four process families are covered, at three different depths. Being explicit about the depth is the point of this table.

| Family | What it is | What souxmar does | Depth |
| --- | --- | --- | --- |
| **LPBF** — laser powder-bed fusion (metal) | Laser fuses a thin powder layer, recoater spreads the next | Full chain: layered build mesh → layer-wise thermal history → melt-pool geometry + porosity risk → inherent-strain distortion → residual stress → DfAM checks → build time / cost → build report | Primary. Every default in the block is a 316L-on-a-400 W-machine number. |
| **DED / WAAM** — directed-energy deposition, wire-arc | Wire or powder fed into a moving melt pool, no powder bed | Build time, energy, mass and cost through `solver.am.buildtime` with `process: ded_waam` (mass-flow limited); distortion and DfAM apply unchanged. The metal thermal chain is reused with the arc read as an equivalent moving heat source — `laser_power` becomes arc power, `scan_speed` becomes travel speed. | Partial. There is no arc physics, no droplet transfer, no bead geometry model. |
| **FFF / FDM** — material extrusion (polymer) | Thermoplastic road extruded and welded to the road below | Full chain: interlayer thermal cycling → reptation healing / Z-strength → DfAM checks → build time → planar slicing to G-code and CLI → build report | Primary for polymer. Aimed at large-format engineering polymers (PEKK, PA12-CF). |
| **SLS** — polymer powder-bed fusion | Laser fuses polymer powder in a heated bed | Build time, energy, mass and cost through `solver.am.buildtime` with `process: sls`. The polymer thermal/bonding chain can be run against an SLS part, but a laser-scanned powder bed is not a stack of extruded roads, so treat the healing numbers as indicative ordering only. | Partial, and labelled as such in the desktop panel. |

What is **not** covered at all: binder jetting, material jetting, sheet lamination, vat photopolymerisation, cold spray, friction-stir deposition, and every hybrid additive-subtractive process. There is no support-structure generator, no scan-path planner, no machine post-processor beyond the naive G-code writer, and no powder-lifecycle model.

## Capability map

Eighteen capabilities across eight plugin directories. Capability ids are frozen: downstream tooling, the agent's `propose_am_setup` planner, and the desktop panels all name these strings literally.

| # | Capability id | Kind | Plugin directory | Purpose |
| --- | --- | --- | --- | --- |
| 1 | `mesher.am.layered` | mesher | `am-layered-mesher` | Layer-aligned Hex8 build mesh; cell tag = layer index |
| 2 | `reader.lattice` | reader | `lattice-reader` | Parametric strut-lattice beam mesh from a spec file |
| 3 | `solver.am.thermal.lpbf` | solver | `am-thermal` | Layer-wise LPBF thermal history (Rosenthal) |
| 4 | `postproc.am.melt_pool` | postproc | `am-thermal` | Melt-pool size + porosity risk (lack-of-fusion / keyhole) |
| 5 | `solver.am.distortion.inherent_strain` | solver | `am-distortion` | Part-scale residual distortion, inherent-strain method |
| 6 | `postproc.am.residual_stress` | postproc | `am-distortion` | von-Mises-equivalent residual stress |
| 7 | `solver.am.polymer.fff` | solver | `am-polymer` | FFF/FDM interlayer thermal cycling |
| 8 | `postproc.am.bond_strength` | postproc | `am-polymer` | Interlayer degree of healing / Z-strength |
| 9 | `solver.am.overhang` | solver | `am-manufacturability` | Downskin / overhang / support need per cell |
| 10 | `solver.am.printability` | solver | `am-manufacturability` | Composite DfAM printability score |
| 11 | `solver.am.buildtime` | solver | `am-manufacturability` | Per-layer build time, energy, mass, cost |
| 12 | `writer.am.gcode` | writer | `am-slicer` | Planar slice → FFF G-code |
| 13 | `writer.am.cli` | writer | `am-slicer` | Planar slice → Common Layer Interface ASCII |
| 14 | `writer.am.report` | writer | `am-slicer` | Markdown build report / traveller sheet |
| 15 | `solver.marine.hydrostatic` | solver | `marine` | Hydrostatic pressure per depth load case |
| 16 | `solver.marine.hull_collapse` | solver | `marine` | Pressure-hull collapse margin |
| 17 | `solver.marine.corrosion` | solver | `marine` | Seawater corrosion / galvanic / CP demand |
| 18 | `writer.marine.qualification_report` | writer | `marine` | Advisory AM-part qualification dossier |

Capabilities 15–18 are documented in detail in [`MARINE.md`](MARINE.md); this file covers 1–14 and the conventions all eighteen share.

Two of those kinds will look wrong at first glance and are explained in ADR-0044:

- **`solver.am.overhang`, `solver.am.printability` and `solver.am.buildtime` are `solver.*` despite consuming no field.** The postproc dispatch path hard-requires an upstream `field: {from: …}` handle (`src/pipeline/registry_dispatcher.cpp:252`) — a missing field is a dispatch error, not a NULL. A mesh-only analysis therefore cannot be a postproc. "Solver" here is a vtable shape, not a claim that these capabilities solve a PDE.
- **`reader.lattice` is a `reader.*` despite generating geometry from parameters rather than reading a part.** Meshers get no value bag (`souxmar_mesher_mesh_fn(geometry, options, out_mesh, user_data)`), so a fully-parametric generator has nowhere to read `unit_cell` / `cell_size` / `relative_density` from. Readers get `(path, inputs, options)`.

## Shared conventions

### Build direction

`build_direction` is a list of three numbers, default `[0.0, 0.0, 1.0]`, normalised internally. A zero-length vector is rejected with `SOUXMAR_E_INVALID_ARGUMENT`. Every capability that has an "up" — thermal, distortion, overhang, printability, buildtime, slicing, hydrostatic depth — takes it.

`mesher.am.layered` is the exception: it always builds `+Z`, because the mesher ABI gives it no value bag to be told otherwise. Every downstream default is `[0, 0, 1]`, so the pair agrees out of the box.

### Layer resolution

Every AM capability resolves a cell's build layer identically:

1. If `souxmar_mesh_cell_tag(mesh, c) >= 0`, **that value is the layer index**.
2. Otherwise, bin the cell centroid's projection on `build_direction` by `layer_height`, measured from the mesh bounding-box minimum along that axis.

`num_layers` is the maximum resolved layer index plus one, unless the `num_layers` input is greater than zero, in which case it clamps to that.

Rule 1 is why `mesher.am.layered` exists: it stamps the layer index into the cell tag, so no downstream capability has to re-bin coordinates. Rule 1 is also a trap — the ABI documents `tag` as the *inherited geometry-entity id*, so a mesh from a conforming mesher that carries geometry tags will be mis-binned into "layers" that are really CAD faces. Either mesh with `mesher.am.layered`, or make sure the tags are cleared so the coordinate fallback runs.

### Boundary-face tags from `mesher.am.layered`

Outer boundary faces carry: `10` = −X, `11` = +X, `12` = −Y, `13` = +Y, `14` = −Z (baseplate interface / downskin), `15` = +Z (top / upskin). Interior faces stay `SOUXMAR_FACE_UNTAGGED`. The local-face ordering is the host's canonical Hex8 table in `src/core/face_topology.cpp`, not a guess.

### Multi-metric output fields

A `souxmar_field_t` carries 1 component (`SOUXMAR_FK_SCALAR`) or 3 (`SOUXMAR_FK_VECTOR`). A capability with three related metrics packs them into a VECTOR field and documents the component order — the same pattern `postproc.mesh_quality` already uses. **The component order is part of the contract.** A viewer colouring by component 2 of `melt_pool` is looking at porosity risk; that will not change under you.

### Materials

**No plugin in this block reads a material file.** Every material property is an explicit input with a documented default. This is deliberate: it keeps the plugins dependency-free, keeps the YAML self-documenting, and makes it impossible for a silent library lookup to change your answer between machines.

The curated reference library lives at `examples/materials/am-marine.toml` with a source for every number, and is mirrored as a static table inside the `propose_am_setup` agent tool and the desktop panels. It is editorial reference data — public literature values for comparing alloys — not certification data.

### Nested YAML

Upstream handles arrive under the fixed keys `geometry`, `mesh`, `field`. Everything else in a stage's `input:` map is passed to the plugin verbatim; nothing is stripped. Host-recognised option keys are `tolerance`, `max_iterations`, `random_seed` for solvers and postprocs, and `target_size`, `optimize`, `element_order`, `random_seed` for meshers. Unknown keys are ignored by every capability here, which is what makes it safe to paste one process block into several stages.

## Capability reference

Notation: `key (unit, default)`. Every key is optional unless marked **required**. Every numeric input is clamped to a documented range; no user value is ever divided by unchecked.

### `mesher.am.layered` — layer-aligned build mesh

Options only — the mesher ABI passes no value bag.

| Option | Meaning |
| --- | --- |
| `target_size` (m, `0.005` when ≤ 0) | The voxel edge **and** the simulation layer thickness. Clamped `[1e-5, 1.0]`. It is an upper bound: the grid divides the extent box exactly, so the realised layer thickness is ≤ `target_size`. |
| `element_order` (`1`) | `1` only. `2` returns `SOUXMAR_E_NOT_IMPLEMENTED` rather than silently downgrading. |
| `optimize` | Accepted and ignored — a structured voxel grid is already at the optimum of every quality metric souxmar computes. |
| `random_seed` | Ignored; generation is fully deterministic. |

Extent is the input geometry's bounding box when a geometry is supplied and the box is non-degenerate; otherwise the documented demo build box `[0, 0, 0] … [0.06, 0.02, 0.02]` m (a 60 × 20 × 20 mm coupon). The fallback exists because there is literally no other channel through which a pipeline could hand this mesher a box. Hard cap 200 000 cells; above that, `SOUXMAR_E_INVALID_ARGUMENT`.

Output: Hex8 mesh, standard VTK/Gmsh Hex8 node ordering (bottom quad CCW, then the top quad above it). **Cell tag = layer index**, 0 at the build plate. Face tags as above.

It reads the geometry's *bounding box only* — no faces, no trimming, no staircase-free surface. A part is represented by its envelope, which over-predicts mass and cross-sectional area.

### `reader.lattice` — parametric strut lattice

`path` (**required**, string) points at a lattice spec file: UTF-8 text, `#` comments, one `key = value` per line, values numbers / bare strings / `[a, b, c]` lists. The recognised spec keys are exactly the YAML keys below, and **a value in the stage's `input:` map overrides the file** — that is the intended way to sweep one parameter while keeping the geometry in version control. A malformed line is `SOUXMAR_E_IO` naming the offending line number.

| Key | Unit | Default | Notes |
| --- | --- | --- | --- |
| `bbox` | m (list of 6) | `[0,0,0,0.05,0.05,0.05]` | `xmin,ymin,zmin,xmax,ymax,zmax` |
| `unit_cell` | — | `bcc` | `cubic` \| `bcc` \| `fcc` \| `octet` \| `diamond`; anything else is `SOUXMAR_E_INVALID_ARGUMENT` |
| `cell_size` | m | `0.005` | clamped `[1e-4, 1.0]` |
| `strut_diameter` | m | `0.0` | `0` ⇒ derive from `relative_density` |
| `relative_density` | — | `0.15` | clamped `[0.01, 0.6]` |
| `nx`, `ny`, `nz` | — | derived from bbox / `cell_size` | each clamped `[1, 60]` |
| `max_cells` | — | `400000` | guard rail on emitted Edge2 cells |

Output: a **mesh** (`out_geometry` left NULL) of Edge2 cells, one per strut, de-duplicated by a sorted (lo, hi) node-index key. **Cell tag = strut family**: `0` X-axial, `1` Y-axial, `2` Z-axial, `3` body-diagonal, `4` face-diagonal.

Relative density ↔ strut diameter is the swept-cylinder relation

```
rho* = (pi·d²/4)·L_total / V_lattice        =>   d = sqrt(4·rho*·V_lattice / (pi·L_total))
```

with `L_total` the summed length of the *generated* struts (so boundary trimming is accounted for exactly). It ignores the volume double-counted where struts meet at a node, so it **over-predicts density** — a couple of percent below `rho* ≈ 0.3`, badly above it. Above 0.3, size the struts explicitly and treat the density as nominal.

Struts are trimmed at the box, not at a part surface, so boundary struts dangle. That is deliberate: culling them would silently change the relative density.

### `solver.am.thermal.lpbf` — layer-wise LPBF thermal history

| Key | Unit | Default | | Key | Unit | Default |
| --- | --- | --- | --- | --- | --- | --- |
| `laser_power` | W | `200` | | `density` | kg/m³ | `7990` |
| `scan_speed` | m/s | `0.8` | | `specific_heat` | J/(kg·K) | `500` |
| `hatch_spacing` | m | `1.1e-4` | | `melt_temperature` | °C | `1400` |
| `layer_height` | m | `0.001` | | `interlayer_time` | s | `10.0` |
| `process_layer_height` | m | `3.0e-5` | | `build_direction` | — | `[0,0,1]` |
| `absorptivity` | — | `0.35` | | `baseplate_layers` | — | `0` |
| `preheat_temperature` | °C | `80` | | `num_layers` | — | `0` ⇒ from the mesh |
| `baseplate_temperature` | °C | `80` | | | | |
| `thermal_conductivity` | W/(m·K) | `15.0` | | | | |

Note the two layer heights. `layer_height` is the **simulation** layer — one field time step, and it must match the mesh. `process_layer_height` is the **machine's** powder layer, and it sets the energy density and the fusion criterion. One simulation layer therefore lumps `layer_height / process_layer_height` machine layers, and `interlayer_time` is the dwell between *simulation* layers: if you lump 30 machine layers, pass 30 × (recoat + scan).

Output: `souxmar_field_new("temperature", SOUXMAR_FL_NODAL, SOUXMAR_FK_SCALAR, num_nodes, num_layers)` in °C. Step `j` is the state just after layer `j` is scanned — nodes in layer `j` at their Rosenthal mid-hatch peak, nodes below decaying toward `baseplate_temperature`, nodes in not-yet-built layers at `preheat_temperature`.

The moving-source solution is Rosenthal's (1946) surface point source on a semi-infinite solid,

```
T(w, y, z) = T0 + q / (2·pi·k·R) · exp(−v·(R + w) / (2·alpha)),   q = absorptivity·laser_power
R = sqrt(w² + y² + z²),   alpha = k / (rho·c),   w > 0 ahead of the source
```

with `2·pi` rather than `4·pi` because a source on a free surface spreads into a hemisphere. Peak temperature is evaluated at the mid-hatch offset `hatch_spacing/2` — the coldest line between two adjacent tracks, which is the conservative place to ask "did this layer melt" — and capped at a documented vaporisation ceiling, because a point source is singular at `R = 0`.

### `postproc.am.melt_pool` — melt-pool geometry and porosity risk

Consumes the nodal `temperature` field. Takes the same process inputs as the thermal solver (repeat the block; the ABI gives a postproc no channel to the solver stage that ran before it).

Output: `souxmar_field_new("melt_pool", SOUXMAR_FL_CELL, SOUXMAR_FK_VECTOR, num_cells, 1)`.

| Component | Name | Unit |
| --- | --- | --- |
| `[0]` | `melt_pool_depth_m` | m |
| `[1]` | `normalised_enthalpy` (ΔH/h_s) | — |
| `[2]` | `porosity_risk` | 0…1 |

Melt-pool depth is the `melt_temperature` isotherm on the centreline under the source; width is the widest point of the same isotherm on the free surface. Normalised enthalpy is the keyhole-onset group of Hann et al. (2011) / King et al. (2014),

```
dH / h_s = A·P / (h_s·sqrt(pi·alpha·v·sigma³)),    h_s = rho·c·T_melt[K]
```

with `sigma` the beam radius (a documented constant, not an input — there is no contract key for it).

`porosity_risk` is the **maximum** of two competing sub-scores, because the two mechanisms sit at opposite ends of the process window:

- **Lack of fusion**, from Tang, Pistorius & Beuth (2017): with `h` = hatch spacing, `t` = `process_layer_height`, `W` and `D` the pool width and depth, fully fused material satisfies `I_lof = (h/W)² + (t/D)² ≤ 1`. The sub-score is zero while that criterion holds and rises past it, saturating at `I_lof = 4`. The severe anchor is an interpolation choice, not a measured threshold.
- **Keyholing**: zero below `dH/h_s = 30` (King et al.'s measured conduction→keyhole transition), saturating at 60 — an interpolation anchor for the developed keyhole regime, not a measured threshold.

Rosenthal systematically **under**-predicts LPBF conduction-mode melt-pool depth (commonly by 1.5–2×) because it has no latent heat, no Marangoni convection, no surface depression, and no multiple scattering in the powder bed. It therefore **over**-predicts lack-of-fusion risk. Calibrate `absorptivity` against one measured single-track cross-section before trusting any of these numbers.

### `solver.am.distortion.inherent_strain` — part-scale residual distortion

| Key | Unit | Default | Notes |
| --- | --- | --- | --- |
| `youngs_modulus` | Pa | `1.9e11` | drives the stress read-back, not the curvature |
| `poisson_ratio` | — | `0.28` | |
| `cte` | 1/K | `1.6e-5` | |
| `melt_temperature` | °C | `1400` | |
| `preheat_temperature` | °C | `80` | |
| `inherent_strain` | — | `0.0` | `0` ⇒ derive as `−cte·(T_melt − T_preheat)·strain_calibration`; a supplied value is used as given (negative shrinks) |
| `strain_calibration` | — | `0.30` | **must be calibrated against a measured part** — see [Calibration](#calibration) |
| `layer_height` | m | `0.001` | |
| `substrate_thickness` | m | `0.0` | `0` ⇒ from the mesh |
| `build_direction` | — | `[0,0,1]` | |
| `baseplate_layers` | — | `0` | |
| `baseplate_clamped` | bool | `true` | |
| `num_layers` | — | `0` | `0` ⇒ from the mesh |

Output: `souxmar_field_new("distortion_displacement", SOUXMAR_FL_NODAL, SOUXMAR_FK_VECTOR, num_nodes, num_layers)` in metres, `[ux, uy, uz]`. Step `j` is the distortion after depositing layer `j`, so the field animates the build. With `baseplate_clamped` and `baseplate_layers > 0`, baseplate-layer nodes read exactly zero at every step — that is the fixture-datum frame, not a free-body frame.

The method is Keller & Ploshikhin's inherent strain (2014): the whole thermo-mechanical history of one layer is lumped into a stress-free strain, applied mechanically layer by layer on the growing part. Each new layer of thickness `h` bonded to `t_below` already-built material contributes a curvature increment

```
dkappa = 6·(−eps*)·h·t_below / (h + t_below)³
```

which for `h << t_below` is exactly Stoney's thin-film relation `kappa = 6·(−eps*)·t_f / t_s²` (Stoney, 1909) in the equal-modulus limit — which is why `E` and `nu` do not scale the curvature. Increments accumulate against the *current* composite thickness, so distortion grows monotonically with layer count and falls as the substrate thickens.

There is no stiffness matrix, no equilibrium iteration, no plasticity, and no geometry sensitivity beyond the bounding box, the layer bins, and each node's distance from the centroid axis. The displacement field is kinematically admissible but does not satisfy equilibrium: it reproduces the *shape and the trend*, not the field. A part that is not roughly plate-like (a thin tube, a lattice) is outside the plate idealisation Stoney rests on.

### `postproc.am.residual_stress` — equivalent residual stress

Consumes `distortion_displacement`. Re-reads every elastic input above, plus `yield_strength` (Pa, `5.0e8`).

Output: `souxmar_field_new("residual_stress", SOUXMAR_FL_CELL, SOUXMAR_FK_VECTOR, num_cells, 1)`.

| Component | Name | Notes |
| --- | --- | --- |
| `[0]` | `sigma_vm_Pa` | capped at `yield_strength` — an ideal-plastic radial-return cap, so the component stays a stress |
| `[1]` | `sigma_vm_over_yield` | `[0] / yield_strength`, so it is bounded by 1; a cell reading 1.0 is at yield and has plastically relieved |
| `[2]` | `layer_index` | the cell's resolved build layer, as a double |

The raw elastic overshoot is deliberately not reported. It is not a stress, and it is not mesh-convergent: differentiating the displacement ansatz across the artificial discontinuity at a clamped baseplate reached 86 × yield (43 GPa) on a 2 mm demo mesh — an artefact of the kinematic clamp, not a prediction. Every value in this field is therefore one the model can defend.

The recovery is the inherent-strain method's own: a least-squares displacement gradient over the cell's nodes, total small strain, minus the eigenstrain applied to that cell, through isotropic Hooke, to a von Mises equivalent. One averaged strain per cell — it cannot resolve the near-surface gradient that hole-drilling or XRD actually measures. With `baseplate_clamped`, the datum layers report exactly zero stress; the real baseplate carries that reaction and is usually where a part cracks off.

### `solver.am.polymer.fff` — FFF interlayer thermal cycling

| Key | Unit | Default | | Key | Unit | Default |
| --- | --- | --- | --- | --- | --- | --- |
| `nozzle_temperature` | °C | `250` | | `density` | kg/m³ | `1010` |
| `bed_temperature` | °C | `100` | | `specific_heat` | J/(kg·K) | `1800` |
| `chamber_temperature` | °C | `40` | | `thermal_conductivity` | W/(m·K) | `0.25` |
| `layer_time` | s | `20` | | `glass_transition_temperature` | °C | `145` |
| `layer_height` | m | `2.0e-4` | | `build_direction` | — | `[0,0,1]` |
| `road_width` | m | `4.0e-4` | | `baseplate_layers` | — | `0` |
| `convection_coefficient` | W/(m²·K) | `30` | | `num_layers` | — | `0` ⇒ from the mesh |

Output: `souxmar_field_new("interface_temperature", SOUXMAR_FL_NODAL, SOUXMAR_FK_SCALAR, num_nodes, num_layers)` in °C — the interface-temperature history at each node's layer, one step per layer.

Lumped-capacitance cooling, `T(t) = T_env + (T_dep − T_env)·exp(−t/tau)` with `tau = rho·c·V/(h·A)`; the characteristic length comes from `layer_height` and `road_width` counting the welded bottom face as non-convecting. When the next road lands, the interface starts at the mean of the new road and the substrate — the contacting-semi-infinite-bodies result for matched properties. The construction follows the FDM bond-formation literature (Yang & Pitchumani 2002; Sun et al. 2008).

Read the field for what it is: an **interface** field. A node plane reads chamber temperature until the layer above it lands, so a freshly deposited top surface reads cold. What the field tracks is each weld, from the instant that weld is made.

### `postproc.am.bond_strength` — interlayer healing and Z-strength

Consumes `interface_temperature`.

| Key | Unit | Default |
| --- | --- | --- |
| `glass_transition_temperature` | °C | `145` |
| `reptation_time_reference` | s | `2.0` |
| `reptation_reference_temperature` | °C | `260` |
| `activation_energy` | J/mol | `8.0e4` |
| `layer_time` | s | `20` |

`layer_time` is re-declared here because ABI v1 fields carry no time axis — the postproc cannot recover the sample spacing from the field and must be told. Keep it equal to the solver's.

Output: `souxmar_field_new("bond_quality", SOUXMAR_FL_CELL, SOUXMAR_FK_VECTOR, num_cells, 1)`.

| Component | Name | Unit |
| --- | --- | --- |
| `[0]` | `degree_of_healing` | 0…1 |
| `[1]` | `z_strength_fraction` | 0…1 |
| `[2]` | `seconds_above_tg` | s |

Yang & Pitchumani's reptation healing law in its non-isothermal (reduced weld time) form:

```
t_rep(T) = t_rep_ref · exp( (E_a/R)·(1/T − 1/T_ref) )      [T in K]
xi       = integral over t where T > Tg of dt / t_rep(T(t))
D_h      = min(1, xi^0.25)
```

Only time above `Tg` counts — below it the chains are frozen and no reptation occurs. `z_strength_fraction` applies a fixed geometric contact fraction on top of `D_h` for the inter-bead void that healing cannot close; it is a documented heuristic, not a qualified allowable.

### `solver.am.overhang` — downskin, overhang, support need

| Key | Unit | Default |
| --- | --- | --- |
| `build_direction` | — | `[0,0,1]` |
| `overhang_threshold_deg` | ° | `45` |
| `layer_height` | m | `0.001` |

Boundary faces are computed by the plugin itself: a face is on the boundary when no other cell shares the same node set, keyed by a **sorted** node-index vector in an ordered container (never `unordered_map` iteration — that would break the determinism gate). Tri3/Quad4 surface meshes and Tet4/Hex8/Prism6/Pyramid5 volume meshes are handled.

The definitions, verbatim: for an outward unit face normal `n` and unit build direction `b`, the face is **downward** iff `n·b < 0`; its **tilt from the build plate** is `acos(|n·b|)` in degrees; `support_needed = 1` iff any downward boundary face has tilt `< overhang_threshold_deg` (strict, so a face exactly at the threshold is not flagged).

Output: `souxmar_field_new("overhang", SOUXMAR_FL_CELL, SOUXMAR_FK_VECTOR, num_cells, 1)`.

| Component | Name | Notes |
| --- | --- | --- |
| `[0]` | `min_downskin_tilt_deg` | `90` when the cell has no downward boundary face |
| `[1]` | `support_needed` | 0 or 1 |
| `[2]` | `downskin_area_m2` | total downward boundary area of the cell |

The 45° default is the powder-bed rule of thumb reproduced in every vendor DfAM guide. It is a rule of thumb, not a material property: real self-supporting angles run roughly 30–50° with alloy, layer height and beam parameters.

Note the geometry trap: a voxelised box has axis-aligned faces only, so on a `mesher.am.layered` mesh the layer-0 cells report a 0° downskin (the baseplate interface, which this capability cannot recognise as supported) and every other cell reports the 90° no-downskin sentinel. That is the honest answer for that geometry and a useless one for a real part — run the overhang check against a conforming surface mesh (`reader.obj`, `reader.stl`, or a conforming mesher).

### `solver.am.printability` — composite DfAM score

| Key | Unit | Default |
| --- | --- | --- |
| `build_direction` | — | `[0,0,1]` |
| `overhang_threshold_deg` | ° | `45` |
| `min_wall_thickness` | m | `5.0e-4` |
| `machine_build_volume` | m (list of 3) | `[0.25, 0.25, 0.3]` |
| `corrosion_allowance` | m | `0.0` |
| `layer_height` | m | `0.001` |

Output: `souxmar_field_new("printability", SOUXMAR_FL_CELL, SOUXMAR_FK_VECTOR, num_cells, 1)`.

| Component | Name | Notes |
| --- | --- | --- |
| `[0]` | `printability_score` | 0…1, 1 = trivially printable |
| `[1]` | `limiting_factor_code` | `0` none, `1` overhang, `2` thin wall, `3` build-volume fit, `4` aspect ratio / height, `5` corrosion allowance |
| `[2]` | `wall_thickness_proxy_m` | |

One hard gate plus four weighted penalties, each penalty in `[0, 1]`:

- **Build-volume fit** is a gate: if the axis-aligned bounding box does not fit `machine_build_volume` component-wise, the score is 0 and the limiting factor is `3` on every cell. No re-orientation search happens here — that is what the `set_build_orientation` agent tool is for.
- `p_overhang = (threshold − min_downskin_tilt) / threshold`, clamped, zero when the cell has no sub-threshold downward facet.
- `p_thin = (min_wall_thickness − proxy) / min_wall_thickness`, clamped.
- `p_aspect = (slenderness − 8) / 8`, clamped, with slenderness = part height along `b` over the smaller in-plane extent. 8:1 is a vendor rule of thumb for recoater collision and toppling risk, not a validated limit. It is global, so identical on every cell.
- `p_corr = (min_wall_thickness + 2·corrosion_allowance − proxy) / (min_wall_thickness + 2·corrosion_allowance)`, clamped, only when a corrosion allowance is given. The factor 2 assumes both faces of the wall are wetted.

```
printability_score = clamp(1 − (0.40·p_overhang + 0.30·p_thin + 0.15·p_aspect + 0.15·p_corr), 0, 1)
```

`limiting_factor_code` names the largest **weighted** term, ties broken in code order.

`wall_thickness_proxy_m` is `2·V_cell / A_boundary(cell)`, which is exactly the wall thickness for a wall meshed with a single cell through its thickness. For anything else it is mesh-resolution dependent — a cell with one boundary facet reports twice its own height, whatever the real wall is. A true local thickness needs a medial-axis or ray-cast computation on geometry the mesh ABI does not carry.

The weights are hand-chosen and calibrated against nothing. The score is a **triage aid for ranking candidate designs and orientations**. It will happily score an unbuildable part above 0.9 when the reason it is unbuildable is something no term looks at: trapped powder, unreachable internal channels, minimum feature size below the beam width, thermal-stress-driven recoater crashes.

### `solver.am.buildtime` — time, energy, mass, cost

| Key | Unit | Default | | Key | Unit | Default |
| --- | --- | --- | --- | --- | --- | --- |
| `process` | — | `lpbf` | | `deposition_rate` | kg/h | `3.0` |
| `layer_height` | m | `0.001` | | `laser_power` | W | `200` |
| `process_layer_height` | m | `3.0e-5` | | `machine_power_overhead` | W | `2500` |
| `scan_speed` | m/s | `0.8` | | `material_cost_per_kg` | currency/kg | `90` |
| `hatch_spacing` | m | `1.1e-4` | | `machine_rate_per_hour` | currency/h | `45` |
| `recoat_time` | s per machine layer | `8.0` | | `density` | kg/m³ | `7990` |
| `print_speed` | m/s | `0.05` | | `build_direction` | — | `[0,0,1]` |
| `road_width` | m | `4.0e-4` | | | | |

`process` is one of `lpbf` \| `fff` \| `ded_waam` \| `sls`; anything else is `SOUXMAR_E_INVALID_ARGUMENT`.

Per-layer cross-sectional area comes from binning cell volume into layer bins (`area = volume_in_bin / layer_height`). For a surface-only mesh it is the `|n·b|`-weighted projected facet area, which for a closed shell counts both the up-facing and the down-facing facet of every column and therefore over-predicts the true cross-section by roughly 2×.

With `machine_layers = max(1, round(layer_height / process_layer_height))`, per simulation layer:

```
lpbf, sls : t = machine_layers · (area / (hatch_spacing·scan_speed) + recoat_time)
fff       : t = machine_layers · area / (road_width·print_speed)
ded_waam  : t = (area·layer_height) / (deposition_rate / (3600·density))
```

An empty layer inside the build height still costs `machine_layers·recoat_time` on a powder-bed machine and nothing on FFF or DED.

Output: `souxmar_field_new("buildtime", SOUXMAR_FL_CELL, SOUXMAR_FK_VECTOR, num_cells, 1)` with `[0] layer_time_s` (this cell's layer), `[1] cumulative_time_s`, `[2] layer_area_m2`. The last five cost/rate keys are read and range-checked here but unused — the field has three components, and energy / mass / cost totals are re-derived by `writer.am.report` from this field plus the same keys, so one YAML block drives both stages.

This is a pure geometric scan-length model. It ignores skywriting and jump times, contour-versus-infill parameter sets, multi-laser splitting, purge and warm-up, dose factors, machine start/stop and every operator intervention. Real LPBF jobs commonly run 10–40 % longer.

### `writer.am.gcode` — planar slice to FFF G-code

`path` (**required**), `layer_height` (m, `2.0e-4`), `road_width` (m, `4.0e-4`), `filament_diameter` (m, `1.75e-3`), `nozzle_temperature` (°C, `250`), `bed_temperature` (°C, `100`), `print_speed` (m/s, `0.05`), `travel_speed` (m/s, `0.15`), `infill_spacing` (m, `0.002`; ≤ 0 disables infill), `infill_angle_deg` (°, `45`), `retract_length` (m, `0.001`; 0 = off), `flow_multiplier` (—, `1.0`), `build_direction` (`[0,0,1]`), `max_layers` (`5000`).

The mesh's triangulated boundary is sliced with planes at layer mid-heights, segments are chained into closed contours by tolerance-based endpoint matching, and the emitted file is: a `;` header block listing every parameter, `M104`/`M140`/`G28`/`G90`/`M82`, then per layer a `G1 Z…` plus one perimeter pass per contour and alternating ±`infill_angle_deg` scanline infill, then a trailer turning the heaters off. Extrusion follows the volumetric road model every FFF slicer uses,

```
dE = flow_multiplier · road_width · layer_height · L / (pi/4 · filament_diameter²)
```

with absolute `E`. souxmar inputs are SI; G-code is mm and mm/min, so lengths are ×1000 and speeds ×60000.

Determinism: contours sorted by (min y, min x) of their lowest point, scanlines in increasing coordinate order, quads split into two triangles by a fixed rule.

Limitations, stated plainly: single perimeter, no tool-radius offset compensation (the bead straddles the true surface by half a road width), no support generation, no bridging logic, no skirt or brim, no cooling or acceleration control, no seam placement, no collision or build-volume check. It is a geometrically faithful but **mechanically naive** toolpath. Read it before you feed it to a machine.

### `writer.am.cli` — Common Layer Interface ASCII

`path` (**required**), `layer_height` (m, `0.001`), `units` (mm per CLI unit, `1.0`), `build_direction` (`[0,0,1]`), `label` (string, `"souxmar"`), `binary` (bool, `false`; `true` ⇒ `SOUXMAR_E_NOT_IMPLEMENTED`).

Emits `$$HEADERSTART`, `$$ASCII`, `$$UNITS/…`, `$$VERSION/200`, `$$LABEL/…`, `$$DATE/010100` (fixed — no wall clock), `$$DIMENSION/…`, `$$LAYERS/n`, `$$HEADEREND`, `$$GEOMETRYSTART`, then per layer `$$LAYER/z` and `$$POLYLINE/id,dir,n,x1,y1,…`, then `$$GEOMETRYEND`. It shares the slicing code with the G-code writer (`slicer.hpp` / `slicer.cpp` in the same plugin directory), so a contour that is wrong in one is wrong in both — which is the point of sharing it.

### `writer.am.report` — Markdown build report

`path` (**required**), `title` (string, `"souxmar AM build report"`), `process` (`lpbf`), `material` (string, `"316L"`), `design_notes` (string, `""`), plus the cost and rate keys of `solver.am.buildtime` it needs to total up.

A writer receives exactly **one** field, so the report dispatches on `souxmar_field_name()` over `temperature` / `melt_pool` / `distortion_displacement` / `residual_stress` / `interface_temperature` / `bond_quality` / `overhang` / `printability` / `buildtime` / `corrosion` / `collapse_margin` / `hydrostatic_pressure` and renders the matching section. An unrecognised name gets a generic min/max/mean/component-count section; a NULL field gets a "no field supplied" note. Want two sections? Two writer stages.

Always rendered: a mesh summary (node and cell counts, element histogram, bounding box), the process parameters, the build totals, and a `Traceability` section carrying an FNV-1a 64-bit digest of node coordinates plus connectivity — **a non-cryptographic content digest, not a signature**. It detects an accidentally different mesh; it does not detect a deliberately altered one. No wall-clock, no absolute paths, no environment values, so two runs on two machines produce byte-identical output.

## Fidelity: what each capability actually is

Three fidelity classes:

- **Closed-form** — an analytical solution of an idealised problem, with a named literature source. Trends and orders of magnitude are meaningful; absolute values need calibration.
- **Heuristic** — a weighted or thresholded index with hand-chosen constants. Only the *ordering* between two designs is meaningful.
- **Exact** — a geometric or bookkeeping computation with no physical model in it. It is as right as its inputs.

| Capability | Class | Do not use it for |
| --- | --- | --- |
| `mesher.am.layered` | Exact (structured grid) | Representing a part shape. It meshes a bounding box, not your geometry. |
| `reader.lattice` | Exact geometry, **approximate** density relation | Density above `rho* ≈ 0.3`; any claim about lattice stiffness or strength (no section property crosses the reader ABI); printability of thin struts. |
| `solver.am.thermal.lpbf` | Closed-form (Rosenthal) | Absolute temperatures anywhere; melt-pool depth without calibrated absorptivity; anything scan-strategy dependent; tall-part bulk temperature (no interlayer superposition). |
| `postproc.am.melt_pool` | Closed-form + **heuristic thresholds** | Accept/reject decisions on porosity. Process-window *ranking* only; the severe anchors are interpolation choices. |
| `solver.am.distortion.inherent_strain` | Closed-form (inherent strain + Stoney) | Predicting distortion magnitude before `strain_calibration` is fitted to a measured part; non-plate-like geometry; post-cut springback; anything requiring equilibrium. |
| `postproc.am.residual_stress` | Closed-form elastic read-back with a yield cap | Comparison with hole-drilling or XRD measurements; fatigue assessment; post-HIP or post-heat-treat states (no relief is modelled). |
| `solver.am.polymer.fff` | Closed-form (lumped capacitance) | Thick large-format beads (the Biot number leaves the lumped regime); crystallisation-plateau behaviour; in-plane variation of any kind. |
| `postproc.am.bond_strength` | Closed-form healing law + **heuristic** contact fraction | Declaring a Z-direction allowable. Believe the ordering between two process windows; do not believe the absolute healing number without a coupon test. |
| `solver.am.overhang` | Exact (facet geometry) | Support volume, support cost, or a support strategy. It flags cells; it generates nothing. Voxel meshes give degenerate answers. |
| `solver.am.printability` | **Heuristic** (hand-weighted) | Any go/no-go gate. It cannot see trapped powder, internal channels, minimum feature size, or thermal risk. |
| `solver.am.buildtime` | Closed-form geometric | Quoting a job. No jumps, no scan strategy, no warm-up, no labour, no post-processing, no scrap. |
| `writer.am.gcode` | Exact slicing, naive toolpath | Printing without reading it. No offset compensation, no supports, no bridging, no collision check. |
| `writer.am.cli` | Exact slicing | Machines that need binary CLI (`binary: true` is `SOUXMAR_E_NOT_IMPLEMENTED`). |
| `writer.am.report` | Exact bookkeeping | Certification, traceability in the regulatory sense, or tamper evidence. The digest is FNV-1a. |

For the marine capabilities' fidelity, see [`MARINE.md`](MARINE.md) — and its advisory-only section, which applies to every marine output.

## Worked pipelines

### Metal: the LPBF process chain

`examples/am-lpbf-bracket/pipeline.yaml` runs the whole metal chain. The core of it:

```yaml
version: 1
stages:
  - id: build
    plugin: mesher.am.layered
    input:
      target_size: 0.002          # voxel edge AND simulation layer, metres
      element_order: 1

  - id: thermal
    plugin: solver.am.thermal.lpbf
    input:
      mesh: { from: build }
      laser_power: 195.0
      scan_speed: 0.8
      hatch_spacing: 1.1e-4
      layer_height: 0.002         # must match the mesh
      process_layer_height: 3.0e-5  # the machine's powder layer
      absorptivity: 0.35
      preheat_temperature: 80.0
      baseplate_temperature: 80.0
      thermal_conductivity: 15.0
      density: 7990.0
      specific_heat: 500.0
      melt_temperature: 1400.0
      interlayer_time: 12.5
      baseplate_layers: 1

  - id: meltpool
    plugin: postproc.am.melt_pool
    input:
      mesh:  { from: build }
      field: { from: thermal }     # postproc.* REQUIRES this
      laser_power: 195.0           # repeat the process block; a postproc
      scan_speed: 0.8              # has no channel to the stage before it
      hatch_spacing: 1.1e-4
      layer_height: 0.002
      process_layer_height: 3.0e-5
      absorptivity: 0.35
      thermal_conductivity: 15.0
      density: 7990.0
      specific_heat: 500.0
      melt_temperature: 1400.0

  - id: write-melt-pool
    plugin: writer.vtu
    input:
      mesh:  { from: build }
      field: { from: meltpool }
      path:  bracket-melt-pool.vtu
```

Run it:

```bash
souxmar run examples/am-lpbf-bracket/pipeline.yaml \
    --plugin-path build/dev/examples/plugins
```

The distortion leg hangs off the same mesh (`solver.am.distortion.inherent_strain` → `postproc.am.residual_stress`), and `solver.am.overhang` needs nothing but the mesh. Colour the melt-pool field by component `[2]` for porosity risk; warp the distortion field by vector with a large scale factor, because the raw magnitudes are tens of microns.

### Polymer: FFF thermal, bonding, and a toolpath

```yaml
stages:
  - id: part
    plugin: reader.obj
    input: { path: fairing.obj }

  - id: fff
    plugin: solver.am.polymer.fff
    input:
      mesh: { from: part }
      nozzle_temperature: 400.0     # PEKK
      bed_temperature: 140.0
      chamber_temperature: 90.0
      layer_time: 45.0
      layer_height: 3.0e-4
      road_width: 6.0e-4
      glass_transition_temperature: 162.0
      density: 1320.0
      specific_heat: 1900.0
      thermal_conductivity: 0.25
      convection_coefficient: 25.0

  - id: bond
    plugin: postproc.am.bond_strength
    input:
      mesh:  { from: part }
      field: { from: fff }
      glass_transition_temperature: 162.0
      reptation_time_reference: 2.0
      reptation_reference_temperature: 260.0
      activation_energy: 8.0e4
      layer_time: 45.0              # keep equal to the solver's

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

Note that `writer.am.gcode` slices the mesh independently of the thermal stages — it consumes no field. Keep `layer_height` consistent across the three, or the bonding you analysed is not the bonding you print.

### DfAM triage on real geometry

```yaml
stages:
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
      corrosion_allowance: 0.001    # subsea part: add wall for 25 years

  - id: cost
    plugin: solver.am.buildtime
    input:
      mesh: { from: part }
      process: lpbf
      material_cost_per_kg: 75.0
      machine_rate_per_hour: 45.0

  - id: report
    plugin: writer.am.report
    input:
      mesh:  { from: part }
      field: { from: cost }
      path:  bracket-build-report.md
      title: "316L LPBF bracket — build report"
      process: lpbf
      material: "316L"
      material_cost_per_kg: 75.0
      machine_rate_per_hour: 45.0
```

Three orientations means three copies of this pipeline with different `build_direction` values — or one call to the `set_build_orientation` agent tool, which scores the six axis-aligned candidates plus anything you supply and stages the winner.

## Calibration

Three constants in this block are **calibration parameters, not material properties**. Shipping a number for them was unavoidable; believing it is not.

### `strain_calibration` (inherent-strain distortion)

Default `0.30`. It is the fraction of the free thermal contraction that survives as permanent, plastically accommodated strain instead of relaxing away. It is machine- *and* material- *and* parameter-set-specific, and it also has to be re-fitted whenever `layer_height` changes, because the model accumulates one curvature increment per simulation layer — halving the layer height roughly doubles the predicted bow.

How to calibrate:

1. Print the canonical distortion coupon on your machine with your parameter set. A twin-cantilever bridge is the standard geometry: build it, cut one leg free of the plate, measure the tip deflection.
2. Model the same coupon: `mesher.am.layered` (or a conforming mesh with cleared tags) at the layer height you will use for real work, the same `build_direction`, `baseplate_layers` and `baseplate_clamped` you will use for real work.
3. Run `solver.am.distortion.inherent_strain` with `inherent_strain: 0.0` and sweep `strain_calibration`. The response is close to linear, so two runs bracket the answer.
4. Record the fitted value **with the layer height, the machine, the alloy, the parameter set and the coupon geometry** next to it. A bare number is worthless.
5. Re-run your real part with the fitted value. Report distortion as a range, not a number.

If your calibration campaign reports an inherent strain directly (which is the usual output format), skip the derivation entirely and pass `inherent_strain` — a supplied value is used as given, negative meaning shrinkage.

### `absorptivity` (LPBF thermal, melt pool)

Default `0.35`, a flat-plate 316L value at 1070 nm. A powder bed with multiple scattering absorbs considerably more, and a keyhole more again — reported values reach 0.5–0.7. Because Rosenthal already under-predicts depth, an uncalibrated absorptivity compounds the error in the conservative direction (over-predicted lack-of-fusion risk).

How to calibrate: cross-section one single-track weld at known power and speed, measure the melt-pool depth, and tune `absorptivity` until `postproc.am.melt_pool` component `[0]` matches. One track is enough to move this model from "wrong" to "usefully wrong"; a 3 × 3 power/speed matrix is enough to check that the trend is right too.

### The healing-law constants (`reptation_time_reference`, `reptation_reference_temperature`, `activation_energy`)

Defaults `2.0` s at `260` °C with `8.0e4` J/mol. These three define `t_rep(T)`, and `D_h` goes as `xi^0.25`, so the model is a fourth-root — forgiving on the absolute value, unforgiving on the temperature dependence, which is where `activation_energy` lives.

How to calibrate:

1. Print Z-oriented tensile coupons at a small matrix of `layer_time` values (and, if you can, chamber temperatures) on your machine with your polymer.
2. Test them. Normalise the Z strength by the XY strength of the same build. That ratio is what `z_strength_fraction` is trying to predict.
3. Run `solver.am.polymer.fff` → `postproc.am.bond_strength` for each point in the matrix with the printed conditions.
4. Fit `activation_energy` to the *slope* across the matrix first, then `reptation_time_reference` to the level, holding `reptation_reference_temperature` at your polymer's published reference if you have one.
5. Record the fit with the polymer grade, the moisture conditioning, the nozzle and chamber temperatures, and the raster pattern.

If you only ever compare two process windows for the same polymer on the same machine, the defaults are adequate: the ordering is much more robust than the level.

### Everything else with a number in it

`imperfection_knockdown` and `am_anisotropy_knockdown` in `solver.marine.hull_collapse`, the corrosion rates in `solver.marine.corrosion`, the 0.85 contact fraction in `z_strength_fraction`, the printability weights, the 8:1 slenderness limit, the 30/60 keyhole anchors, the 1→4 lack-of-fusion anchors — all of these are documented literature values or explicit interpolation choices. Each is stated in the plugin source header with its provenance. None is a design value.

## Plugging a real solver in behind the same capability id

The capability id is the contract; the model behind it is not. That is the entire point of dispatching by name.

A calibrated layer-wise thermomechanical AM simulation can replace the closed-form models **without changing a single pipeline file**, provided it keeps three promises:

1. **The same capability id.** Register `solver.am.thermal.lpbf` (or `solver.am.distortion.inherent_strain`) from the new plugin. Dispatch is a prefix match on `solver.` plus an exact-string registry lookup, so nothing in the host changes. Only one plugin may own an id in a given search path — drop the closed-form example plugin out of `--plugin-path`, or give the high-fidelity implementation a sibling id (`solver.am.thermal.lpbf.fe`) and name it explicitly in the pipeline.
2. **The same output field.** Same name, same location (`SOUXMAR_FL_NODAL` / `SOUXMAR_FL_CELL`), same kind, same component order, same units — °C for temperature, metres for displacement, Pa for stress. Every downstream consumer (`postproc.am.melt_pool`, `writer.am.report`, the desktop panels, `check_printability`) dispatches on the field name and indexes components positionally.
3. **The same input keys.** A high-fidelity solver will want *more* keys (mesh-conduction options, time-step controls, plasticity tables). Add them; unknown keys are ignored by the closed-form plugins, so the same YAML stays runnable against both. Do not repurpose an existing key to mean something else.

Two ways to do it, both already precedented in-tree:

- **In-process plugin.** A C ABI plugin that links a real FE kernel. This is how `examples/plugins/fenicsx-solver` and `examples/plugins/heat-solver` work.
- **Subprocess adapter.** The [ADR-0009](adr/0009-openfoam-process-isolation.md) pattern: write a deck, run a third-party binary as a child process, read the results back. This is how OpenFOAM works, how [RFC-0011](rfcs/0011-calculix-solver-adapter.md)'s CalculiX adapter works, and it is the path RFC-0012 proposes for calibrated AM thermomechanics — layer-wise transient thermal plus elasto-plastic mechanics through the CalculiX or FEniCSx adapters, behind these same ids.

What you cannot do without an ABI change is give a **mesher** a value bag: a fully-parametric build-volume mesher still has to arrive as a `reader.*`. RFC-0012 §Proposal carries the additive `souxmar_registry_add_mesher_ext` ratchet that would fix that, and explains why it was deliberately deferred out of this block.

## Other surfaces

- **Desktop.** `src/desktop/src/workbench/ManufacturingPanel.tsx` (and `MarinePanel.tsx`) sit with the Mesher / Solver / Materials / BC pickers above the YAML editor. They append whole stages — a solver plus the postproc that consumes it — into the open buffer; ⌘S still reaches disk. Every id, key, unit and default they write is the contract's.
- **Agent.** Six tools cover this block: `propose_am_setup`, `check_printability`, `set_build_orientation`, `estimate_build_cost`, `apply_hydrostatic_load`, `check_marine_integrity`. Their confirmation tiers and the ratchet that admitted them are [ADR-0045](adr/0045-agent-tool-contract-am-ratchet.md).
- **Skills.** `.claude/skills/simulating-additive-manufacturing/` is the operational walkthrough for the AM chain; `.claude/skills/assessing-marine-am-parts/` for the subsea one.

## References

- [ADR-0044](adr/0044-manufacturing-capability-namespaces.md) — why these capabilities live under the existing five namespaces.
- [ADR-0045](adr/0045-agent-tool-contract-am-ratchet.md) — the additive agent-tool ratchet for tools 19–24.
- [RFC-0012](rfcs/0012-am-process-simulation.md) — physics roadmap, validation plan, and the deferred mesher-value-bag ratchet.
- [`PLUGIN_SDK.md`](PLUGIN_SDK.md) — the C ABI these plugins are written against.
- [`MARINE.md`](MARINE.md) — the marine half of the block, and the advisory-only scope statement.
- `examples/plugins/am-*`, `examples/plugins/lattice-reader`, `examples/plugins/marine` — the implementations. Every source file opens with `What it computes:` and `What this is NOT:`; those headers are the authoritative statement of each model's limits.
- `examples/am-lpbf-bracket`, `examples/am-marine-propeller`, `examples/am-submarine-pressure-hull`, `examples/am-polymer-auv-fairing` — runnable examples.
- Literature the models implement: Rosenthal (1946) and Eagar–Tsai for the moving source; Hann et al. (2011) and King et al. (2014) for normalised enthalpy; Tang, Pistorius & Beuth (2017) for lack-of-fusion; Keller & Ploshikhin (2014) for inherent strain; Stoney (1909) and Timoshenko (1925) for the curvature relation; Yang & Pitchumani (2002) and Sun et al. (2008) for polymer bond formation; Deshpande, Fleck & Ashby (2001) for the octet truss.
