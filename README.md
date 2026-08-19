# souxmar

An open-source CAE platform: parametric CAD, mesh generation, FEM and CFD,
post-processing — wrapped in a cross-platform desktop app with an agentic AI
chat that can drive the entire pipeline. C++20 core, Python bindings, stable
C plugin ABI, Tauri + React desktop app, Apache 2.0.

![LPBF melt pool computed by solver.am.thermal.lpbf](docs/img/rosenthal-melt-pool.png)

<sub>Above: the Rosenthal temperature field and melt-pool isotherm computed by
the in-tree `am-thermal` plugin at its default 316L / 200 W / 0.8 m/s
parameters. Regenerate it with `python3 scripts/gen-physics-figures.py`.</sub>

For mechanical, structural, aerospace, manufacturing and marine engineers who
want a Cursor-style experience for simulation work: open the app, describe the
problem in chat, watch it mesh and solve, inspect results in a built-in
viewport.

The strongest part of the repository is the physics.
**[`docs/PHYSICS.md`](docs/PHYSICS.md)** documents every model in it — the
governing equation, the literature citation, the validity envelope, and the
direction and magnitude of its known error. If you only read one page, read
that one.

<!-- version --> `v0.9.0`

## What this is NOT

souxmar does not replace FreeCAD, Gmsh, FEniCSx, OpenFOAM, Blender or
ParaView. It unifies them under a shared data model and a stable plugin ABI,
then puts a modern UI and an agentic AI on top.

More specifically, and more importantly:

- **There is no CAD kernel.** `include/souxmar-c/brep.h` and `sketch.h` are
  ABI surface only; the in-core backing returns `NOT_IMPLEMENTED` from every
  operation and no `cad.*` plugin exists here. Parametric modelling, the
  feature tree and the 2D sketcher are designed
  ([RFC-0003](docs/rfcs/0003-cad-kernel.md),
  [RFC-0005](docs/rfcs/0005-feature-tree.md)) and unbuilt.
- **The always-on solvers are demonstration stubs.** `solver.heat.linear`,
  `solver.elasticity.linear`, `solver.modal.linear` and `solver.cfd.simple`
  return closed-form fields so that the pipeline, the examples and the agent
  evals have something runnable in the default CI matrix. They are not FEM
  and not CFD — no stiffness matrix is ever assembled. The one real
  discretised solve, `solver.heat.fenicsx` (DOLFINx + PETSc), is behind an
  opt-in build flag.
- **The manufacturing and marine models are closed-form and heuristic.**
  Every one cites its source and states what it ignores. They are screening
  and preliminary-sizing aids, not calibrated process simulations. The marine
  qualification dossier is advisory: souxmar is not a classification society
  and issues no approval.
- **The viewport does not render yet.** The desktop app's 3D panel is
  scaffolded; the renderer behind it is
  [RFC-0001](docs/rfcs/0001-viewport-renderer.md) and unbuilt.
- **The Pro-tier services are scaffolds**, not deployments. `services/`
  contains API shapes and handlers with nothing running behind them. Every
  `*.souxmar.invalid` hostname in this repository is a placeholder on the
  RFC 6761 reserved TLD, chosen so it cannot be mistaken for a live endpoint.

[`docs/CAPABILITIES.md`](docs/CAPABILITIES.md) states this per capability,
and is generated from the tree rather than written by hand.

## Status

**Pre-1.0, single maintainer, actively developed.** `v0.9.0` is the first
tagged release; everything below is checkable from a clone.

| | |
| --- | --- |
| Version | `v0.9.0` — see [`VERSION`](VERSION), [`CHANGELOG.md`](CHANGELOG.md) |
| Licence | Apache-2.0 |
| Plugin C ABI | v1 major **frozen** ([ADR-0008](docs/adr/0008-abi-v1-final-freeze.md)); minor at v1.9, ratcheted additively |
| Agent tool contract | v1 **frozen** ([ADR-0011](docs/adr/0011-tool-contract-v1-final-freeze.md)); 24 tools |
| In-tree plugins | 25, providing 38 capabilities |
| Tests | 740 gtest cases across 67 files |
| C/C++ | ~66,800 tracked lines |
| Design record | 45 ADRs, 9 RFCs |
| CI | `CI`, `Security` and `Visual regression` workflows on every push |

