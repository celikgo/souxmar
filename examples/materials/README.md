# examples/materials — reference feedstock data for the AM + marine block

One file: [`am-marine.toml`](am-marine.toml). Twelve materials — seven metals
and five polymers — curated for additively manufactured parts destined for
marine and subsea service.

## Read this before you use a number from it

**These are reference values for setting up an analysis. They are not design
allowables.**

Every figure in the file is a rounded, indicative value for a material
*family*. Additively manufactured properties depend on the machine, the
parameter set, the build orientation, the shielding atmosphere, the powder or
wire lot, the thermal history and the post-processing. Two 316L coupons
printed from the same nominal recipe on two machines can differ by 15% in
yield and by a factor of two in elongation. Nothing in this file has been
measured by anyone involved in souxmar.

What the file is good for:

* getting a pipeline to run with numbers that are the right order of magnitude;
* comparing materials against each other for a first cut at selection;
* sanity-checking a result you got some other way.

What it is not good for: anything that gets built, quoted, certified or signed
off. Replace every value with one from your own qualified coupons first.

Every material carries a `source` string, and so does every sub-table
(`process_window`, `anisotropy`, `corrosion`, `seawater`). Where a number
cannot be attributed to a standard or a datasheet family, the source says
"indicative" and the number is rounded hard so nobody mistakes it for a
measurement. Where it *can* be attributed — 2507's yield minimum to ASTM A240,
NAB's to ASTM B148 C95800, Ti-6Al-4V's to ASTM F2924 — the source names the
standard and says which condition it applies to.

## No plugin reads this file

Deliberate, per ADR-0044. Every material property in the manufacturing block
is an explicit pipeline input with a documented default, so:

* a `pipeline.yaml` is self-describing — you can read one and know exactly what
  material assumptions produced the result;
* a pipeline's output does not silently change when someone edits a material
  database;
* a plugin needs no file I/O, no parser and no search path for materials, which
  keeps it inside the "public headers only, C++20 stdlib only" plugin rule.

The cost is that material values are copy-pasted into stage inputs. That is
the intended workflow, and the file is laid out to make it painless.

## Key names match pipeline input keys

Any key in `am-marine.toml` that shares a name with a capability input key
holds the same quantity in the same unit. So this:

```toml
[metals."316L"]
density = 7990.0
youngs_modulus = 1.90e11
poisson_ratio = 0.28
yield_strength = 5.0e8
cte = 1.60e-5
thermal_conductivity = 15.0
specific_heat = 500.0
melt_temperature = 1400.0
```

pastes straight into a stage:

```yaml
  - id: distortion
    plugin: solver.am.distortion.inherent_strain
    input:
      mesh: { from: build }
      youngs_modulus: 1.9e11
      poisson_ratio: 0.28
      cte: 1.6e-5
      melt_temperature: 1400.0
```

The same holds for `[*.process_window]` (`laser_power`, `scan_speed`,
`hatch_spacing`, `process_layer_height`, `absorptivity`,
`nozzle_temperature`, `bed_temperature`, `chamber_temperature`,
`print_speed`, `road_width`), for `am_anisotropy_knockdown` (which goes into
`solver.marine.hull_collapse`), and for `chromium_pct` / `molybdenum_pct` /
`nitrogen_pct` (which go into `solver.marine.corrosion`).

A few keys are deliberately *not* named after a pipeline input because the
contract's key name would be misleading. `[metals."NAB".process_window]` calls
its heat-source power `arc_power`, not `laser_power`, because there is no laser
in a wire-arc cell — but the pipeline key you paste it into is `laser_power`,
and the file says so inline.

Units are strict SI throughout, with souxmar's one documented exception:
**temperatures are degrees Celsius**, here and in every YAML key and every
output field.

## What is in it

