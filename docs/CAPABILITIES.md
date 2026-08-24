<!-- GENERATED FILE — DO NOT EDIT.
     Regenerate with: python3 scripts/gen-capability-table.py
     CI runs the same script with --check and fails when this is stale. -->

# Capabilities

Everything souxmar can actually do, read out of the tree rather than
written down: the ids come from the plugin manifests, the default-build
column from `examples/CMakeLists.txt`, and the test column from grepping
`tests/integration/` for each id.

**How to read the state column.**

| State | Means |
| --- | --- |
| `implemented and tested` | Does what its id says, and a test in `tests/integration/` runs it. |
| `implemented, untested` | Does what its id says; nothing in `tests/integration/` covers it. |
| `stub` | Registers the id and returns a plausible field so the pipeline runs end to end. Not the computation the id implies. |
| `planned` | Id is reserved; no implementation. |

A test naming a capability id in `tests/unit/test_manifest.cpp` or
`tests/unit/test_plugin_index.cpp` does **not** count as coverage here —
those tests feed ids to a parser, they do not run the code behind them.

The `Model` column is the honest one: most of these are closed-form or
heuristic engineering models with a cited source, not discretised solvers.
See [`PHYSICS.md`](PHYSICS.md) for the equations and validity envelopes.

## Plugin capabilities