What is **not** true, and has been claimed here before: there is no stable
1.0, no PyPI package, no hosted service, and no team. If you find a statement
on this page you cannot verify with `git`, `gh` and a browser, that is a bug —
please [open an issue](../../issues/new).

## 60-second quickstart

```bash
git clone https://github.com/celikgo/souxmar.git
cd souxmar
export VCPKG_ROOT="$HOME/vcpkg"        # https://github.com/microsoft/vcpkg

cmake --preset dev && cmake --build --preset dev
```

Then run a real pipeline — read an STL, compute per-cell mesh quality, write
a ParaView file:

```bash
cd examples/stl-cube
../../build/dev/src/cli/souxmar run pipeline.yaml \
  --plugin-path ../../build/dev/examples/plugins
# -> cube.vtu
```

The manufacturing chain end to end — layered build mesh, LPBF thermal
history, melt-pool porosity risk, inherent-strain distortion, residual
stress, overhang check, three ParaView files and two Markdown build reports:

```bash
souxmar run examples/am-lpbf-bracket/pipeline.yaml \
  --plugin-path build/dev/examples/plugins
```

Read [`examples/am-lpbf-bracket/README.md`](examples/am-lpbf-bracket/README.md)
first — it is explicit about what that pipeline does and does not model.

## Surfaces

- **Desktop app** (macOS / Windows / Linux) — chat panel, pipeline editor,
  inspector, and a viewport panel whose renderer is not built yet.
- **CLI** (`souxmar`) — for CI, batch runs, scripting. This is the surface
  that works today.
- **Python** (`pysouxmar`) — **not on PyPI.** `pip install pysouxmar` does not
  work and never has. Build the bindings from source with the `dev-python`
  preset:
  ```bash
  cmake --preset dev-python && cmake --build --preset dev-python
  ```
- **Plugin SDK** — stable C ABI for shipping your own meshers, solvers,
  elements, readers and writers as out-of-tree binaries. See
  [`docs/PLUGIN_SDK.md`](docs/PLUGIN_SDK.md).

## Capabilities

The full table — every capability id, whether it is implemented, tested,
a stub, or planned, what kind of model sits behind it, and which test covers
it — is in **[`docs/CAPABILITIES.md`](docs/CAPABILITIES.md)**. That file is
generated by [`scripts/gen-capability-table.py`](scripts/gen-capability-table.py)
from the plugin manifests, the CMake build wiring and the test sources, and CI
fails if it drifts.

Summary of the 25 in-tree plugins:

| Plugin | Capabilities | Notes |
| --- | --- | --- |
| `hello-mesher`, `hello-writer` | `mesher.tetra.hello`, `writer.text-summary` | Minimal ABI references |
| `grid-mesher` | `mesher.tetra.grid` | Structured grid over the bounding box |
| `gmsh-mesher` | `mesher.tetra.gmsh` | Opt-in, real conforming mesher |
| `stl-reader`, `obj-reader` | `reader.stl`, `reader.obj` | Always-on surface readers |
| `occt-reader` | `reader.step`, `reader.iges` | Opt-in, OpenCASCADE |
| `blender-reader` | `reader.blend` | Opt-in, Blender subprocess |
| `vtu-writer` | `writer.vtu` | ParaView output, no VTK linkage |
| `heat-solver`, `elasticity-stub`, `modal-stub`, `cfd-stub` | `solver.heat.linear`, `solver.elasticity.linear`, `solver.modal.linear`, `solver.cfd.simple` | **Closed-form demonstration stubs, not FEM/CFD** |
| `fenicsx-solver` | `solver.heat.fenicsx` | Opt-in — the one real discretised solve |
| `openfoam-solver` | `solver.cfd.openfoam.{simple,pimple,inter}` | Opt-in, out-of-process |
| `mesh-quality`, `scalar-magnitude` | `postproc.mesh_quality`, `postproc.scalar_magnitude` | Post-processing |
| `am-layered-mesher`, `lattice-reader` | `mesher.am.layered`, `reader.lattice` | AM geometry |
| `am-thermal` | `solver.am.thermal.lpbf`, `postproc.am.melt_pool` | Rosenthal (1946) — [PHYSICS §1–2](docs/PHYSICS.md) |
| `am-distortion` | `solver.am.distortion.inherent_strain`, `postproc.am.residual_stress` | Keller & Ploshikhin (2014) — [PHYSICS §3](docs/PHYSICS.md) |
| `am-polymer` | `solver.am.polymer.fff`, `postproc.am.bond_strength` | FFF interlayer bonding — [PHYSICS §4](docs/PHYSICS.md) |
| `am-manufacturability` | `solver.am.{overhang,printability,buildtime}` | DfAM checks — [PHYSICS §5](docs/PHYSICS.md) |
| `am-slicer` | `writer.am.{gcode,cli,report}` | Planar slicing and build reports |
| `marine` | `solver.marine.{hydrostatic,hull_collapse,corrosion}`, `writer.marine.qualification_report` | Windenburg–Trilling, PREN/CPT — [PHYSICS §6–8](docs/PHYSICS.md) |