| id | family | AM route | why it is in a marine list |
| --- | --- | --- | --- |
| `316L` | austenitic stainless | LPBF, DED | the default; cheap, prints easily, marginal in stagnant seawater |
| `2507` | super duplex stainless | LPBF, DED | PREN 42; what you use when 316L will not survive the water |
| `NAB` | nickel-aluminium bronze | DED/WAAM | the propeller alloy: cavitation-erosion resistance, protective film |
| `Ti-6Al-4V` | alpha-beta titanium | LPBF, EBM | immune to seawater, half the density, and dangerously noble |
| `IN625` | Ni-Cr-Mo superalloy | LPBF, DED | hot, stressed and wet at the same time — valve and connector trim |
| `AlSi10Mg` | Al-Si casting alloy | LPBF | light and fast to print; the worst choice for permanently wet parts |
| `CuNi90-10` | copper-nickel | DED/WAAM | seawater piping and heat exchangers; hard flow-velocity ceiling |
| `PA12-CF` | carbon-filled polyamide | FFF, SLS | the default engineering polymer; absorbs water |
| `PEKK` | polyaryletherketone | FFF, SLS | subsea structural thermoplastic; needs a heated chamber |
| `PEEK` | polyaryletherketone | FFF, SLS | as above, better characterised, harder to print |
| `PVDF` | fluoropolymer | FFF | chemically bulletproof; cable jackets, flexible-pipe liners |
| `HDPE` | polyolefin | FFF | inert, zero water uptake, and it floats. Creeps badly |

Per material: density, Young's modulus, Poisson's ratio, as-built yield and
ultimate strength, CTE, thermal conductivity, specific heat, melt temperature
(plus glass-transition temperature for the polymers), an indicative AM process
window, the as-built Z-versus-XY anisotropy ratios, a PREN or a seawater note,
an indicative feedstock cost, and a `source` line for all of it.

## The three notes worth reading even if you skip the rest

**As-built LPBF is not wrought.** As-built 316L yields 450-590 MPa against a
wrought minimum of 205, because the cellular solidification structure is
strong and brittle. A solution anneal removes that advantage. Using a wrought
allowable for an as-built part is conservative; using an as-built number for
an annealed part is not. The file states the condition for every strength
value.

**As-built duplex is not duplex.** Rapid solidification suppresses austenite
formation, so as-built LPBF 2507 is 90%+ ferrite with poor toughness and
degraded corrosion resistance. It becomes duplex only after a solution anneal.
This is the single largest trap in the metals list.

**A semicrystalline polymer's healing threshold is not its Tg.** PA12's glass
transition is around 50 °C but interdiffusion between roads needs the
interface above the crystallisation temperature, nearer 150 °C.
`postproc.am.bond_strength` takes the threshold as
`glass_transition_temperature`, so for PA12 you must override it or the model
will report healing that does not happen. The file's `notes` field for
`PA12-CF` says this in as many words.

## What is deliberately absent

No fatigue data, no fracture toughness, no crack-growth curves, no
temperature-dependent property curves, no separate rows for HIPed / annealed /
aged conditions, no creep-rupture curves for the polymers, and no allowables
or safety factors of any kind. The end of `am-marine.toml` lists these and
says why. The short version: as-built AM surfaces knock fatigue strength down
by roughly half against machined-and-HIPed material, and no single number can
carry that.

## Where the numbers get used

* [`examples/am-lpbf-bracket`](../am-lpbf-bracket) — `316L`, LPBF window.
* [`examples/am-marine-propeller`](../am-marine-propeller) — `NAB`, WAAM window, coupled to `316L`.
* [`examples/am-submarine-pressure-hull`](../am-submarine-pressure-hull) — `316L`, plus its `am_anisotropy_knockdown` in the collapse check.
* [`examples/am-polymer-auv-fairing`](../am-polymer-auv-fairing) — `PEKK`, with `PA12-CF` named as the cheaper substitute.
* `docs/MANUFACTURING.md` and `docs/MARINE.md` — the capability reference.
* The `propose_am_setup` agent tool mirrors this file as a static table, so
  that the tool needs no file I/O. If you add a material here, that table is
  the other place to add it.