| Capability | Plugin | State | Model | Default build | Covering test |
| --- | --- | --- | --- | --- | --- |
| `postproc.am.residual_stress` | `am-distortion` | implemented and tested | Elastic read-back of the locked-in strain from the distortion stage; re-runnable against a different yield strength without re-running the mechanics. | always-on | `tests/integration/test_am_distortion.cpp` |
| `solver.am.distortion.inherent_strain` | `am-distortion` | implemented and tested | Inherent-strain distortion accumulated layer by layer. The strain magnitude is a hand-entered calibration constant, not a prediction. | always-on | `tests/integration/test_am_distortion.cpp` |
| `mesher.am.layered` | `am-layered-mesher` | implemented and tested | Layer-aligned Hex8 voxel grid perpendicular to the build direction; stamps cell tag = layer index, which every other AM capability reads. | always-on | `tests/integration/test_am_distortion.cpp`<br>`tests/integration/test_am_meshers.cpp`<br>`tests/integration/test_am_thermal.cpp` |
| `solver.am.buildtime` | `am-manufacturability` | implemented and tested | Per-layer cross-sectional area and build time. | always-on | `tests/integration/test_am_manufacturability.cpp`<br>`tests/integration/test_am_slicer.cpp` |
| `solver.am.overhang` | `am-manufacturability` | implemented and tested | Per-cell downskin angle, overhang classification and support need, from extracted boundary faces. | always-on | `tests/integration/test_am_manufacturability.cpp` |
| `solver.am.printability` | `am-manufacturability` | implemented and tested | Composite DfAM printability score. The weighting is a documented choice, not a measured threshold. | always-on | `tests/integration/test_am_manufacturability.cpp` |
| `postproc.am.bond_strength` | `am-polymer` | implemented and tested | Polymer healing law applied to that interface thermal history. | always-on | `tests/integration/test_am_polymer.cpp` |
| `solver.am.polymer.fff` | `am-polymer` | implemented and tested | FFF/FDM interlayer thermal cycling — the temperature history at the road interface, which is what sets Z-direction strength. | always-on | `tests/integration/test_am_polymer.cpp` |
| `writer.am.cli` | `am-slicer` | implemented and tested | The same slice written as Common Layer Interface (CLI) ASCII. | always-on | `tests/integration/test_am_slicer.cpp` |
| `writer.am.gcode` | `am-slicer` | implemented and tested | Planar slicing (boundary extraction, plane intersection, contour chaining, hole classification, deterministic ordering) to FFF/FDM G-code. | always-on | `tests/integration/test_am_slicer.cpp` |
| `writer.am.report` | `am-slicer` | implemented and tested | Markdown build report / traveller sheet. | always-on | `tests/integration/test_am_slicer.cpp` |
| `postproc.am.melt_pool` | `am-thermal` | implemented and tested | Melt-pool depth by bracketed bisection, width by a 1024-point scan plus ternary search, keyhole onset by normalised enthalpy (King 2014). Fixed iteration counts, for bit-identical results across platforms. | always-on | `tests/integration/test_am_thermal.cpp` |
| `solver.am.thermal.lpbf` | `am-thermal` | implemented and tested | Rosenthal (1946) moving point source in its surface (2*pi) form, layer by layer. Under-predicts LPBF melt-pool depth by 1.5-2x; calibrate absorptivity first. | always-on | `tests/integration/test_am_thermal.cpp` |
| `reader.blend` | `blender-reader` | implemented, untested | Runs Blender as a subprocess to export .blend to OBJ, then parses that. No test in tests/integration/ because the default CI matrix does not install Blender. | opt-in (`SOUXMAR_WITH_BLENDER`) | — none — |
| `solver.cfd.simple` | `cfd-stub` | stub | Uniform velocity field with per-patch inlet/wall/outlet routing. **Not Navier-Stokes**: nothing is advected, nothing is solved. | always-on | `tests/integration/test_cfd_stub.cpp` |
| `solver.elasticity.linear` | `elasticity-stub` | stub | Closed-form uniaxial tension along x — the analytical bar solution a real solver should converge to. **Not FEM**: ignores boundary conditions, mesh stiffness and cell tags. `solver.elasticity.fem` is the real one, and is tested against this closed form. | always-on | `tests/integration/test_elasticity_stub.cpp`<br>`tests/integration/test_fem_elasticity.cpp` |
| `solver.elasticity.fem` | `fem-elasticity` | implemented and tested | Genuine small-strain linear isotropic FEM: isoparametric Tet4 (one-point) and Hex8 (2x2x2 Gauss), assembled to CSR, Dirichlet by symmetric elimination, consistent Neumann tractions, Jacobi-preconditioned CG. Passes the constant-strain patch test exactly on both elements. Trilinear hexes shear-lock in bending and both elements lock as nu approaches 0.5; no stress output yet. | always-on | `tests/integration/test_fem_elasticity.cpp` |
| `solver.heat.fenicsx` | `fenicsx-solver` | implemented, untested | The only discretised FEM solve in the tree: assembles and solves the Poisson problem with DOLFINx and PETSc. No test in tests/integration/ because the default CI matrix does not install DOLFINx. | opt-in (`SOUXMAR_WITH_FENICSX`) | — none — |
| `mesher.tetra.gmsh` | `gmsh-mesher` | implemented, untested | Drives the Gmsh C++ API for a real conforming tetrahedral mesh. No test in tests/integration/ because the default CI matrix does not install Gmsh. | opt-in (`SOUXMAR_WITH_GMSH`) | — none — |
| `mesher.tetra.grid` | `grid-mesher` | implemented and tested | Structured NxNxN grid over the geometry's bounding box, Kuhn-decomposed into 6 positively-oriented tetrahedra per hex. Does not conform to the geometry — it meshes the box, not the shape. | always-on | `tests/integration/test_cfd_stub.cpp`<br>`tests/integration/test_elasticity_stub.cpp`<br>`tests/integration/test_fem_elasticity.cpp`<br>`tests/integration/test_swap_mesher.cpp` |
| `solver.heat.linear` | `heat-solver` | stub | Closed-form T = T_steady(1 - exp(-t/tau)) times a cosine profile over the bounding box. **Not FEM**: no conduction, no boundary conditions, no stiffness, no mesh dependence. | always-on | `tests/integration/test_mesh_quality_plugin.cpp`<br>`tests/integration/test_postproc_end_to_end.cpp` |
| `mesher.tetra.hello` | `hello-mesher` | stub | Emits one unit tetrahedron and ignores the input geometry entirely. Exists to exercise the C ABI mesh handles. | always-on | `tests/integration/test_c_bridge_pipeline.cpp`<br>`tests/integration/test_cfd_stub.cpp`<br>`tests/integration/test_load_hello_mesher.cpp`<br>`tests/integration/test_mesh_quality_plugin.cpp`<br>`tests/integration/test_pipeline_end_to_end.cpp`<br>`tests/integration/test_postproc_end_to_end.cpp` |
| `writer.text-summary` | `hello-writer` | implemented and tested | Two-line text summary of a mesh. Reference writer for the ABI. | always-on | `tests/integration/test_pipeline_end_to_end.cpp` |
| `reader.lattice` | `lattice-reader` | implemented and tested | Parametric strut lattice (cubic/bcc/fcc/octet/diamond) emitted as Edge2 beams. A reader rather than a mesher because the v1 mesher ABI passes no value bag. | always-on | `tests/integration/test_am_meshers.cpp` |
| `solver.marine.corrosion` | `marine` | implemented and tested | PREN/CPT pitting, oxygen-limited galvanic coupling and sqrt(t) pit growth from ASM Handbook Vol. 13A, LaQue (1975), DNV-RP-B401 and Melchers (2003). Indicative rates for comparing alloys, not design data. | always-on | `tests/integration/test_marine.cpp` |
| `solver.marine.hull_collapse` | `marine` | implemented and tested | Windenburg-Trilling (1934) interframe instability, membrane yield, Bresse-Levy long-cylinder and Zoelly (1915) sphere buckling. Modes are dropped, not clamped, when the geometry leaves their validity bracket. **Not a classification-society calculation.** | always-on | `tests/integration/test_marine.cpp` |
| `solver.marine.hydrostatic` | `marine` | implemented and tested | Nodal seawater pressure per depth load case, density from the EOS-80 one-atmosphere correlation (Millero & Poisson 1981). Pressure only — no stress, no deflection. | always-on | `tests/integration/test_marine.cpp` |
| `writer.marine.qualification_report` | `marine` | implemented and tested | Markdown AM qualification dossier. Advisory checklist derived from public practice — souxmar is not a classification society and issues no approval. | always-on | `tests/integration/test_marine.cpp` |
| `postproc.mesh_quality` | `mesh-quality` | implemented and tested | Per-cell signed volume, edge ratio, Jacobian, aspect ratio and skew. | always-on | `tests/integration/test_mesh_quality_plugin.cpp` |
| `solver.modal.linear` | `modal-stub` | stub | Closed-form Euler-Bernoulli cantilever; the bounding-box x-span is taken as the beam length. **Not an eigensolve** — no mass or stiffness matrix is ever formed. | always-on | — none — |
| `reader.obj` | `obj-reader` | implemented and tested | Wavefront OBJ parsed to a Tri3 surface mesh. | always-on | `tests/integration/test_obj_reader.cpp` |
| `reader.iges` | `occt-reader` | implemented, untested | IGES through OpenCASCADE. Same CI caveat as reader.step. | opt-in (`SOUXMAR_WITH_OPENCASCADE`) | — none — |
| `reader.step` | `occt-reader` | implemented, untested | STEP through OpenCASCADE, entity topology preserved. No test in tests/integration/ because the default CI matrix does not install OCCT. | opt-in (`SOUXMAR_WITH_OPENCASCADE`) | — none — |
| `solver.cfd.openfoam.inter` | `openfoam-solver` | implemented, untested | As above, two-phase VOF `interFoam`. | opt-in (`SOUXMAR_WITH_OPENFOAM`) | — none — |
| `solver.cfd.openfoam.pimple` | `openfoam-solver` | implemented, untested | As above, transient `pimpleFoam`. | opt-in (`SOUXMAR_WITH_OPENFOAM`) | — none — |
| `solver.cfd.openfoam.simple` | `openfoam-solver` | implemented, untested | Generates an OpenFOAM case and execs `simpleFoam` out of process per ADR-0009; never links libOpenFOAM. No test in tests/integration/ because the default CI matrix does not install OpenFOAM. | opt-in (`SOUXMAR_WITH_OPENFOAM`) | — none — |
| `postproc.scalar_magnitude` | `scalar-magnitude` | implemented and tested | Reduces a field of any component count to a scalar magnitude, preserving location and time steps. | always-on | `tests/integration/test_postproc_end_to_end.cpp` |
| `reader.stl` | `stl-reader` | implemented and tested | ASCII STL parsed to a Tri3 surface mesh. | always-on | `tests/integration/test_reader_end_to_end.cpp` |
| `writer.vtu` | `vtu-writer` | implemented and tested | Hand-emits ParaView-readable VTK XML UnstructuredGrid (ASCII) without linking VTK. | always-on | `tests/integration/test_c_bridge_pipeline.cpp`<br>`tests/integration/test_cli_smoke.cpp`<br>`tests/integration/test_obj_reader.cpp`<br>`tests/integration/test_reader_end_to_end.cpp`<br>`tests/integration/test_vtu_consumer_conformance.cpp` |