## Building

Prerequisites: CMake ≥ 3.25, Ninja, a C++20 compiler (GCC 13 / Clang 17 /
AppleClang / MSVC 19.36+), and [vcpkg](https://github.com/microsoft/vcpkg)
cloned with `VCPKG_ROOT` exported.

On macOS, vcpkg builds `libsodium` through autotools, so you also need
`brew install autoconf autoconf-archive automake libtool`. Without them the
first `cmake --preset dev` fails inside the vcpkg port build rather than in
souxmar's own configure, which makes the cause easy to misread.

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
```

Other presets (see [`CMakePresets.json`](CMakePresets.json)): `dev-python`,
`ci-linux-gcc`, `ci-linux-clang`, `ci-macos`, `ci-windows`, `asan`, `tsan`.

The first `cmake --preset` builds vcpkg dependencies from source (~5 minutes
for the default feature set, longer with heavy adapters enabled). Later runs
use the vcpkg binary cache.

## How it's funded (open-core)

The library, plugin SDK, CLI, Python bindings **and the desktop app** are
Apache-2.0 — no crippled community edition. Optional managed services are
intended to be commercial; none of them is running today. You bring your own
Anthropic / OpenAI / local-Ollama key and your own compute. See
[`docs/BUSINESS_MODEL.md`](docs/BUSINESS_MODEL.md).

## Documents

**Start here**

- [`docs/PHYSICS.md`](docs/PHYSICS.md) — every model, its citation, its
  validity envelope, and its known error
- [`docs/CAPABILITIES.md`](docs/CAPABILITIES.md) — generated capability table
- [`docs/VISION.md`](docs/VISION.md) — purpose, target users, scope, non-goals
- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — layers, data model,
  dependencies

**Reference**

- [`docs/PLUGIN_SDK.md`](docs/PLUGIN_SDK.md) — C ABI, plugin lifecycle,
  versioning
- [`docs/AI_INTEGRATION.md`](docs/AI_INTEGRATION.md) — agentic chat, BYOK
  credentials, tool surface
- [`docs/MANUFACTURING.md`](docs/MANUFACTURING.md) ·
  [`docs/MARINE.md`](docs/MARINE.md) — capability inputs and model limits
- [`docs/DESKTOP_APP.md`](docs/DESKTOP_APP.md) ·
  [`docs/UI_DESIGN.md`](docs/UI_DESIGN.md) — desktop stack and design system
- [`docs/adr/`](docs/adr/) · [`docs/rfcs/`](docs/rfcs/) — decision records

**Process**

- [`CONTRIBUTING.md`](CONTRIBUTING.md) · [`SECURITY.md`](SECURITY.md) ·
  [`docs/GOVERNANCE.md`](docs/GOVERNANCE.md) ·
  [`docs/ROADMAP.md`](docs/ROADMAP.md)
- [`docs/HISTORY.md`](docs/HISTORY.md) — how the project got here, and an
  honest note about the shape of its commit history

## License

Apache License 2.0. See [`LICENSE`](LICENSE).
