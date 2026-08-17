# Marine and subsea

Four capabilities for additively-manufactured subsea parts:
hydrostatic load cases, pressure-hull collapse margin, seawater
corrosion, and an advisory qualification dossier.

::: warning ADVISORY ONLY
**souxmar is not a classification society, a notified body, or a
certifying authority.** Nothing here approves anything, and running
it qualifies nothing.

The collapse pressures are preliminary sizing arithmetic from
closed-form shell formulae with blanket knockdown factors. The
corrosion numbers are indicative literature values chosen so alloys
can be compared — not design allowances. The qualification report is
a checklist derived from publicly documented AM qualification
practice, not a reproduction of anyone's rule and not a
demonstration of compliance with it.

A hull, a propeller, or a pressure-boundary fitting is approved
against a class rule or a naval standard, with the surveyor's own
formulae and measured weld, out-of-roundness and material data.
:::

## The capabilities

| Capability id | Kind | Gives you |
| ------------- | ---- | --------- |
| `solver.marine.hydrostatic` | solver | Nodal `hydrostatic_pressure` (Pa), one step per depth factor |
| `solver.marine.hull_collapse` | solver | Per-cell `[collapse_pressure_Pa, margin, governing_mode_code]` |
| `solver.marine.corrosion` | solver | Per-cell `[thickness_loss_mm, pitting_risk, galvanic_risk]` |
| `writer.marine.qualification_report` | writer | Markdown advisory dossier |

## Depth and sign conventions

`build_direction` is the **"up" axis** (default `[0, 0, 1]`) — the
same key the AM capabilities use, because a subsea AM part is
analysed in the frame it was built in.

The mesh's **top** sits at `design_depth × factor`; every other node
sits deeper by its own distance below that node:

```
p = rho · g · (depth_of_top + (z_top − z_node))  [+ p_atm]
```

**Depth is positive downward. Pressure is a positive magnitude,
compressive on the wetted surface** — gauge unless
`include_atmospheric` is set. The solver does not know which faces
are wetted: every node is loaded, and the consumer applies it as a
follower load along the inward normal. A sign error here is silent,
so check the deformed shape.

Load cases arrive as field time steps, one per `depth_factors`
entry. The default `[1.0, 1.5, 2.25]` is operating / test /
collapse.

## Hull collapse

```yaml
  - id: collapse
    plugin: solver.marine.hull_collapse
    input:
      mesh: { from: ring }
      hull_type: ring_stiffened_cylinder   # or cylinder | sphere
      diameter: 1.0            # OUTER diameter, m
      thickness: 0.012
      unsupported_length: 0.5  # frame spacing
      youngs_modulus: 1.9e11
      poisson_ratio: 0.28
      yield_strength: 5.0e8
      design_depth: 300.0
      seawater_density: 1025.0
      imperfection_knockdown: 0.75
      am_anisotropy_knockdown: 0.90
      safety_factor: 1.5
```

Four modes are evaluated and the **minimum after knockdown** governs:

| Code | Mode | Closed form |
| ---- | ---- | ----------- |
| 0 | Interframe shell instability | Windenburg–Trilling `p = 2.42·E·(t/D)^2.5 / [(1−ν²)^0.75·(L/D − 0.45·√(t/D))]` |
| 1 | Membrane yield | `p = 2·σy·t/D` (cylinder), `4·σy·t/D` (sphere) |
| 2 | General (long-cylinder) instability | `p = 2·E·t³ / [(1−ν²)·D³]` — unstiffened cylinder only |
| 3 | Sphere elastic buckling | `p = 2·E·(t/R)² / √(3(1−ν²))` |

Then `margin = p_collapse / (p_design · safety_factor)`, with
`p_design = rho·g·design_depth` as gauge.

**General instability of a ring-stiffened hull is not evaluated.** It
needs frame area, frame inertia and bulkhead spacing, which this
capability's inputs do not carry — and it is frequently the governing
mode for a real framed hull. A `ring_stiffened_cylinder` result that
reports mode 0 or 1 is not telling you the frames are adequate; it
never looked.

The result is uniform over the mesh: an analytical answer keyed to
the input geometry, not a mesh-resolved one. Refining the mesh will
not change it.

## Corrosion, PREN and galvanic risk

```yaml
  - id: corrosion
    plugin: solver.marine.corrosion
    input:
      mesh: { from: ring }
      alloy: "316L"            # 2507 | NAB | Ti6Al4V | IN625 | AlSi10Mg | CuNi90-10
      mating_alloy: "Ti6Al4V"  # noble mate: the part is the anode
      area_ratio_cathode_anode: 4.0
      seawater_temperature: 10.0
      flow_velocity: 0.1       # near-stagnant
      service_life_years: 25
      cathodic_protection: true
      as_built_surface: true
```

