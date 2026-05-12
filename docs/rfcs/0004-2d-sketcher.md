# RFC 0004: 2D sketcher + constraint solver

- **Author:** TBD (Desktop team lead, ratified by Platform)
- **Status:** Draft
- **Tracking issue:** TBD
- **Affects:** ABI, agent tool contract, on-disk format (new `*.sketch.yaml`)
- **Tier:** 3
- **Date opened:** 2026-05-12
- **Date `final-comment` started:** —
- **Date accepted / rejected:** —

> **Stub.** Builds on RFC-003 (BREP session). Sketches anchor to a plane — either a world plane or a planar face on a BREP body. The constraint solver lives in a plugin (`sketch.solver.*`) so the kernel choice can swap without touching the host. RFC-005 (feature tree) consumes sketches as inputs for extrude / revolve / sweep / loft.

## Summary

Add a 2D sketcher to the workbench: pick a plane, draw primitives (line, polyline, arc, circle, spline), apply geometric + dimensional constraints, solve. Sketches persist as `*.sketch.yaml` inside the project directory and are referenced by the feature tree (RFC-005) as inputs to extrude / revolve / sweep. The constraint solver is wrapped as a `sketch.solver.*` plugin; the first implementation is `examples/plugins/sketch-solver-gcs` wrapping FreeCAD's **planegcs** (LGPL-2.1). The C ABI gains `include/souxmar-c/sketch.h` (ABI minor `(1, 4, 0)`, additive). Sixteen agent tools land in `libsouxmar-ai` for the chat-driven authoring path.

## Motivation

The post-v1.0 block's Sprint 30 (parametric features) requires sketch inputs; without RFC-004, the feature operations have nothing to extrude or revolve. Concretely:

- The Sprint 24 dogfood retro (TBD link) flagged "I can't author a part — I can only import STEP" as the second-highest external friction point, behind the renderer.
- The agent's "design a bracket" eval case (planned for Sprint 30) presumes a sketch tool surface that does not exist.
- Architectural / civil users — one of our three primary personas per `docs/VISION.md` — author 2D plans more than 3D bodies; a competent sketcher unlocks that workflow even before the parametric tree lands.

The constraint solver is the load-bearing decision because it determines licensing, the math envelope (how many DoF can be solved interactively), and how diagnostic errors surface to the user when the sketch is over- or under-constrained.

## Proposal

Four layers: solver plugin → kernel ABI → bridge → React/agent.

### Solver: planegcs via a `sketch.solver.*` plugin

planegcs is FreeCAD's 2D geometric-constraint solver, LGPL-2.1, mature, and battle-tested in production at FreeCAD scale. Dynamic-link only (matches the OCCT posture in RFC-003). Wrapped in `examples/plugins/sketch-solver-gcs/`, capability `sketch.solver.gcs`.

The plugin contract is small: receive a list of primitives + constraints, mutate the primitives in place to satisfy the constraints, return solver status. The host doesn't depend on planegcs directly — the solver is hot-swappable. A future RFC could add a souxmar-native solver (alternative C below) without touching the host.

### C ABI: `include/souxmar-c/sketch.h` (new)

`SOUXMAR_C_API_VERSION` bumps `(1, 3, 0)` → `(1, 4, 0)`. Additive.

