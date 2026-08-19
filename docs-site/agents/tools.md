<!--
  This file is **generated** by scripts/docs-site/gen-agent-tools.py.
  Do not hand-edit — your changes will be overwritten on the next
  docs-site build. To change a tool's name / description / category,
  change it at the source (include/souxmar/ai/*.h + the matching
  src/ai/tools/*.cpp) and rebuild the docs site.
  Generator schema version: 1.
-->

# Tool catalogue

Every tool the souxmar agent can call is listed here. The list is
generated directly from `souxmar agent list --json` — what you see
below is exactly what the engine ships in this revision of the
binary. The contract is **frozen final at v1**
([ADR-0011](https://github.com/celikgo/souxmar/blob/master/docs/adr/0011-tool-contract-v1-final-freeze.md)).

**Contract version:** `v1`  **Tool count in this build:** **24**  

## At a glance
| Category | Tools |
| -------- | ----- |
| Read | `list_plugins`, `query_field`, `query_mesh_quality`, `read_geometry_summary`, `screenshot_viewport` |
| Mesh | `check_printability`, `estimate_build_cost`, `mesh` |
| BC | `apply_hydrostatic_load`, `set_bc`, `set_build_orientation`, `set_material` |
| Solve | `solve` |
| Field | `check_marine_integrity` |
| Pipeline | `apply_pipeline_diff`, `propose_am_setup`, `propose_pipeline` |
| Export | `export_results` |
| CFD | `apply_inlet`, `apply_outlet`, `apply_wall`, `propose_cfd_setup`, `validate_bcs` |
| Postproc | `compute_field` |

## Confirmation policies

- **`auto`** — no confirmation prompt — runs as soon as the agent calls it.
- **`confirm-once`** — first call in a session prompts; subsequent calls auto-proceed.
- **`confirm-always`** — every call prompts; never bypassed in a session.

## All tools

### Read

#### `list_plugins`

- **Category:** `Read`
- **Confirmation:** `auto`

Enumerate every capability registered with the host registry. Returns the capability id, kind, owning plugin id, advertised ABI version, and declared threading model. Use this before calling `mesh` / `solve` / `compute_field` / `export_results` to find a capability the host actually has loaded.

#### `query_field`

- **Category:** `Read`
- **Confirmation:** `auto`

Return min / max / mean over the current field (set by a prior `solve` call). Reports the field's location + kind metadata so the agent can reason about magnitude (scalar) vs. component (vector / tensor) interpretation.

#### `query_mesh_quality`

- **Category:** `Read`
- **Confirmation:** `auto`

Summarise mesh quality (signed volume, edge ratio, minimum dihedral angle) across the current mesh. Runs the `postproc.mesh_quality` plugin against ToolContext.mesh_handle and reports per-metric min/max/mean plus the count of cells flagged by simple thresholds (inverted, sliver, extreme aspect).

#### `read_geometry_summary`

- **Category:** `Read`
- **Confirmation:** `auto`

Inspect the current project's geometry. Returns vertex / edge / face / solid counts, the axis-aligned bounding box, and the list of named tags.

#### `screenshot_viewport`

- **Category:** `Read`
- **Confirmation:** `confirm-once`

Capture the current 3D viewport as a PNG and return its path. Requires the souxmar desktop build (headless / CLI agent: returns NOT_AVAILABLE).

### Mesh

#### `check_printability`

- **Category:** `Mesh`
- **Confirmation:** `auto`

Run the DfAM printability check (`solver.am.printability`) against the current mesh and summarise the blockers: score distribution, how many cells fall below the acceptance threshold, and which limiting factor dominates (overhang / thin wall / build-volume fit / aspect ratio / corrosion allowance). Read-only.

#### `estimate_build_cost`

- **Category:** `Mesh`
- **Confirmation:** `auto`

Estimate mass, build time, energy, machine cost, material cost and total for the staged part: reads session `manufacturing` (as staged by propose_am_setup) plus the session mesh's bounding box. Every parameter can be overridden per call. Bounding-box arithmetic, not a slicer — run solver.am.buildtime via `solve` for the layer-resolved figure.

#### `mesh`

- **Category:** `Mesh`
- **Confirmation:** `auto`

Run a registered mesher.* plugin against the current geometry. Returns a mesh summary (node / cell counts, bounding box). The resulting mesh handle is stashed on the session for subsequent solve / export tools.

### BC

#### `apply_hydrostatic_load`

- **Category:** `BC`
- **Confirmation:** `confirm-once`

Stage a depth-derived pressure load case on a tagged wetted surface: computes p = rho*g*h (optionally + atmospheric), derives seawater density from salinity + temperature when it is not given, and appends a `hydrostatic` entry to the session's boundary conditions. Supply depth_factors for an operating / test / collapse set.

#### `set_bc`

- **Category:** `BC`
- **Confirmation:** `confirm-once`

Attach a boundary condition (Dirichlet / Neumann / Robin) to a tagged entity. The BC is staged in session state and consumed by the next `solve` call.

#### `set_build_orientation`

- **Category:** `BC`
- **Confirmation:** `confirm-once`

Score candidate build orientations (the six axis-aligned directions plus any you supply) on projected support area, build height and projected footprint, then stage the winner in session state as a `build_orientation` entry plus `manufacturing.build_direction`. Uses the staged mesh when there is one; with no mesh it returns an advisory ranking over the candidate list and says so.

#### `set_material`

- **Category:** `BC`
- **Confirmation:** `confirm-once`

Attach a material specification (linear elastic / thermal / etc.) to a tagged region. The material is staged in session state and consumed by the next `solve` call.

### Solve

#### `solve`

- **Category:** `Solve`
- **Confirmation:** `confirm-always`

Run a registered solver.* plugin against the current mesh (set by a prior `mesh` call) plus any boundary conditions staged via `set_bc`. Returns a Field summary; the field handle is stashed on the session.

### Field

#### `check_marine_integrity`

- **Category:** `Field`
- **Confirmation:** `auto`

Advisory marine integrity summary for the current mesh: runs `solver.marine.hull_collapse` (collapse pressure, margin over the factored design pressure, governing mode) and `solver.marine.corrosion` (thickness loss over the service life, pitting risk, galvanic risk), then returns the numbers plus concrete advisories. Narrow with `checks`. Advisory only — not a classification-society calculation.

### Pipeline

#### `apply_pipeline_diff`

- **Category:** `Pipeline`
- **Confirmation:** `confirm-once`

Apply a list of structured edits (add / remove / set_input / replace) to a pipeline draft. The result is re-emitted as YAML and round-tripped through the parser, so the returned pipeline is guaranteed to load at `souxmar run` time. Read-only: produces a draft, never writes to disk.

#### `propose_am_setup`

- **Category:** `Pipeline`
- **Confirmation:** `auto`

Propose a complete, runnable additive-manufacturing pipeline for a {process, material, machine} triple: layered build mesh, process thermal history, distortion / residual stress (metals) or interlayer bond strength (polymers), overhang + printability + build-time stages, machine output (G-code / CLI slices) and a build report. Set include_marine or design_depth to append the hydrostatic / collapse-margin / corrosion / qualification-dossier stages. Read-mostly: it stages the resolved setup under session `manufacturing` and dispatches nothing.

#### `propose_pipeline`

- **Category:** `Pipeline`
- **Confirmation:** `auto`

Validate and emit a YAML pipeline draft from a structured spec. Round-trips through the parser so the draft is guaranteed to load — the agent can iterate before the user runs `write_pipeline` to commit it to disk.

### Export

#### `export_results`

- **Category:** `Export`
- **Confirmation:** `confirm-always`

Write the current mesh + (optional) field to disk via a registered writer.* plugin (e.g. writer.vtu for ParaView). Path may be relative; the writer resolves it. Required when the user asks to 'save the results' or 'open in ParaView'.

### CFD

#### `apply_inlet`

- **Category:** `CFD`
- **Confirmation:** `confirm-once`

Apply a CFD inlet BC to a tagged surface: velocity (scalar magnitude or 3-vector), optional static pressure, optional turbulence intensity / hydraulic diameter. The BC is staged in session state and consumed by the next `solve` call.

#### `apply_outlet`

- **Category:** `CFD`
- **Confirmation:** `confirm-once`

Apply a CFD outlet BC to a tagged surface: pressure_outlet (Dirichlet on static pressure), outflow (zero-gradient on velocity), or fully_developed (convective). Staged in session state; consumed by the next `solve` call.

#### `apply_wall`

- **Category:** `CFD`
- **Confirmation:** `confirm-once`

Apply a CFD wall BC to a tagged surface: choose between no-slip / slip / wall-function. Optional thermal coupling (temperature) and surface roughness. Staged in session state; consumed by the next `solve` call.

#### `propose_cfd_setup`

- **Category:** `CFD`
- **Confirmation:** `auto`

Propose a CFD setup from a verbal goal + optional list of mesh boundary tags. Returns a sequence of apply_inlet / apply_wall / apply_outlet calls the agent can dispatch, plus a recommended solver capability. Read-only; does not mutate session state. Output is a sketch — refine before solving.

#### `validate_bcs`

- **Category:** `CFD`
- **Confirmation:** `auto`

Sanity-check the staged boundary-condition bag. Reports duplicate tags, missing inlet/outlet on a CFD case, malformed entries, and a tally by BC type. Read-only — does not mutate session_state.

### Postproc

#### `compute_field`

- **Category:** `Postproc`
- **Confirmation:** `confirm-always`

Compute a derived field from the current mesh + field via a registered postproc.* plugin (e.g. von Mises from stress, magnitude from a vector field). The resulting field is stashed on the session for further tools.


## How this page is generated

```sh
scripts/docs-site/gen-agent-tools.py \
    --engine build/dev/tools/souxmar/souxmar \
    --out    docs-site/agents/tools.md
```

Re-runs in CI on every master-push that touches `docs-site/` or
`scripts/docs-site/`. To verify it's in sync locally:

```sh
scripts/docs-site/gen-agent-tools.py \
    --engine build/dev/tools/souxmar/souxmar \
    --out    docs-site/agents/tools.md \
    --check-only
```
