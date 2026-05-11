---
name: auditing-mesh-quality
description: Use when a mesh has been generated and you need to assess whether it is suitable for FEM/CFD analysis. Computes per-cell quality metrics (Jacobian, aspect ratio, dihedral angle, skewness, orthogonality), identifies bad cells, recommends refinement strategy. Triggers on "mesh quality", "mesh metrics", "bad mesh", "mesh refinement", "Jacobian", "aspect ratio", "skewness".
---

# Auditing mesh quality

A correct solver on a bad mesh produces wrong answers. Before any non-trivial analysis, the mesh must clear quality thresholds appropriate for the solver and the physics. This skill walks through the souxmar mesh-quality audit.

## When to use this skill

- After running any mesher (native, Gmsh, third-party) before solving.
- Investigating a "stress concentration" that turns out to be a bad-cell artefact.
- Comparing two mesh-algorithm options on the same geometry.
- Establishing acceptance criteria for a custom mesher's output.

## When NOT to use this skill

- Pure visualisation of a mesh — that does not need quality scoring.
- Surface meshes for rendering only — quality requirements differ from FEM.

## Quality metrics souxmar computes

The mesh-quality module (`src/core/mesh/quality.cpp`) computes per-cell:

| Metric          | Definition                                                                  | Good range (FEM)         |
| --------------- | --------------------------------------------------------------------------- | ------------------------ |
| Jacobian ratio  | min/max determinant of the element Jacobian over Gauss points              | > 0.3 (linear), > 0.1 (quadratic) |
| Aspect ratio    | longest edge / shortest edge                                                | < 10 for stress, < 4 for transient |
| Skewness        | normalised deviation from ideal element shape (0 = perfect, 1 = degenerate) | < 0.7                    |
| Min dihedral    | smallest angle between adjacent faces                                       | > 15° (tet), > 30° (hex) |
| Max dihedral    | largest angle between adjacent faces                                        | < 165° (tet), < 150° (hex) |
| Edge ratio      | longest edge / smallest edge in cell                                        | < 8                      |

For CFD (additional):

| Metric             | Definition                                                | Good range (CFD)             |
| ------------------ | --------------------------------------------------------- | ---------------------------- |
| Orthogonality      | how aligned cell-face normals are with cell-centre lines  | > 0.5 (1 is ideal)           |
| Non-orthogonality  | inverse measure                                           | < 70°                        |
| Wall y+            | dimensionless wall distance (RANS turbulence models)      | model-specific (< 1, 30–300) |

Thresholds are solver-dependent. The values above are conservative defaults; specific solvers in the registry tighten or relax them.

## Running the audit

### From CLI

```bash
souxmar mesh audit <pipeline-file> --stage mesh
```

Output:

```
Mesh: 124,392 cells (tet)

Jacobian ratio:
  min     0.42         > 0.30 ✓
  p5      0.61
  median  0.83
  max     1.00
Aspect ratio:
  median  2.4          < 10  ✓
  p95     6.1
  max    14.2          ⚠ 14 cells > 10  (cells: 8231, 18044, ...)
Min dihedral:
  min    11.4°         > 15° ✗ 3 cells below threshold (8231, 23110, 99201)
Skewness:
  median  0.31         < 0.7 ✓
  max     0.91         ⚠ 1 cell at 0.91

VERDICT: 3 cells fail mandatory thresholds; 15 cells warn.
        Suggest: refine in regions [bbox: ...]; rerun mesher with target_size_local=...
```

### From Python

```python
import pysouxmar as sx

mesh = sx.load_mesh("path/to/mesh.vtu")
report = sx.audit_mesh_quality(mesh, profile="fem-elasticity-linear")
print(report.summary())
report.export_html("quality-report.html")
```

### From the desktop app

Inspector → Mesh tab → "Audit quality" button. The viewport overlays bad-cell markers; the inspector lists them with click-to-zoom.

### From the agent

The agent can call the `audit_mesh_quality` tool (read-only). It reports a structured summary and optionally a screenshot with bad cells highlighted.

## Quality profiles

Different solver kinds need different thresholds. Profiles live in `src/core/mesh/profiles/`:

- `fem-elasticity-linear` — strict on Jacobian, lenient on aspect ratio.
- `fem-elasticity-nonlinear` — strict on everything; nonlinear solvers diverge on bad cells.
- `fem-thermal` — moderate; thermal is more forgiving.
- `cfd-laminar-incompressible` — strict orthogonality.
- `cfd-rans-turbulent` — strict orthogonality + y+ targets.

Pick the profile that matches your downstream solver. Custom profiles can be added in user projects.

## Interpreting the verdict

- **Pass:** all cells within thresholds. Proceed.
- **Warn:** some cells outside but recoverable. Solver may converge with iteration count or relaxation tweaks. Document in the analysis report.
- **Fail:** mandatory thresholds violated. Solver may converge but result is unreliable. Refine and re-mesh.

## Refinement strategies

When the audit fails:

1. **Local refinement at the failing cells.** Most meshers accept `target_size_at_box(...)` parameters. Re-mesh with finer target size in the bad region.
2. **Switch element type.** If linear tets are too stiff, use quadratic tets. If hexes are too rigid for the geometry, use mixed tet/hex.
3. **Switch mesher.** If native produces sliver tets on a tricky geometry, try Gmsh; vice versa. The plugin model exists exactly for this.
4. **Heal the geometry.** Many bad cells trace back to bad CAD: small slivers between faces, near-coincident vertices, ill-defined fillets. Fix the CAD first.
5. **Coarsen to convergence test.** Sometimes the cells the audit flags are in regions that do not influence the answer. A coarsening + convergence study confirms.

## Common mistakes

- Treating Jacobian as the only metric. A mesh with great Jacobian and bad dihedral angles still produces poor stress fields.
- Auditing after solving and being surprised. Audit before.
- Setting thresholds tighter than the solver actually needs because "stricter is safer." It is more expensive; pick thresholds calibrated to the solver.
- Ignoring orthogonality for CFD. Linear-elastic-FEM thresholds do not apply.
- Accepting a "warn" verdict on a critical analysis without explanation in the report.
- Re-meshing globally when local refinement would suffice. Slower; doesn't help; may move the bad cells, not eliminate them.

## Reference

- `docs/ARCHITECTURE.md` — mesh data model.
- `docs/PLUGIN_SDK.md` — mesher capability contract (tag inheritance, etc.).
- `src/core/mesh/quality.cpp` — implementation.
- `src/core/mesh/profiles/` — quality profiles.
- NAFEMS R0094 — mesh quality reference (industry baseline).