```c
/* include/souxmar-c/sketch.h — additive, v1.4.
 *
 * A 2D sketch is a list of primitives in a local UV plane, plus a
 * list of constraints over those primitives, plus an anchor (the
 * placement of the UV plane in 3D). The kernel side owns the data;
 * solver plugins operate on it via the ABI below.
 *
 * Threading: sketches are single-threaded. A sketch is one unit of
 * mutation; concurrent edits are caller-coordinated.
 */

#ifndef SOUXMAR_C_SKETCH_H
#define SOUXMAR_C_SKETCH_H

#include "abi.h"
#include "brep.h"   /* anchor faces are BREP face IDs */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct souxmar_sketch_t souxmar_sketch_t;

/* ---- Primitive kinds (stable numeric values) ---- */
#define SOUXMAR_SK_POINT      0
#define SOUXMAR_SK_LINE       1
#define SOUXMAR_SK_ARC        2
#define SOUXMAR_SK_CIRCLE     3
#define SOUXMAR_SK_POLYLINE   4
#define SOUXMAR_SK_SPLINE     5   /* cubic Bezier, control points in UV */

/* ---- Constraint kinds (stable numeric values) ---- */
#define SOUXMAR_SC_COINCIDENT      10
#define SOUXMAR_SC_PARALLEL        11
#define SOUXMAR_SC_PERPENDICULAR   12
#define SOUXMAR_SC_TANGENT         13
#define SOUXMAR_SC_EQUAL           14
#define SOUXMAR_SC_HORIZONTAL      15
#define SOUXMAR_SC_VERTICAL        16
#define SOUXMAR_SC_DISTANCE        17   /* dimensional; value carried */
#define SOUXMAR_SC_ANGLE           18
#define SOUXMAR_SC_RADIUS          19
#define SOUXMAR_SC_DIAMETER        20

/* ---- Lifecycle ---- */

SOUXMAR_API
souxmar_sketch_t* souxmar_sketch_new(void);

SOUXMAR_API
void souxmar_sketch_free(souxmar_sketch_t* sketch);

/* ---- Anchor (where the sketch lives in 3D) ---- */

/* World-axis-aligned planes: 0 = XY, 1 = XZ, 2 = YZ. */
SOUXMAR_API
souxmar_status_t souxmar_sketch_anchor_world(souxmar_sketch_t* sketch,
                                              uint8_t            plane);

/* Anchor to a planar face on a BREP session. The face must be planar
 * (kernel rejects non-planar with SOUXMAR_E_INVALID_ARGUMENT). */
SOUXMAR_API
souxmar_status_t souxmar_sketch_anchor_face(souxmar_sketch_t*               sketch,
                                             const souxmar_brep_session_t*   session,
                                             uint64_t                         face_id);

/* ---- Add primitives (returns a u32 primitive ID) ---- */

SOUXMAR_API
uint32_t souxmar_sketch_add_point(souxmar_sketch_t* sketch, double u, double v);

SOUXMAR_API
uint32_t souxmar_sketch_add_line(souxmar_sketch_t* sketch,
                                  uint32_t           start_point_id,
                                  uint32_t           end_point_id);

SOUXMAR_API
uint32_t souxmar_sketch_add_arc(souxmar_sketch_t* sketch,
                                 uint32_t           start_point_id,
                                 uint32_t           end_point_id,
                                 uint32_t           centre_point_id);

SOUXMAR_API
uint32_t souxmar_sketch_add_circle(souxmar_sketch_t* sketch,
                                    uint32_t           centre_point_id,
                                    double             radius);

/* ---- Add constraints (returns a u32 constraint ID) ---- */

/* Generic constraint add. `kind` is SOUXMAR_SC_*. Targets is the
 * primitive ID array the constraint operates on (length depends on
 * kind: 1 for horizontal/vertical, 2 for parallel/perpendicular/etc.).
 * `value` is the dimensional value (mm/rad/etc.); 0.0 for non-
 * dimensional constraints. */
SOUXMAR_API
uint32_t souxmar_sketch_add_constraint(souxmar_sketch_t* sketch,
                                        uint8_t            kind,
                                        const uint32_t*    targets,
                                        size_t             target_count,
                                        double             value);

/* ---- Queries ---- */

SOUXMAR_API
size_t souxmar_sketch_num_primitives(const souxmar_sketch_t* sketch);

SOUXMAR_API
size_t souxmar_sketch_num_constraints(const souxmar_sketch_t* sketch);

SOUXMAR_API
souxmar_status_t souxmar_sketch_point_position(const souxmar_sketch_t* sketch,
                                                uint32_t                 point_id,
                                                double*                  out_u,
                                                double*                  out_v);

/* ---- Solver invocation (handled by the host; calls into the plugin) ---- */

/* Returns: SOUXMAR_OK (solved), SOUXMAR_E_UNDERCONSTRAINED,
 * SOUXMAR_E_OVERCONSTRAINED, SOUXMAR_E_NO_CONVERGENCE. */
SOUXMAR_API
souxmar_status_t souxmar_sketch_solve(souxmar_sketch_t* sketch);

#ifdef __cplusplus
}
#endif
#endif /* SOUXMAR_C_SKETCH_H */
```

