# Manufacturing

souxmar ships an additive-manufacturing and subsea capability
block: eighteen capabilities covering metal and polymer AM process
simulation, design-for-additive-manufacturing checks, slicing, and
preliminary marine/pressure-hull assessment.

They are all **in-tree plugins** loaded through the same C ABI a
third-party plugin uses. `solver.am.thermal.lpbf` is dispatched
exactly the way `solver.elasticity.linear` is, and any of them can
be replaced by a higher-fidelity implementation behind the same
capability id.

## Two pages

- [Additive manufacturing](/manufacturing/additive) — LPBF,
  DED/WAAM, FFF and SLS: the process chain, the DfAM checks, G-code
  and build reports.
- [Marine and subsea](/manufacturing/marine) — depth and pressure
  conventions, pressure-hull collapse margin, seawater corrosion,
  and the advisory qualification dossier.

## Read this first

Every model in this block is **closed-form or heuristic**. None of
them is a finite-element solve. They are fast, deterministic and
honest about their limits — each plugin's source header opens with
`What it computes:` and `What this is NOT:`, and the repo docs carry
a per-capability fidelity table stating what each capability must
not be used for.

Concretely, that means:

| Use it for | Do not use it for |
| ---------- | ----------------- |
| Comparing two build orientations | Quoting a build |
| Ranking two process windows | Accept/reject on porosity |
| Seeing where distortion trends | Predicting distortion magnitude uncalibrated |
| Preliminary hull sizing | Class approval of anything |

Two constants must be calibrated against a measured part before any
metal-AM number here is quantitative: `strain_calibration`
(distortion) and `absorptivity` (melt pool). The procedures are in
the repo guide.

## Units

Strict SI — metres, kilograms, seconds, watts, pascals — with **one
exception: temperatures are degrees Celsius**, in inputs and in
output fields. So `layer_height: 0.001` is one millimetre and
`melt_temperature: 1400` is 1400 °C.

## Where things live

| Surface | What |
| ------- | ---- |
| CLI | `souxmar run <pipeline.yaml> --plugin-path <dir>` |
| Desktop | Manufacturing and Marine panels above the pipeline editor; they append whole stages into the open buffer |
| Agent | Six tools: `propose_am_setup`, `check_printability`, `set_build_orientation`, `estimate_build_cost`, `apply_hydrostatic_load`, `check_marine_integrity` |
| Examples | `examples/am-lpbf-bracket`, `am-marine-propeller`, `am-submarine-pressure-hull`, `am-polymer-auv-fairing` |

## Reference documents in the repo

- [`docs/MANUFACTURING.md`](https://github.com/celikgo/souxmar/blob/master/docs/MANUFACTURING.md)
  — the domain guide: every capability, every input key with unit
  and default, every output component order, the fidelity table and
  the calibration procedures.
- [`docs/MARINE.md`](https://github.com/celikgo/souxmar/blob/master/docs/MARINE.md)
  — marine conventions, collapse modes, corrosion, and the
  advisory-only scope statement.
- [ADR-0044](https://github.com/celikgo/souxmar/blob/master/docs/adr/0044-manufacturing-capability-namespaces.md)
  — why these capabilities live under the existing five plugin
  namespaces instead of a new one.
- [RFC-0012](https://github.com/celikgo/souxmar/blob/master/docs/rfcs/0012-am-process-simulation.md)
  — the physics roadmap: what replaces the closed-form models, and
  how it gets validated.
