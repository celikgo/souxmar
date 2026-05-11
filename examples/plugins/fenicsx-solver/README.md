# fenicsx-solver

Opt-in DOLFINx-backed Poisson solver. Registers `solver.heat.fenicsx`. The validation-grade companion to the in-tree `solver.heat.linear` (Sprint 5 push 3 heat-solver, which is closed-form / analytical).

## Build prerequisites

This plugin is gated behind two configure-time conditions:

1. `cmake -DSOUXMAR_WITH_FENICSX=ON`
2. `find_package(DOLFINX)` succeeds. On Debian / Ubuntu: `apt install fenics-dolfinx-real`. On macOS / conda: `conda install -c conda-forge fenics-dolfinx`.

If DOLFINx is missing, the plugin's CMakeLists `return()`s with a clean STATUS skip — no noisy linker errors. Default-CI builds skip this directory entirely (the option defaults `OFF`).

## The FFCx form-generation step

DOLFINx's bilinear form is described in UFL (Unified Form Language, Python). The C++ adapter links against a C file FFCx compiles from that description. We ship `poisson.py` as the canonical source; you need to run `ffcx` once and commit the output before the plugin will build:

```bash
pip install fenics-ffcx fenics-basix fenics-ufl
ffcx --output-directory examples/plugins/fenicsx-solver \
     examples/plugins/fenicsx-solver/poisson.py
```

That emits `poisson.c` and `poisson.h`. The CMakeLists adds `poisson.c` to the plugin's compile list. **`poisson.c` is not committed to this repo** — it's a machine-generated artifact whose contents track the FFCx version a developer happens to have installed. The README points at the regen step; CI nightly runs ffcx as part of the FEniCSx-bearing build matrix.

If you flip `SOUXMAR_WITH_FENICSX=ON` without running FFCx first, the plugin's CMakeLists emits a STATUS message describing exactly the command to run.

## What it solves

The Poisson equation with homogeneous Dirichlet BCs:

```
-∆u = f      in Ω
   u = 0     on ∂Ω
```

Inputs (souxmar pipeline stage input bag):

- `mesh: { from: <mesher_stage> }` — required Tet4 mesh.
- `source_term` — scalar `f`, default 1.0.

Output: nodal scalar `Field` ("temperature") — the Poisson solution.

## Validation

The Sprint 7 push 2 always-on counterpart `solver.heat.linear` (analytical closed form) and this plugin should agree to within FEM discretisation error on the same problem. The `validating-solver` skill walks through the agreement test — it's the patch-test pattern called out in `docs/ENGINEERING_PRACTICES.md`. The Sprint 8 agent eval suite (push 4 of this sprint) includes a "compare FEniCSx vs analytical heat solution" task whose pass criterion is `||u_fenicsx - u_analytical|| / ||u_analytical|| < 1e-2`.

## Limitations (v1)

- Poisson only. Linear-elasticity needs additional UFL forms (`elasticity.py`) and another FFCx pass. The Sprint 7 push 2 ships `elasticity-stub` as the always-on elasticity surface; the real DOLFINx elasticity adapter lands in Sprint 8 alongside the OpenFOAM CFD work.
- Homogeneous Dirichlet only. The full BC manifest from `set_bc` lands when the `solver.*` C ABI carries a structured BC array. That's an additive minor ratchet event tracked for Sprint 8 (ADR-0008 compliant — no v1 break).
- Single MPI rank. Distributed solve lands with the out-of-core / parallel mesh work in Sprint 8+.
