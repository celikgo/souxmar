# Marine and subsea

This document is the domain guide for souxmar's marine capability block: depth and pressure conventions, seawater properties, pressure-hull collapse modes, seawater corrosion and cathodic protection, the additive-manufacturing material shortlist for subsea service, and the exact scope of what these capabilities may be used to claim.

The additive-manufacturing half of the same block — process models, DfAM checks, slicing — lives in [`MANUFACTURING.md`](MANUFACTURING.md). The decision that put these capabilities under the existing plugin namespaces is [ADR-0044](adr/0044-manufacturing-capability-namespaces.md).

> ## Advisory only
>
> **souxmar is not a classification society, a notified body, or a certifying authority.** Nothing in this block approves anything, and running any of it qualifies nothing.
>
> The collapse pressures are **preliminary sizing arithmetic** from closed-form shell formulae with blanket knockdown factors. The corrosion numbers are **indicative literature values chosen so that alloys can be compared**, not design allowances. The qualification report is a **checklist derived from publicly documented AM qualification practice**, not a reproduction of anyone's rule and not a demonstration of compliance with it.
>
> A pressure hull, a propeller, or a pressure-boundary fitting is approved against a class rule or a naval standard, using the surveyor's own formulae, the surveyor's own tolerances, and measured weld, out-of-roundness and material data. See [What would make this class-credible](#what-would-make-this-class-credible) for the honest gap list.

## Depth, pressure and sign conventions

`build_direction` doubles as the **"up" axis** in the marine capabilities. It defaults to `[0, 0, 1]`, is normalised internally, and a zero-length vector is `SOUXMAR_E_INVALID_ARGUMENT`. Reusing the AM key is deliberate: a subsea AM part is analysed in the frame it was built in, and having two nearly-identical keys for the same axis would guarantee that one day they disagree.

`solver.marine.hydrostatic` treats the mesh as a rigid body lowered to each depth:

- The mesh's **top** — its maximum coordinate along `build_direction` — sits at `depth_top = design_depth × factor`.
- Every other node sits deeper by its own distance below that node.

```
p(i, j) = rho · g · (depth_top(j) + (z_top − z_i))   [+ p_atm]
z_i     = dot(x_i, b_hat)
```

**Depth is positive downward. Pressure is a positive magnitude, compressive on the wetted surface.** The field is a *gauge* pressure unless `include_atmospheric` is true, in which case `atmospheric_pressure` (default 101325 Pa) is added to give an absolute pressure. Use gauge for a structural check of a hull whose interior is at one atmosphere; use absolute when the consumer needs a true absolute pressure.

Two consequences that catch people:

1. **The solver does not know which faces are wetted.** Every node is loaded. A structural stage consuming this field must apply it as a follower load along the inward surface normal, and must decide for itself which surfaces are dry.
2. **A sign error here is silent.** The field carries magnitudes, so applying it outward instead of inward gives you a beautifully converged answer for a hull being inflated. Check the deformed shape.

Depth load cases arrive as **time steps**, one per entry of `depth_factors`. The default list `[1.0, 1.5, 2.25]` is the trio a subsea pressure boundary is normally checked at:

| Step | Factor | Meaning |
| --- | --- | --- |
| 0 | 1.00 | operating / maximum operating depth |
| 1 | 1.50 | test / proof depth (hydrostatic test) |
| 2 | 2.25 | collapse / crush depth |

Any list of 1 to 16 factors is accepted, and the step index is the list index — so the meaning of a step is whatever your list says it is, not what the table above says. Document your factors in the pipeline file; the field cannot carry that for you.

`solver.marine.hull_collapse` computes its own design pressure from `design_depth` independently, as gauge (`p_design = rho·g·design_depth`), so keep the two stages' `design_depth`, `seawater_density` and `gravity` consistent or the margin will not correspond to the pressure field you are looking at.

## Seawater properties

### Density

When `seawater_density` is left at `0.0`, `solver.marine.hydrostatic` derives it from the one-atmosphere **International Equation of State of Seawater (EOS-80)** — the Millero & Poisson (1981) polynomial as reproduced in UNESCO Technical Paper in Marine Science 44 (1983) — evaluated at `salinity_psu` and `seawater_temperature`.

| Property | Value |
| --- | --- |
| Validity band | 0 ≤ S ≤ 42 PSU, −2 ≤ T ≤ 40 °C, at 1 atm |
| Published standard error of the fit | 3.6e-3 kg/m³ |
| rho(35 PSU, 10 °C) | 1026.95 kg/m³ |

Inputs are clamped to the validity band, so a nonsense YAML value cannot produce a negative density.

The **one-atmosphere** qualifier matters. In-situ seawater is compressed by the column above it, which raises density by roughly 0.5 % per 1000 m. Ignoring it under-predicts the load: under 0.2 % at 300 m, around 1.5 % at 3000 m — unconservative, and worth adding back by hand as an explicit `seawater_density` if you are working deep.

`solver.marine.hull_collapse` has **no derive path**: its `seawater_density` defaults to a flat 1025 kg/m³. If you care about the third significant figure, set it explicitly in both stages.

### Reference environment

Every corrosion rate in the block is quoted in one reference environment, and every environmental factor is a multiplier relative to it:

| Parameter | Reference value |
| --- | --- |
| Salinity | 35 PSU (open ocean) |
| Temperature | 10 °C (temperate) |
| Dissolved oxygen | 8 mg/L (aerated) |
| Flow velocity | 0.5 m/s |
| Surface | machined (not as-built AM) |
| Galvanic couple | none |

Deviations are applied as documented factors: a temperature doubling every ~15 K (the rule of thumb for aqueous corrosion), a linear oxygen term (oxygen reduction is the cathodic rate-determining step in aerated seawater), a deliberately weak `sqrt` salinity term, a `v^0.35` mass-transfer term plus an erosion-corrosion multiplier above the alloy's critical velocity, and a flat penalty for an as-built AM surface.

## Pressure-hull collapse modes

`solver.marine.hull_collapse` evaluates the closed-form pressures for the three hull forms it supports, applies knockdowns, and reports the **governing (minimum)** mode. `diameter` is the shell **outer** diameter D; `unsupported_length` is the frame spacing L (centre-to-centre of ring frames, or the length between end closures for an unstiffened cylinder); R = D/2.

### Mode 0 — interframe shell instability (lobar collapse between frames)

The Windenburg & Trilling (1934) closed-form approximation to von Mises' solution, in the form given in Ross, *Pressure Vessels: External Pressure Technology*, and in Moss, *Pressure Vessel Design Manual*:

```
p_wt = 2.42·E·(t/D)^2.5 / [ (1 − nu²)^0.75 · ( L/D − 0.45·sqrt(t/D) ) ]
```

Valid for thin shells (t/D ≲ 0.05) with `L/D > 0.45·sqrt(t/D)`. The bracket is checked before the division and the mode is **dropped**, not clamped, when the geometry falls outside it — a dropped mode is an honest "this formula does not apply here", not a pass.

### Mode 1 — membrane yield

Thin-wall hoop stress `p·D/(2t)` reaches yield:

```
p_y = 2·sigma_y·t / D           (cylinder)
p_y = 4·sigma_y·t / D           (sphere — membrane stress is p·D/(4t))
```

Using the mean diameter `(D − t)` instead of the outer diameter would raise `p_y` by `t/(D − t)`, about 1.2 % at the default geometry — far inside the model's own error.

### Mode 2 — general (long-cylinder) elastic instability

The classical ring result with the plane-strain correction, evaluated for `hull_type: cylinder` only, i.e. a cylinder whose ring frames contribute nothing:

```
p_long = 2·E·t³ / [ (1 − nu²)·D³ ]
```

### Mode 3 — sphere elastic buckling

The classical Zoelly (1915) result:

```
p_cr = 2·E·(t/R)² / sqrt(3·(1 − nu²))
```

### Knockdowns and margin

Elastic instability modes (0, 2, 3) are multiplied by `imperfection_knockdown × am_anisotropy_knockdown`. The yield mode (1) is multiplied by `am_anisotropy_knockdown` alone — out-of-roundness knocks down buckling, not yield strength. The governing collapse pressure is the minimum over the applicable modes **after** knockdown, ties resolved to the lowest mode code. Then

```
p_design = seawater_density · gravity · design_depth        (gauge)
margin   = p_collapse / (p_design · safety_factor)
```

A margin below 1 means the hull does not pass its own stated factor.

Both knockdowns are blunt instruments and both are wrong in a direction you should know about:

- `imperfection_knockdown` defaults to **0.75**, a reasonable out-of-roundness allowance for a well-controlled cylinder. For a **sphere**, classical Zoelly badly overpredicts test data — measured hemisphere collapse commonly falls at 0.2–0.5 of classical (von Kármán & Tsien 1939; Krenzke & Kiernan 1963). Lower the input yourself; the plugin will not do it for you.
- `am_anisotropy_knockdown` defaults to **0.90**. Reported LPBF 316L property anisotropy between in-plane and build-direction loading is typically 5–15 %; 0.90 sits mid-band and must be replaced by your own witness-coupon data — which is precisely the evidence `writer.marine.qualification_report` asks for.

### Hand-check

Reproducible from the defaults (D = 1.0 m, t = 12 mm, L = 500 mm, E = 190 GPa, nu = 0.28, sigma_y = 500 MPa, ring-stiffened cylinder, 300 m, rho = 1025):

| Quantity | Unfactored | After knockdown |
| --- | --- | --- |
| Interframe (mode 0) | 17.11 MPa | 11.55 MPa (× 0.75 × 0.90) |
| Membrane yield (mode 1) | 12.00 MPa | 10.80 MPa (× 0.90) |
| **Governing** | | **10.80 MPa, mode 1** |
| Design pressure at 300 m | 3.016 MPa | |
| Margin against 1.5 × design | | **2.39** |

### What this mode set does not contain

- **General instability of a ring-stiffened cylinder is not evaluated.** It needs the frame area, the frame second moment of inertia and the bulkhead spacing (Bryant 1954 / Kendrick), and this capability's input contract supplies none of them. It is frequently the **governing** mode for a real framed hull and must be checked separately. A `hull_type: ring_stiffened_cylinder` result that reports mode 0 or 1 is not telling you the frames are adequate — it is telling you it never looked.
- Frame yielding, frame tripping, shell-frame interaction, end closures, penetrations and hatches, weld residual stress, AM build residual stress, creep, cyclic dive-cycle fatigue.
- The result is **uniform over the mesh**: every cell carries the same triple. It is an analytical answer keyed to the input geometry, not a mesh-resolved one. Refining the mesh will not change it, and no stress concentration anywhere in your part is visible to it.

## Corrosion, galvanic coupling and cathodic protection

`solver.marine.corrosion` reports three per-cell indicators for a part immersed for `service_life_years`. As with collapse, the inputs are part-level, so the answer is painted onto the mesh rather than resolved over it.

### Thickness loss

`[0] thickness_loss_mm` is the total **wall penetration** estimate over the service life — the number you compare against your corrosion allowance. It is the sum of three terms:

1. **Uniform** — the alloy's reference rate scaled by the temperature, oxygen, salinity, velocity and surface factors described above, times the years.
2. **Galvanic** — assumed **oxygen-limited**, which is the normal case in aerated seawater: the cathode can only consume electrons as fast as oxygen reaches it. The anodic current density is the limiting cathodic current density scaled by the oxygen and velocity factors, the cathode/anode area ratio, and a driving-force term capped at 1; thickness loss follows through the alloy's Faraday coefficient (1.16 mm/a per A/m² for iron — the textbook value). **Only the anodic member loses metal**, so this term is zero when your part is the noble half of the couple. The limiting current density used is the initial design current density for temperate open seawater from cathodic-protection design practice (DNV-RP-B401, cited by name); the 0.25 V driving-force cap is the unlike-metal coupling limit of MIL-STD-889C for general marine service (0.15 V for critical applications).
3. **Pitting** — power-law pit growth, `depth = k_pit · sqrt(years)`, weighted by `pitting_risk²`. The square-root-of-time form is the one used throughout the marine immersion literature (Melchers). The `risk²` weight is an explicit tuning choice with no derivation: it suppresses the term when initiation is unlikely.

A coating scales the uniform and galvanic terms by `(1 − coating_efficiency)`. `cathodic_protection: true` scales them by a small residual factor rather than to zero — real CP has imperfect potential distribution and shielded geometry, and a report claiming exactly zero corrosion under CP would be a lie.

### Pitting risk and PREN

`[1] pitting_risk` is a scaled indicator in `0…1`, **not a probability**. For the stainless and Ni-Cr-Mo alloys it is a logistic in `(T − T_crit)` with an 8 K width, where

```
PREN = %Cr + 3.3·%Mo + 16·%N
CPT  = 2.24·PREN − 47.1        [°C]
```

PREN is the pitting-resistance equivalent number in its PREN-16N form, the variant used throughout the duplex stainless literature. The CPT-versus-PREN regression is the one fitted to ASTM G48 (6 % FeCl₃) critical-pitting-temperature data and reproduced throughout that same literature. The indicator reads exactly 0.5 at the critical temperature.

**Stagnant water lowers the effective critical temperature toward the critical *crevice* temperature** — deposits and biofilm make crevices — by up to 20 K, faded in linearly below 0.3 m/s. Titanium is treated as immune below the ~80 °C seawater limit. The copper alloys and aluminium get a flat base risk, because PREN does not order them at all: quoting a PREN for a bronze is meaningless and the plugin refuses to.

### Why 316L is marginal in warm stagnant seawater and 2507 is not

This is the single most useful thing the corrosion capability tells you, so it is worth spelling out with the block's own numbers:

| Alloy | %Cr | %Mo | %N | PREN | CPT from the correlation |
| --- | --- | --- | --- | --- | --- |
| 316L | 16.9 | 2.4 | 0.045 | **≈ 25.5** | **≈ 10 °C** |
| 2507 super-duplex | 25.0 | 3.8 | 0.27 | **≈ 41.9** | **≈ 47 °C** |
| Alloy 625 | 21.5 | 9.0 | 0 | ≈ 51.2 | ≈ 68 °C |

316L's critical pitting temperature lands *inside the range of ordinary seawater*. Temperate open water at 10 °C sits right at the indicator's 0.5 point; a warm site at 25–30 °C is well above it. Then subtract up to 20 K again for stagnant conditions, and 316L in warm, still, aerated seawater is operating below its critical crevice temperature — which is exactly the condition under which 316L pits and crevice-corrodes in service, and exactly why 316L subsea hardware is normally either cathodically protected, coated, kept in flowing water, or accepted with an inspection regime.

2507's PREN puts its critical temperature roughly 35 K higher, above any natural seawater temperature even after the stagnation penalty. That, not its uniform rate, is why it is the default choice for permanently wetted, unprotected stainless hardware. (Its uniform rate is lower too — 0.001 versus 0.003 mm/year in the reference environment — but uniform corrosion is never what kills a stainless part in seawater.)

Two caveats that must travel with this comparison:

- The correlation is a regression on laboratory `G48` data. Real crevice behaviour in seawater is dominated by microbiology, deposits, weld metallurgy and crevice geometry, none of which are inputs here.
- A stainless steel that **goes active** — loses its passive film — moves roughly 0.4 V negative in the galvanic series and changes the sign of the couple it is in. The model has no activation transition. If you are designing a couple where that could happen, the model is not describing your part.

### Galvanic series

`[2] galvanic_risk` folds the driving force and the square root of the cathode/anode area ratio through `x/(1+x)`, so it reads 0.5 for a 0.25 V couple at equal areas and saturates for a small anode wired to a large cathode. When the part is the noble member the number drops to a small residual — the mating part is the one dissolving.

The static series shipped in `examples/plugins/marine/marine_data.hpp` is for **flowing seawater** (2.4–4.0 m/s, 10–27 °C), volts versus a saturated Ag/AgCl reference, sorted most-active first. The values are the mid-points of the published bands; the source is LaQue, *Marine Corrosion: Causes and Prevention* (Wiley, 1975), the galvanic-series chart reproduced in ASM Handbook Vol. 13A and in MIL-STD-889C Table I.

| Entry | Band (V vs Ag/AgCl) |
| --- | --- |
| magnesium alloy (sacrificial anode) | −1.67 … −1.60 |
| Al-Zn-In sacrificial anode | −1.10 … −1.05 |
| zinc (sacrificial anode) | −1.05 … −0.98 |
| aluminium alloy (Al-Si cast / 5xxx) | −0.85 … −0.75 |
| carbon / low-alloy steel, cast iron | −0.71 … −0.60 |
| copper | −0.36 … −0.31 |
| nickel-aluminium bronze (C95800) | −0.35 … −0.25 |
| 90-10 copper-nickel (C70600) | −0.28 … −0.20 |
| 70-30 copper-nickel (C71500) | −0.25 … −0.20 |
| nickel 200 (passive) | −0.20 … −0.10 |
| Ni-Cu alloy 400 (passive) | −0.14 … −0.04 |
| 316L austenitic stainless (passive) | −0.10 … −0.05 |
| 2507 super-duplex stainless (passive) | −0.08 … −0.03 |
| Ni-Cr-Mo alloy 625 (passive) | −0.10 … +0.05 |
| titanium grade 5 (passive) | −0.05 … +0.05 |
| graphite / carbon-fibre composite | +0.20 … +0.30 |
| platinum | +0.25 … +0.35 |

Read it the way the series is meant to be read: adjacency is safe, distance is not, and **area ratio dominates**. A small stainless fastener in a large bronze plate is a nuisance; a small bronze fitting in a large stainless or CFRP structure is a wear part. Note where **carbon-fibre composite** sits — noble, above every metal in the table. A metal fitting bolted into a CFRP AUV structure is the anode, and that couple has sunk more subsea hardware than any exotic alloy question.

### Cathodic protection

The block does not size an anode. What it gives you is:

- The **galvanic term** at your stated couple and area ratio, which is the demand side of a CP calculation.
- The effect of `cathodic_protection: true` on the uniform and galvanic terms — a residual, not zero.
- A reminder in the qualification report that a CP design is its own calculation (current demand, anode mass, current distribution, coating breakdown factors over life) done to a recognised recommended practice, of which DNV-RP-B401 is the commonly cited one.

Two failure modes the model cannot see and you must: **hydrogen embrittlement of the cathodic member** (it matters when titanium or a high-strength steel is the noble half of a couple, and it is a genuine subsea failure mode), and **CP shielding** by coatings, crevices, or the inside of a lattice — where the current does not reach and the protection is notional.

Also absent, deliberately and completely: microbiologically-influenced corrosion, sulfide-polluted seawater, chlorination, weld metallurgy and sensitisation, stress-corrosion cracking, corrosion-fatigue interaction, anode consumption over life.

## Marine AM material shortlist

The curated library is `examples/materials/am-marine.toml`, with a source column for every number, mirrored as a static table inside the `propose_am_setup` agent tool and the desktop panels. The seven alloys the corrosion capability knows about, and what each is actually for:

| Material | Typical AM route | What it is for subsea | Watch out for |
| --- | --- | --- | --- |
| **NAB** — nickel-aluminium bronze (C95800) | WAAM / DED, LPBF | **Propellers, impellers, pump and valve bodies, tidal-turbine blades.** The classical marine propeller alloy: good seawater erosion resistance, biofouling resistance, and a long cast-and-repair tradition that makes wire-arc deposition an easy sell. | Erosion-corrosion above its critical velocity (≈ 4.3 m/s in the table); selective-phase attack; the highest uniform rate of the passive/near-passive entries (0.030 mm/year at reference). It is anodic to stainless — small NAB fittings in large stainless assemblies dissolve. |
| **316L** austenitic stainless | LPBF, DED | **General subsea hardware**: brackets, housings, manifold bodies, non-critical structural parts. The most-characterised AM alloy in existence, which is why it is the block's default everywhere. | Pitting and crevice corrosion — CPT ≈ 10 °C, see above. Marginal for permanent unprotected immersion in warm or stagnant water. Fine with CP, coating, flow, or an inspection regime. |
| **2507** super-duplex stainless | LPBF | **Permanently wetted, unprotected subsea hardware**: pressure-boundary fittings, seawater-system components, high-strength brackets. PREN ≈ 42 buys real margin over natural seawater temperatures. | AM duplex needs its **phase balance** demonstrated — as-built austenite/ferrite ratio and nitrogen retention drive both strength and corrosion, and the post-build solution anneal is not optional. Do not assume wrought 2507 property data applies to your build. |
| **Ti-6Al-4V** | LPBF, EB-PBF | **Deep hulls and deep pressure housings**, seawater-loop components where mass matters. Effectively immune to seawater pitting below ~80 °C, best strength-to-weight in the list. | It is the noble end of most couples you will build — everything attached to it corrodes, and hydrogen embrittlement of the *cathode* is a real subsea mode. Anisotropy and build-direction properties must be measured, not assumed. Cost and machine availability. |
| **CuNi 90-10** (C70600) | DED / WAAM, LPBF | **Seawater piping and heat-exchanger components**, spools and fittings. Biofouling resistance is the reason it stays in service. | Critical velocity ≈ 3.5 m/s — it is a *flow-limited* alloy, and AM surface roughness makes local velocity worse than your nominal number. Higher uniform rate (0.025 mm/year at reference); needs a corrosion allowance, not zero. |
| **Alloy 625** (IN625) | LPBF, DED | High-integrity subsea and hot-service components where 2507 is not enough. Lowest uniform rate and highest PREN in the table. | Cost, and the temptation to use it as a default. It is noble; it makes everything bolted to it anodic. |
| **AlSi10Mg** | LPBF | Topside and dry-internal parts, tooling, non-immersed AUV internals. | **Not for permanent immersion.** Highest uniform rate in the table (0.020 mm/year at reference), chloride pitting is essentially unavoidable, and it is anodic to everything except the sacrificial anodes. If it is wet, it is the anode. |

On the polymer side, `solver.am.polymer.fff` + `postproc.am.bond_strength` cover the two engineering polymers that actually get used in the water:

| Material | What it is for | Watch out for |
| --- | --- | --- |
| **PEKK** (and PEEK) | **AUV fairings, flooded-volume structures, non-pressure housings, connector bodies.** High-temperature semi-crystalline thermoplastic: no galvanic behaviour at all, near-zero water uptake, survives the autoclave cycles a wet-layup repair needs. | Z-direction strength is the design driver, which is what the bonding capability is for. Crystallinity depends on the thermal history the model only approximates. Large-format beads leave the lumped-capacitance regime the model assumes. |
| **PA12-CF** — carbon-filled polyamide 12 | **AUV fairings and internal structure** where stiffness per unit mass matters and temperature does not. | Moisture uptake changes the properties; the carbon fill is **noble** in the galvanic series, so a CF part in contact with a metal fitting makes the metal the anode. Anisotropy is large and Z-strength is the limit. |

Polymer parts do not get a collapse or a corrosion number from this block — `solver.marine.hull_collapse` is a metal-shell model and `solver.marine.corrosion` is an electrochemical model for the seven alloys above. A flooded polymer fairing does not need either; a *pressure-bearing* polymer housing needs a creep and water-uptake assessment souxmar does not have.

## Capability reference

### `solver.marine.hydrostatic`

| Key | Unit | Default |
| --- | --- | --- |
| `design_depth` | m | `300` |
| `depth_factors` | — (list) | `[1.0, 1.5, 2.25]` — 1…16 entries |
| `seawater_density` | kg/m³ | `0.0` ⇒ derive from salinity + temperature |
| `salinity_psu` | PSU | `35.0` |
| `seawater_temperature` | °C | `10.0` |
| `gravity` | m/s² | `9.80665` |
| `include_atmospheric` | bool | `false` |
| `atmospheric_pressure` | Pa | `101325` |
| `build_direction` | — | `[0,0,1]` (the "up" axis) |

Output: `souxmar_field_new("hydrostatic_pressure", SOUXMAR_FL_NODAL, SOUXMAR_FK_SCALAR, num_nodes, num_factors)` in Pa, one step per factor.

### `solver.marine.hull_collapse`

| Key | Unit | Default | | Key | Unit | Default |
| --- | --- | --- | --- | --- | --- | --- |
| `hull_type` | — | `ring_stiffened_cylinder` | | `design_depth` | m | `300` |
| `diameter` | m | `1.0` (outer) | | `seawater_density` | kg/m³ | `1025` |
| `thickness` | m | `0.012` | | `gravity` | m/s² | `9.80665` |
| `unsupported_length` | m | `0.5` | | `imperfection_knockdown` | — | `0.75` |
| `youngs_modulus` | Pa | `1.9e11` | | `am_anisotropy_knockdown` | — | `0.90` |
| `poisson_ratio` | — | `0.28` | | `safety_factor` | — | `1.5` |
| `yield_strength` | Pa | `5.0e8` | | | | |

`hull_type` is one of `cylinder` \| `ring_stiffened_cylinder` \| `sphere`; anything else is `SOUXMAR_E_INVALID_ARGUMENT`, as is `t/D > 0.25` (outside any thin-shell formula in the set).

Output: `souxmar_field_new("collapse_margin", SOUXMAR_FL_CELL, SOUXMAR_FK_VECTOR, num_cells, 1)` — the same triple on every cell, documented as analytical rather than mesh-resolved.

| Component | Name | Notes |
| --- | --- | --- |
| `[0]` | `collapse_pressure_Pa` | governing, after knockdown |
| `[1]` | `margin` | collapse ÷ (design pressure × safety factor) |
| `[2]` | `governing_mode_code` | `0` interframe elastic instability, `1` membrane yield, `2` general instability, `3` sphere elastic buckling |

### `solver.marine.corrosion`

| Key | Unit | Default |
| --- | --- | --- |
| `alloy` | — | `316L`; also `2507`, `NAB`, `Ti6Al4V`, `IN625`, `AlSi10Mg`, `CuNi90-10` (aliases accepted). An unrecognised name falls back to the explicit composition inputs. |
| `chromium_pct`, `molybdenum_pct`, `nitrogen_pct` | % | from the alloy table |
| `mating_alloy` | — | `""` ⇒ no couple. Must resolve in the galvanic series, else `SOUXMAR_E_INVALID_ARGUMENT`. |
| `area_ratio_cathode_anode` | — | `1.0` |
| `seawater_temperature` | °C | `10` |
| `salinity_psu` | PSU | `35` |
| `flow_velocity` | m/s | `0.5` |
| `oxygen_mg_per_l` | mg/L | `8.0` |
| `service_life_years` | a | `25` |
| `cathodic_protection` | bool | `false` |
| `coating_efficiency` | — | `0.0` (0…1) |
| `as_built_surface` | bool | `true` |

Output: `souxmar_field_new("corrosion", SOUXMAR_FL_CELL, SOUXMAR_FK_VECTOR, num_cells, 1)` with `[0] thickness_loss_mm` over the service life, `[1] pitting_risk` (0…1), `[2] galvanic_risk` (0…1).

Note `as_built_surface` defaults to **true** and carries a penalty. The AM corrosion literature is genuinely split — fully-dense LPBF 316L often out-performs wrought 316L thanks to its fine cellular structure and absence of MnS inclusions, while as-built porosity, lack-of-fusion voids and partially-melted powder create exactly the crevices that start pits. The model takes the pessimistic side and says so. Set it to `false` when your part is machined or polished on every wetted surface, and record why.

### `writer.marine.qualification_report`

| Key | Unit | Default |
| --- | --- | --- |
| `path` | — | **required** |
| `part_name` | — | `"AM part"` |
| `process` | — | `lpbf` |
| `alloy` | — | `316L` |
| `application` | — | `structural`; one of `hull` \| `propulsion` \| `piping` \| `structural` \| `non_structural` |
| `criticality` | — | `2`, integer 1…3 — **1 is the highest consequence of failure**, 3 the lowest. Stated explicitly because class frameworks number their categories in opposite directions; map to yours yourself. |
| `design_depth` | m | `300` |
| `service_life_years` | a | `25` |
| `class_framework` | — | `"generic"` — echoed as declared, never verified |
| `redundancy` | bool | `false` |

It renders a Markdown dossier: part identification, the criticality-class rationale, the qualification-evidence checklist (feedstock traceability, machine qualification, witness coupons, NDT method and coverage, mechanical test matrix, build-direction property declaration, post-processing / HIP record), a summary of whichever simulation field it was handed, and the advisory-only disclaimer. No wall-clock, no absolute paths, no environment values.

**No certificate number, stamp, surveyor name, approval reference or test result is generated.** Every such field is left blank for the responsible engineer, and the generator refuses to invent them. The traceability digest is FNV-1a 64-bit — a non-cryptographic content hash that detects an accidentally different mesh, not a signature and not tamper evidence.

## Worked pipeline

`examples/am-submarine-pressure-hull` runs the hull chain against an OBJ hull ring plus a `reader.lattice` buoyancy core. The shape of it:

```yaml
version: 1
stages:
  - id: ring
    plugin: reader.obj
    input: { path: hull-ring.obj }

  - id: pressure
    plugin: solver.marine.hydrostatic
    input:
      mesh: { from: ring }
      design_depth: 300.0
      depth_factors: [1.0, 1.5, 2.25]   # operating / test / collapse
      seawater_density: 0.0             # 0 => EOS-80 from S and T
      salinity_psu: 35.0
      seawater_temperature: 10.0
      build_direction: [0.0, 0.0, 1.0]

  - id: collapse
    plugin: solver.marine.hull_collapse
    input:
      mesh: { from: ring }
      hull_type: ring_stiffened_cylinder
      diameter: 1.0
      thickness: 0.012
      unsupported_length: 0.5
      youngs_modulus: 1.9e11
      poisson_ratio: 0.28
      yield_strength: 5.0e8
      design_depth: 300.0               # keep consistent with `pressure`
      seawater_density: 1025.0          # no derive path here
      imperfection_knockdown: 0.75
      am_anisotropy_knockdown: 0.90
      safety_factor: 1.5

  - id: corrosion
    plugin: solver.marine.corrosion
    input:
      mesh: { from: ring }
      alloy: "316L"
      mating_alloy: "Ti6Al4V"           # noble end-cap: the ring is the anode
      area_ratio_cathode_anode: 4.0
      seawater_temperature: 10.0
      flow_velocity: 0.1                # near-stagnant inside the bay
      service_life_years: 25
      cathodic_protection: true
      as_built_surface: true

  - id: dossier
    plugin: writer.marine.qualification_report
    input:
      mesh:  { from: ring }
      field: { from: collapse }
      path:  hull-ring-qualification.md
      part_name: "Pressure-hull ring frame"
      process: lpbf
      alloy: "316L"
      application: hull
      criticality: 1
      design_depth: 300.0
      service_life_years: 25
      class_framework: "generic"
```

```bash
souxmar run examples/am-submarine-pressure-hull/pipeline.yaml \
    --plugin-path build/dev/examples/plugins
```

A writer takes one field, so getting the corrosion summary into a dossier as well means a second `writer.marine.qualification_report` stage pointed at `corrosion`.

## What would make this class-credible

The gap between what this block does and what a classification society would accept, listed honestly so nobody has to discover it in a survey. None of this is on the roadmap as a promise; it is the shape of the work if the demand appears.

**Structural analysis**

1. **General instability of the framed hull.** Frame area, frame inertia, bulkhead spacing, and a Bryant/Kendrick-class evaluation — the mode that most often governs and that this block never looks at.
2. **A real shell FE solve** with measured out-of-roundness imported as an imperfection field, replacing the blanket 0.75 knockdown. Nonlinear (geometrically nonlinear, elasto-plastic) collapse analysis is the accepted route; [RFC-0011](rfcs/0011-calculix-solver-adapter.md)'s CalculiX adapter is the plausible engine.
3. **Penetrations, end closures and hatches** — the discontinuities where hulls actually fail.
4. **Dive-cycle fatigue** against measured AM S-N data in the build orientation, including the surface condition. As-built AM surfaces are a fatigue problem, not a corrosion footnote.
5. **A pressure-test correlation.** One instrumented proof test on one AM ring, with strain-gauge data compared against the prediction, would be worth more than every formula above.

**Materials and process**

6. **Witness-coupon-driven properties** replacing every literature default: build-direction-resolved yield, UTS, elongation, fracture toughness, plus phase balance for duplex and porosity by CT.
7. **A calibrated `strain_calibration` and `absorptivity`** per machine and parameter set (see [`MANUFACTURING.md`](MANUFACTURING.md) § Calibration). Residual stress and distortion feed the fit-up and the fatigue story; uncalibrated they are relative numbers.
8. **A real corrosion assessment**: exposure data or accelerated testing on *your* surface condition, crevice geometry included, plus a CP design done to a recognised recommended practice with coating-breakdown factors over life.
9. **Post-processing evidence**: HIP or solution-anneal records, surface finish on wetted faces, NDT method and coverage with a stated probability of detection.

**Process and documentation**

10. **Machine and operator qualification**, feedstock traceability with powder-lifecycle control, in-process monitoring records — the parts of AM qualification that publicly documented practice such as DNV-ST-B203, the ABS additive-manufacturing guidance, and ISO/ASTM 52920/52930 spend most of their pages on, and that no simulation can substitute for.
11. **An independent review path**: someone who did not run the analysis checking it, against a written acceptance criterion agreed before the run.
12. **Configuration control of the analysis itself.** souxmar gives you a deterministic, content-addressed pipeline and an FNV-1a mesh digest; a class file needs signed revisions, a reviewer, and a change log. The digest is a starting point, not the artefact.

## References

- [`MANUFACTURING.md`](MANUFACTURING.md) — the AM half of the block, the fidelity table, and the calibration procedures.
- [ADR-0044](adr/0044-manufacturing-capability-namespaces.md) — capability-namespace decision.
- [ADR-0045](adr/0045-agent-tool-contract-am-ratchet.md) — the `apply_hydrostatic_load` / `check_marine_integrity` agent tools.
- [RFC-0012](rfcs/0012-am-process-simulation.md) — physics roadmap and validation plan.
- `examples/plugins/marine/` — `marine_loads.cpp` (hydrostatic, collapse), `marine_integrity.cpp` (corrosion, dossier), `marine_data.hpp` (EOS-80, galvanic series, alloy table, every source).
- `examples/am-submarine-pressure-hull`, `examples/am-marine-propeller`, `examples/am-polymer-auv-fairing` — runnable examples.
- `.claude/skills/assessing-marine-am-parts/` — the operational walkthrough.
- Literature and practice cited by name: Millero & Poisson (1981) / UNESCO 44 (1983) for the equation of state; Windenburg & Trilling (1934), Ross and Moss for interframe collapse; Zoelly (1915), von Kármán & Tsien (1939), Krenzke & Kiernan (1963) for spherical buckling; Bryant (1954) and Kendrick for general instability; LaQue (1975), ASM Handbook Vol. 13A/13C and MIL-STD-889C for the galvanic series; Melchers for pitting kinetics; Efird (1977) for copper-alloy critical velocities; DNV-RP-B401 for cathodic-protection current densities; DNV-ST-B203, ABS additive-manufacturing guidance and ISO/ASTM 52920/52930 for AM qualification practice. souxmar reproduces none of these documents and demonstrates compliance with none of them.