`thickness_loss_mm` sums a uniform term (rate scaled by
temperature, oxygen, salinity, velocity and surface condition), an
**oxygen-limited** galvanic term (only the anodic member loses
metal), and a `sqrt(time)` pitting term weighted by the pitting
risk. A coating scales the first two by `1 − coating_efficiency`;
cathodic protection scales them to a small residual, never zero.

Pitting risk for the stainless and Ni-Cr-Mo alloys keys off

```
PREN = %Cr + 3.3·%Mo + 16·%N
CPT  = 2.24·PREN − 47.1   [°C]
```

which is why **316L is marginal and 2507 is not**:

| Alloy | PREN | Critical pitting temperature |
| ----- | ---- | ---------------------------- |
| 316L | ≈ 25.5 | ≈ 10 °C |
| 2507 super-duplex | ≈ 41.9 | ≈ 47 °C |

316L's critical temperature lands *inside* the range of ordinary
seawater — and stagnant water drops the effective threshold by up to
20 K more, because deposits and biofilm make crevices. That is why
316L subsea hardware is normally cathodically protected, coated,
kept in flowing water, or accepted with an inspection regime. 2507
sits about 35 K clear of any natural seawater temperature, which is
why it is the default for permanently wetted unprotected stainless
hardware.

Galvanic risk scales with the driving force and the square root of
the cathode/anode area ratio. **Area ratio dominates**: a small
stainless fastener in a large bronze plate is a nuisance, a small
bronze fitting in a large stainless structure is a wear part. Note
that carbon-fibre composite is *noble* to every metal in the series
— a metal fitting bolted into a CFRP AUV structure is the anode.

## Material shortlist

| Material | Use it for | Watch out for |
| -------- | ---------- | ------------- |
| **NAB** (nickel-aluminium bronze) | Propellers, impellers, pump/valve bodies, tidal blades | Erosion-corrosion above ~4.3 m/s; anodic to stainless |
| **316L** | General subsea hardware, brackets, housings | Pitting/crevice — marginal unprotected in warm or stagnant water |
| **2507** | Permanently wetted unprotected hardware, pressure-boundary fittings | AM phase balance and nitrogen retention must be demonstrated |
| **Ti-6Al-4V** | Deep hulls and housings, mass-critical parts | Noble — everything attached corrodes; hydrogen embrittlement of the cathode |
| **CuNi 90-10** | Seawater piping, heat-exchanger parts, spools | Flow-limited (~3.5 m/s); needs a corrosion allowance |
| **Alloy 625** | High-integrity and hot-service subsea parts | Cost; noble to almost everything |
| **AlSi10Mg** | Topside and dry internals only | **Not for permanent immersion** |
| **PEKK / PA12-CF** | AUV fairings, flooded structures, connector bodies | Z-strength is the design driver; CF fill is noble to metals |

## Qualification dossier

```yaml
  - id: dossier
    plugin: writer.marine.qualification_report
    input:
      mesh:  { from: ring }
      field: { from: collapse }
      path:  hull-ring-qualification.md
      part_name: "Pressure-hull ring frame"
      process: lpbf
      alloy: "316L"
      application: hull        # | propulsion | piping | structural | non_structural
      criticality: 1           # 1 = HIGHEST consequence of failure
      design_depth: 300.0
      service_life_years: 25
```

It renders part identification, a criticality rationale, the
evidence checklist (feedstock traceability, machine qualification,
witness coupons, NDT method and coverage, mechanical test matrix,
build-direction property declaration, post-processing / HIP record),
a summary of whichever field it was handed, and the advisory-only
disclaimer.

**No certificate number, stamp, surveyor name, approval reference or
test result is generated** — every such field is left blank for the
responsible engineer. The traceability digest is FNV-1a 64-bit: a
non-cryptographic content hash, not a signature.

## From chat

```
apply_hydrostatic_load    stage a depth-derived pressure load case  (prompts once)
check_marine_integrity    corrosion / galvanic / collapse-margin summary
```

## Next

- [Additive manufacturing](/manufacturing/additive)
- [`docs/MARINE.md`](https://github.com/souxmar/souxmar/blob/master/docs/MARINE.md)
  — full conventions, seawater properties, every input key, and
  **"What would make this class-credible"**: the honest gap list
  between this block and a class submission.