The solver-plugin vtable is one entry point — `solve_fn(sketch_t*) → status` — registered against the `sketch.solver.*` capability. The kernel picks the highest-priority installed solver (priority is a manifest field).

### Solver plugin contract

```toml
# examples/plugins/sketch-solver-gcs/souxmar-plugin.toml (sketch)

[plugin]
id            = "dev.souxmar.examples.sketch-solver-gcs"
name          = "FreeCAD planegcs Sketch Solver"
version       = "0.1.0"
abi           = 1
min_souxmar_abi_minor = 4

[plugin.capabilities]
provides      = ["sketch.solver.gcs"]

[plugin.threading]
model         = "single-threaded"

[plugin.priority]
# Higher wins when multiple solvers are installed.
solver        = 100
```

### Bridge side

```rust
// src-tauri/souxmar-bridge/src/sketch.rs (sketch)

pub struct SketchSummary {
    pub project_id:        String,
    pub sketch_id:         String,
    pub anchor:            SketchAnchor,
    pub primitive_count:   u32,
    pub constraint_count:  u32,
    pub dof:               i32,             // negative = over-constrained
    pub last_solve_status: String,
}

impl Bridge {
    pub fn sketch_new(&self, project_id: &str, anchor: SketchAnchor)
        -> Result<String, BridgeError>;      // returns sketch_id

    pub fn sketch_add_primitive(&self, sketch_id: &str, kind: u8, args: PrimitiveArgs)
        -> Result<u32, BridgeError>;         // primitive_id

    pub fn sketch_add_constraint(&self, sketch_id: &str, kind: u8,
                                 targets: Vec<u32>, value: f64)
        -> Result<u32, BridgeError>;         // constraint_id

    pub fn sketch_solve(&self, sketch_id: &str)
        -> Result<SketchSummary, BridgeError>;

    pub fn sketch_save(&self, sketch_id: &str, path: &str)
        -> Result<(), BridgeError>;          // writes *.sketch.yaml

    pub fn sketch_load(&self, project_id: &str, path: &str)
        -> Result<String, BridgeError>;      // returns sketch_id
}
```

### On-disk format: `*.sketch.yaml` v1

```yaml
# example.sketch.yaml — v1 frozen at v1.2 release.
version: 1
anchor:
  kind: world          # "world" | "face"
  plane: XY            # only when kind == world; "XY" | "XZ" | "YZ"
  # OR:
  # kind: face
  # session: <design.yaml session reference>
  # face_id: 42

primitives:
  - id: 1
    kind: point
    u: 0.0
    v: 0.0
  - id: 2
    kind: point
    u: 50.0
    v: 0.0
  - id: 3
    kind: line
    start: 1
    end: 2

constraints:
  - id: 1
    kind: horizontal
    targets: [3]
  - id: 2
    kind: distance
    targets: [1, 2]
    value: 50.0          # millimetres
```

