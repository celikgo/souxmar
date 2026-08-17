---
name: assessing-marine-am-parts
description: Use when assessing an additively-manufactured part for marine, subsea or submarine service — hydrostatic pressure at depth, pressure-hull collapse margin, seawater corrosion and galvanic compatibility, cathodic protection, alloy selection, and the qualification-evidence dossier. Triggers on "marine", "subsea", "submarine", "naval", "hull", "pressure hull", "collapse depth", "hydrostatic", "seawater", "corrosion", "galvanic", "cathodic protection", "PREN", "propeller", "AUV", "ROV", "class approval", "DNV", "ABS".
---

# Assessing marine AM parts

souxmar's marine capabilities size and screen; they do not qualify. Everything
in this skill produces **preliminary engineering estimates**. Classification
approval comes from a classification society running its own process on your
part, your machine and your evidence — never from this tool. Say so every time
you present a number.

Read [`docs/MARINE.md`](../../docs/MARINE.md) for the conventions and the
formulas. This skill is the operational walkthrough.

## When to use this skill

- Sizing a pressure hull, housing or subsea enclosure against a design depth.
- Choosing an alloy for seawater service, or checking a galvanic couple.
- Estimating corrosion allowance over a service life.
- Assembling the qualification-evidence checklist for an AM part destined for
  marine service.

## When NOT to use this skill

- The AM process itself — that is
  [`simulating-additive-manufacturing`](../simulating-additive-manufacturing/SKILL.md).
- Hydrodynamics, propeller performance, cavitation, seakeeping. None of that
  is modelled; the CFD adapters are a separate path.
- Anything a class society, a flag state or a regulator will rely on.

## The four capabilities

| Capability | Kind | Gives you |
| --- | --- | --- |
| `solver.marine.hydrostatic` | solver | nodal pressure in Pa, one time step per depth load case |
| `solver.marine.hull_collapse` | solver | collapse pressure, margin, governing mode (uniform over the mesh — it is analytical, not mesh-resolved) |
| `solver.marine.corrosion` | solver | thickness loss over the service life, pitting risk, galvanic risk |
| `writer.marine.qualification_report` | writer | the advisory evidence dossier |

## Depth and pressure

Gauge pressure, positive, compressive on the wetted surface. Seawater at
1025 kg/m³ gives **≈ 3.02 MPa at 300 m** — check any hydrostatic result
against `ρ·g·h` before you trust the rest of the chain.

The default `depth_factors: [1.0, 1.5, 2.25]` produce three load cases —
operating, test, collapse — as three time steps of one field. Name which case
you are quoting. If you leave `seawater_density` at 0 it is derived from
`salinity_psu` and `seawater_temperature`; the derivation and its validity
band are in the plugin header.

## Collapse

`solver.marine.hull_collapse` takes the hull geometry as **inputs**, not from
the mesh: `hull_type`, `diameter`, `thickness`, `unsupported_length`. It
evaluates the applicable modes and reports the governing (minimum) one:

- interframe elastic instability (Windenburg–Trilling) for a ring-stiffened
  cylinder,
- classical elastic buckling for an unstiffened cylinder,
- membrane yield `p = 2·σy·t/D`,
- `p_cr = 2E(t/R)²/√(3(1−ν²))` with knockdown for a sphere.

Two knockdowns matter for AM and both default to placeholders you should
replace with your own data: `imperfection_knockdown` (out-of-roundness — AM
hulls are not perfect cylinders) and `am_anisotropy_knockdown` (build-direction
property debit). **General instability of a ring-stiffened hull is not
evaluated at all.** If the governing mode comes back as general instability
territory, you need a real analysis.

## Corrosion and galvanic compatibility

`PREN = %Cr + 3.3·%Mo + 16·%N` is the first screen for chloride pitting.
The ordering that drives most marine AM decisions:

| Alloy | PREN | In warm stagnant seawater |
| --- | --- | --- |
| 316L | ≈ 24–26 | Marginal. Pits and crevice-corrodes; fine in cold flowing water, poor in warm stagnant water and under deposits. |
| 2507 super-duplex | ≈ 42 | Comfortable in the same conditions. |
| Ti-6Al-4V | – | Essentially immune; the reason it shows up in deep-hull work. |
| NAB (nickel-aluminium bronze) | – | The traditional propeller alloy; good erosion-corrosion behaviour, and the alloy behind the first class-approved printed ship's propeller. |
| CuNi 90-10 | – | Seawater piping, biofouling resistance. |

Then check the couple. `mating_alloy` plus `area_ratio_cathode_anode` drives
the galvanic term, and the area ratio is what actually bites: a small anodic
AM part bolted to a large cathodic structure corrodes fast. `as_built_surface`
defaults to true because as-built AM roughness is worse than machined — if
your part is machined all over, say so.

**Quote `alloy` values.** Until the parser fix in this release, `alloy: "2507"`
parsed as the number 2507 and silently fell back to 316L. Quoting is now
honoured, but the habit is worth keeping, and if you are on an older build,
pass the composition explicitly (`chromium_pct`, `molybdenum_pct`,
`nitrogen_pct`).

## The qualification dossier

`writer.marine.qualification_report` emits a Markdown checklist covering
feedstock traceability, machine qualification, witness coupons, NDT method and
coverage, the mechanical test matrix, the build-direction property
declaration, post-processing/HIP records, and the marine specifics. Its
structure follows publicly documented practice — DNV-ST-B203, the ABS AM
guidance, ISO/ASTM 52920/52930, ASTM F3122 — cited by name so the reader can
go and read the real thing.

What the dossier is: a checklist of what a qualification campaign would need,
plus whatever simulation evidence you supplied.
What it is not: approval, a certificate, or evidence that any box is ticked.

## Review checklist

- [ ] Design depth, test depth and collapse depth all stated, and the quoted
      result labelled with which one it is.
- [ ] Hull geometry inputs match the actual part (the solver does not read
      them from the mesh — cross-check against the mesh bounding box).
- [ ] Both knockdowns replaced with real data, or flagged as placeholders.
- [ ] Alloy quoted as a string; composition explicit where it matters.
- [ ] Galvanic couple and area ratio considered, not just the alloy alone.
- [ ] Corrosion allowance fed back into the wall thickness, and into
      `solver.am.printability`'s `corrosion_allowance` so a wall that only
      passes before corrosion is flagged.
- [ ] Every marine output presented with the advisory caveat attached. Not as
      a footnote — in the same sentence as the number.