39 capabilities across 26 in-tree plugins.

## Agent tools

The agent tool contract is v1 FINAL; the catalogue below is assembled by
`default_v1_tools()` in `src/ai/tools/default_registry.cpp` and read here
from the `t.name` assignments in `src/ai/tools/*.cpp`.

| Tool | Unit test | Eval |
| --- | --- | --- |
| `apply_hydrostatic_load` | `tests/unit/test_ai_tools.cpp` | `evals/v1/marine-01-hydrostatic.yaml`<br>`evals/v1/marine-02-integrity.yaml` |
| `apply_inlet` | `tests/unit/test_ai_tools.cpp` | `evals/v1/cfd-01-propose-inlet-wall-outlet.yaml`<br>`evals/v1/cfd-02-validate-bcs-clean.yaml`<br>`evals/v1/cfd-03-validate-bcs-missing-outlet.yaml` |
| `apply_outlet` | `tests/unit/test_ai_tools.cpp` | `evals/v1/cfd-01-propose-inlet-wall-outlet.yaml`<br>`evals/v1/cfd-02-validate-bcs-clean.yaml` |
| `apply_pipeline_diff` | `tests/unit/test_ai_tools.cpp` | `evals/v1/diff-01-add-stage.yaml`<br>`evals/v1/diff-02-dangling-rejected.yaml`<br>`evals/v1/diff-03-noop-stage.yaml` |
| `apply_wall` | `tests/unit/test_ai_tools.cpp` | `evals/v1/cfd-01-propose-inlet-wall-outlet.yaml`<br>`evals/v1/cfd-02-validate-bcs-clean.yaml`<br>`evals/v1/cfd-03-validate-bcs-missing-outlet.yaml` |
| `check_marine_integrity` | `tests/unit/test_ai_tools.cpp` | `evals/v1/marine-02-integrity.yaml` |
| `check_printability` | `tests/unit/test_ai_tools.cpp` | `evals/v1/am-02-printability.yaml` |
| `compute_field` | `tests/unit/test_ai_tools.cpp` | `evals/v1/postproc-01-scalar-magnitude.yaml`<br>`evals/v1/postproc-02-missing-capability.yaml`<br>`evals/v1/postproc-03-no-field.yaml` |
| `estimate_build_cost` | `tests/unit/test_ai_tools.cpp` | `evals/v1/am-03-build-cost.yaml` |
| `export_results` | `tests/unit/test_ai_tools.cpp` | `evals/v1/export-01-vtu.yaml`<br>`evals/v1/export-02-missing-format.yaml`<br>`evals/v1/multistep-01-full-pipeline.yaml` |
| `list_plugins` | `tests/unit/test_ai_tools.cpp` | `evals/v1/listing-01-list-plugins.yaml`<br>`evals/v1/listing-02-list-plugins-empty.yaml` |
| `mesh` | `tests/unit/test_ai_tools.cpp` | `evals/v1/am-02-printability.yaml`<br>`evals/v1/am-03-build-cost.yaml`<br>`evals/v1/cfd-02-validate-bcs-clean.yaml`<br>+31 more |
| `propose_am_setup` | `tests/unit/test_ai_tools.cpp` | `evals/v1/am-01-propose-lpbf.yaml`<br>`evals/v1/am-02-printability.yaml`<br>`evals/v1/am-03-build-cost.yaml` |
| `propose_cfd_setup` | `tests/unit/test_ai_tools.cpp` | `evals/v1/cfd-01-propose-inlet-wall-outlet.yaml` |
| `propose_pipeline` | `tests/unit/test_ai_tools.cpp` | `evals/v1/pipeline-01-propose-valid.yaml`<br>`evals/v1/pipeline-02-propose-invalid.yaml` |
| `query_field` | `tests/unit/test_ai_tools.cpp` | `evals/v1/multistep-01-full-pipeline.yaml`<br>`evals/v1/query-01-field-after-heat.yaml`<br>`evals/v1/query-02-field-no-field.yaml`<br>+2 more |
| `query_mesh_quality` | `tests/unit/test_ai_tools.cpp` | `evals/v1/multistep-03-mesh-then-quality.yaml`<br>`evals/v1/quality-01-hello-mesh.yaml`<br>`evals/v1/quality-02-no-mesh.yaml`<br>+2 more |
| `read_geometry_summary` | `tests/unit/test_ai_tools.cpp` | `evals/v1/read-01-cube-geometry.yaml`<br>`evals/v1/read-02-tetrahedron-geometry.yaml`<br>`evals/v1/read-03-missing-geometry.yaml`<br>+1 more |
| `screenshot_viewport` | `tests/unit/test_ai_tools.cpp` | `evals/v1/screenshot-01-after-mesh.yaml` |
| `set_bc` | `tests/unit/test_ai_tools.cpp` | `evals/v1/bc-01-dirichlet-clamp.yaml`<br>`evals/v1/bc-02-neumann-flux.yaml`<br>`evals/v1/bc-03-robin-rejected.yaml`<br>+4 more |
| `set_build_orientation` | `tests/unit/test_ai_tools.cpp` | — none — |
| `set_material` | `tests/unit/test_ai_tools.cpp` | `evals/v1/export-01-vtu.yaml`<br>`evals/v1/material-01-linear-elastic.yaml`<br>`evals/v1/material-02-thermal.yaml`<br>+2 more |
| `solve` | `tests/unit/test_ai_tools.cpp` | `evals/v1/export-01-vtu.yaml`<br>`evals/v1/mesh-04-hello-stashes-handle.yaml`<br>`evals/v1/multistep-01-full-pipeline.yaml`<br>+10 more |
| `validate_bcs` | `tests/unit/test_ai_tools.cpp` | `evals/v1/cfd-02-validate-bcs-clean.yaml`<br>`evals/v1/cfd-03-validate-bcs-missing-outlet.yaml` |

24 tools.

## What is deliberately not here

- **No CAD kernel.** `include/souxmar-c/brep.h` and `sketch.h` are ABI surface only. The in-core backing in `src/core/c_abi_brep.cpp` returns `NOT_IMPLEMENTED` from every operation, and no `cad.*` plugin exists in this repository. Parametric modelling, the feature tree and the 2D sketcher are designed (RFC-0003, RFC-0005) and unbuilt.
- **No topology optimisation**, and no calibrated, mesh-resolved AM process solver. Both are named as future work, neither is started.
- **No viewport rendering.** The desktop app's 3D viewport is scaffolded; the Three.js/VTK.js renderer behind it is RFC-0001 and unbuilt.
- **The Pro-tier services are scaffolds.** `services/` contains API shapes and handlers with no deployment behind them; every `*.souxmar.invalid` hostname in this repository is a placeholder on the RFC 6761 reserved TLD, chosen so that nothing here can be mistaken for a running endpoint.