Units are project-wide (`design.yaml` carries the unit; RFC-005's territory). The sketch file itself stores raw numbers in project units.

### React side

A sketcher mode in the Viewport. Entering the mode (toolbar button or `sketch.new` agent tool) switches the camera to orthographic, locks the view to the sketch plane, and overlays a grid + snap indicators. Drawing tools live in a left rail: point, line, polyline, arc (3-point + centre+radius), circle, spline. A dimension toolbar applies constraints to the current selection. Solver status renders in the bottom strip ("solved" / "under-constrained: 3 DoF" / "over-constrained: redundant H/V on line #4").

Exiting the mode either solves-and-saves (default) or discards (cancel). Save writes `<project>/sketches/<id>.sketch.yaml`.

### Agent tool surface (16 tools, all `confirm-once`)

The full list — `confirm-once` because each call mutates a sketch on disk:

| Tool                          | Purpose                                                |
| ----------------------------- | ------------------------------------------------------ |
| `sketch.new`                  | Create a new sketch on a plane.                        |
| `sketch.add_point`            | Add a point primitive.                                 |
| `sketch.add_line`             | Add a line between two points.                         |
| `sketch.add_arc`              | Add a 3-point arc.                                     |
| `sketch.add_circle`           | Add a circle.                                          |
| `sketch.add_polyline`         | Add a polyline.                                        |
| `sketch.add_spline`           | Add a cubic spline.                                    |
| `sketch.constrain_coincident` | Coincident constraint between two points.              |
| `sketch.constrain_parallel`   | Parallel between two lines.                            |
| `sketch.constrain_perpendicular` | Perpendicular between two lines.                    |
| `sketch.constrain_tangent`    | Tangent between curve + line or curve + curve.          |
| `sketch.constrain_horizontal` | Horizontal on a line.                                  |
| `sketch.constrain_vertical`   | Vertical on a line.                                    |
| `sketch.constrain_distance`   | Dimensional distance.                                   |
| `sketch.constrain_radius`     | Dimensional radius on circle / arc.                     |
| `sketch.solve`                | Force a solve, return status + DoF.                     |

Each ships with one eval case. The agent eval suite grows by 16.

## Alternatives considered

### Alternative A: solvespace's libslvs

solvespace is GPLv3. Excellent solver, widely used, but the licence makes it incompatible with Apache-2.0 distribution under any plausible reading. Rejected on licence alone; no further analysis needed.

### Alternative B: Build a souxmar-native solver

A Gauss-Newton + LM solver on the constraint Jacobian is a several-week project. Tempting because it removes a dependency.

Rejected because: (a) planegcs has fifteen years of bug-fixes for the degenerate cases (singular Jacobians on coincident points, branch selection on tangents, etc.) that a naive solver hits in week 1; (b) the budget doesn't accommodate "write a robust constraint solver" inside Sprint 29; (c) the plugin contract is small enough that we can build a native solver later as a *second* `sketch.solver.*` plugin without re-doing the host work.

### Alternative C: Don't ship dimensional constraints in v1; geometric only

Geometric constraints alone (parallel/perpendicular/coincident/tangent) are easier to solve and don't need a value-carrying constraint shape. Could ship in S29 and add dimensional in S30.

Rejected because: a sketcher without dimensions is a drawing tool, not a CAD tool. The Sprint 30 feature operations need dimensions (extrude depth, fillet radius); ducking them in S29 just delays the same work. Better to ship the full DoF story once.

### (Considered and rejected: do nothing)

Without a sketcher, Sprint 30 has no inputs. The post-v1.0 block's load-bearing v1.2 release fails.

## Drawbacks

- **Solver plugin is mission-critical and licensed LGPL.** Same relink-rights story as OCCT in RFC-003 — folded into the existing `docs/LICENSING.md` deliverable.
- **planegcs is C++ with heavy templating.** Plugin author overhead is real; one engineer needs to own this plugin (similar to OCCT, possibly the same person).
- **DoF accounting is exposed to the user.** "Under-constrained: 3 DoF" is correct but unfriendly. Iterating on the message will take dogfood cycles.
- **The 16 agent tools expand the eval surface materially.** Each needs a regression case; the agent eval suite roughly doubles in this sprint.
- **`*.sketch.yaml` is a new frozen format.** v1.x ABI keeps it stable; format evolution is then a Tier-3 RFC.

## Migration plan

- **Existing pipeline files:** unaffected; sketches are a new artefact type.
- **Existing in-tree / out-of-tree plugins:** unaffected (additive ABI).
- **Saved chat sessions:** the 16 `sketch.*` tools are additive; sessions referencing only v1.0/v1.1 tools replay identically.
- **Project directory layout:** new subdirectory `<project>/sketches/` — additive; older project trees without this directory load unchanged.
- **Documentation:** new `docs/tutorials/your-first-sketch.md`; updates to `docs/PLUGIN_SDK.md` (new `sketch.solver.*` plugin type), `docs/AI_INTEGRATION.md` (16 new tools).

## Pre-mortem

It is 2027-05-12. RFC-004 went badly. What happened:

We shipped the sketcher with planegcs and discovered three months later that a specific failure mode — sketches with > 200 DoF that look fine in the UI but take minutes to solve — was hitting a chunk of the architectural-user persona who model floor plans with hundreds of walls. The "split this sketch" suggestion we documented in the risk register turned into a forum thread of confused users. We added a hard cap and a "convert N-segment polyline to single primitive" feature in v1.2.2 that materially helped, but only after we'd burned trust with the architectural early adopters. Separately, the dimensional-constraint UX let users enter dimensions in inches when the project unit was millimetres without converting — a stress-test PR caught one case but missed the radius vs. diameter pairing.

Leading indicators to watch in the first six months:

- Any user-reported "sketch hangs my app" issue with > 100 primitives.
- Bug reports referencing wrong-units dimensions in sketches.
- Solver status messages that reach the forum verbatim (implies users can't decode them).
- > 5% of saved sketches in dogfood exceed 50 DoF — implies the solver is being pushed past its comfort zone.

## Open questions

1. **Spline definition.** Cubic Bezier in control points vs. cubic B-spline with knot vector. planegcs supports both; pick the simpler one (Bezier) for v1; document the choice.
2. **Reference geometry.** Should sketches support construction lines (geometric helpers that don't participate in the profile)? Yes, almost certainly — needed for symmetry lines. Add a `construction: true` flag on the primitive. Pending decision: in the v1 format or v1.1?
3. **Symmetry constraint.** Common in real workflows; not in the initial 11 constraint kinds above. Add later or now? Lean toward later (S30 can land it as an RFC-005 extension).
4. **Sketch IDs.** Globally unique within project? Or scoped to the design tree? Lean toward project-scoped UUIDs.
5. **Dimension display unit.** Project unit, or per-dimension override? Lean project-wide; revisit if user feedback demands per-dimension.
6. **Editing while solver runs.** Block UI vs. solve-on-blur? Solve-on-blur with a 250ms debounce; document.

## Implementation plan

Six PRs in Sprint 29.

- [ ] **PR 1 — ABI add.** `include/souxmar-c/sketch.h`; in-core data model; ABI bump to `(1, 4, 0)`; conformance test scaffold for `sketch.solver.*`. Reviewer: ABI gate.
- [ ] **PR 2 — Solver plugin.** `examples/plugins/sketch-solver-gcs/`; conformance suite on a corpus of test sketches (degenerate / under / over / well-constrained).
- [ ] **PR 3 — On-disk format.** `*.sketch.yaml` v1 schema + round-trip tests; format documented in `docs/PLUGIN_SDK.md` (sketch addendum).
- [ ] **PR 4 — Bridge surface.** Rust commands; Tauri registration; React hook `useSketch(sketch_id)`.
- [ ] **PR 5 — Sketcher UI.** Mode toggle in Viewport; orthographic camera; grid + snap; primitive drawing tools; dimension toolbar; solver status strip.
- [ ] **PR 6 — Agent tools.** 16 `sketch.*` tools wired through the dispatcher; eval cases for each; "your first sketch" tutorial.
- [ ] ADR filed at `docs/adr/NNNN-sketch-solver-pin.md`.
- [ ] Documentation: tutorial; `docs/AI_INTEGRATION.md` update.

## References

- `docs/SPRINT_PLAN.md` — Post-v1.0 plan, Sprint 29 row.
- `docs/rfcs/0003-cad-kernel.md` — Sketches anchor to BREP faces from sessions defined there.
- `docs/rfcs/0005-feature-tree.md` — Feature operations consume sketches as inputs.
- `docs/PLUGIN_SDK.md` — Plugin-type taxonomy; gains `sketch.solver.*`.
- `docs/AI_INTEGRATION.md` — Tool-surface contract.
- `examples/plugins/occt-reader/` — Existing pattern for kernel-bound plugins.
- planegcs upstream (FreeCAD) — TBD link at Sprint 29 day 1.
