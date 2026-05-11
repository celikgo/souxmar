---
name: validating-solver
description: Use when adding a new solver, modifying an existing one, or investigating suspect numerical results. Walks through analytical-solution comparisons, golden tests, mesh-convergence studies, and cross-solver validation. Triggers on "solver validation", "verify solver", "FEM correctness", "convergence study", "golden result".
---

# Validating a souxmar solver

A solver that is fast and elegant but gives wrong answers is worse than not having it. Every souxmar solver — in-tree (`solver.elasticity.linear`, `solver.heat.linear`) or out-of-tree — must demonstrate correctness through a defined validation protocol.

## When to use this skill

- Adding a new solver plugin (in-tree or out-of-tree).
- Modifying an existing solver's numerics, integration scheme, or BC handling.
- Investigating a suspicious result from a real analysis ("this stress looks too high").
- Auditing third-party solver plugins for the conformance badge.

## When NOT to use this skill

- Pure refactors that do not change the numerical path. Standard tests cover those.
- Performance-only changes — use `benchmarking-souxmar` instead.

## Validation hierarchy

Solvers are validated at four levels, in order. A solver that fails at any level cannot ship.

### Level 1 — Analytical solutions

The solver must reproduce the closed-form solution for at least one canonical problem in its domain.

| Solver class                 | Canonical analytical case            | Tolerance       |
| ---------------------------- | ------------------------------------ | --------------- |
| Linear elasticity            | Cantilever beam, end load (Euler-Bernoulli) | 5 % vs analytical at tip deflection, on a fine-enough mesh |
| Linear elasticity (3D)       | Patch test (constant-strain)         | Machine epsilon |
| Heat conduction (steady)     | 1D bar, fixed temperatures           | 1 % vs analytical |
| Heat conduction (transient)  | 1D bar, step temperature on one end  | 2 % at multiple times |
| Modal analysis               | Simply-supported beam, first 5 modes | 2 % vs analytical |
| Linear-elastic plate         | Kirchhoff plate, point load          | 5 %             |

For new solver categories without an obvious canonical case, propose one in the validation RFC.

Tests live in `tests/validation/<solver>/`. They run on every PR.

### Level 2 — Patch tests

The solver must pass the patch test for its element type: a small mesh subjected to a uniform-strain or uniform-temperature field must produce that exact field everywhere, to machine precision.

Patch test failure means the element is non-conforming. Do not ship.

### Level 3 — Mesh-convergence study

For at least one non-trivial problem, the solver's error must decrease at the theoretical rate as the mesh is refined:

| Element                   | Expected convergence rate      |
| ------------------------- | ------------------------------ |
| Linear tet (4-node)       | O(h^1) in stress, O(h^2) in displacement |
| Linear hex (8-node)       | O(h^2)                         |
| Quadratic tet (10-node)   | O(h^2) in stress, O(h^3) in displacement |
| Quadratic hex (20-node)   | O(h^3)                         |

A solver that does not show theoretical convergence is either (a) buggy or (b) using a different formulation than declared. Either way, do not ship.

Convergence studies are run on a 4- or 5-mesh sequence and reported as a log-log plot in the validation report (`tests/validation/<solver>/convergence-report.html`).

### Level 4 — Cross-solver validation

For at least one realistic problem, the solver must agree (within tolerance) with at least one other production-quality solver:

| In-tree                       | Compared against                       | Tolerance              |
| ----------------------------- | -------------------------------------- | ---------------------- |
| `solver.elasticity.linear`    | `solver.elasticity.fenicsx`            | 1e-6 relative          |
| `solver.elasticity.fenicsx`   | Code_Aster on the same problem         | 1 % on key quantities  |
| `solver.heat.linear`          | FEniCSx heat equation example          | 1e-5 relative          |
| (CFD) OpenFOAM `simpleFoam`   | Published benchmark (e.g. Ercoftac)    | per-benchmark          |

Cross-solver validation catches systematic bias that analytical and patch tests miss.

## Workflow for a new solver

1. **Implement the solver as a plugin** following `developing-souxmar-plugin`.
2. **Write Level 1 tests** before iterating on the implementation. Tests should fail; they prove the harness works.
3. **Make Level 1 pass.** Then Level 2. Then Level 3. Then Level 4.
4. **Generate the validation report** — a self-contained HTML page with: problem statement, analytical solution, computed solution, error metrics, convergence plot.
5. **Ship the report** alongside the solver. The plugin marketplace surfaces it.

## Workflow for a modified solver

1. **Run the validation suite at HEAD-1** to capture baseline numbers.
2. **Make the change.**
3. **Run the validation suite at HEAD.**
4. **Compare:** any error metric that gets worse (even within tolerance) requires explanation in the PR description. Worse-but-tolerable is acceptable; surprises are not.

## Workflow for investigating a suspicious result

1. **Reproduce on a smaller problem** if possible. Hand-checkable geometry is best.
2. **Refine the mesh once.** If the result changes by > 5 %, you are not mesh-converged — refine further until convergence.
3. **Cross-check against the alternative solver** if one exists for the problem.
4. **Inspect the BC application.** A surprising fraction of "wrong stress" reports are misapplied BCs.
5. **Inspect the material model.** Units mismatch (Pa vs MPa) is a top-3 cause.
6. **Inspect the element type.** Linear tets are notoriously stiff; switching to quadratic or hex often resolves "stiff response" reports.
7. **File a validation issue** with reproducer.

## Tolerances and units

- The data model is unit-agnostic; the units are whatever the user provides.
- Validation tests are written in **SI** and stated explicitly (`E = 210e9 Pa`, `nu = 0.3`, `length in metres`).
- Tolerances are stated as relative error (`< 1e-6` or `< 0.5 %`) — never absolute, because absolute is unit-dependent.
- Tests checking unit-handling (e.g. ensuring the user can use mm/MPa as long as they are consistent) live separately and explicitly.

## Cross-platform numerical determinism

Per the determinism gate (`auditing-determinism` skill), solver output must be byte-identical across Linux/macOS/Windows for the same input. Floating-point nondeterminism is a real risk — particularly with parallel reductions. Mitigations:

- Use deterministic reduction orderings in PETSc / Eigen (configurable; we set the deterministic flag).
- Avoid `std::accumulate` over thread-local partial sums without a stable reduction.
- Pin BLAS/LAPACK to a deterministic implementation.

## Common mistakes

- Validating only with the same solver's previous version. That catches regressions, not bugs that have always been there.
- Convergence studies that only report at one mesh size. The shape of the convergence curve is the actual finding.
- Reporting errors in displacement (which is forgiving) instead of stress (which is the engineer's primary output).
- Tolerating Level 4 cross-solver disagreement on an "interpretation difference." If two solvers disagree, find out why before shipping.
- Skipping the validation report because "it's just a fix." A fix changes numerics; the report exists to detect that.

## Reference

- `docs/ENGINEERING_PRACTICES.md` — quality bar, perf budgets.
- `tests/validation/` — existing validation tests as worked examples.
- `examples/cantilever-beam/` — the canonical Level-1 case for elasticity.
- Comparable-project precedent: NASA's MSC Nastran benchmark suite, NAFEMS benchmarks.
