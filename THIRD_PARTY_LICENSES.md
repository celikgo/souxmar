# Third-Party Licenses

souxmar is licensed under [Apache License 2.0](LICENSE). It depends on third-party libraries, each under its own license. This file enumerates every direct build-time and runtime dependency and the license it ships under.

CI verifies that every dependency in `vcpkg.json` is represented here; new entries that lack an entry here fail the license-scan job.

## Direct dependencies (vcpkg manifest)

### Always linked

| Dependency      | Version (≥) | License                          | Upstream                                      |
| --------------- | ----------- | -------------------------------- | --------------------------------------------- |
| `fmt`           | 10.2.1      | MIT                              | https://github.com/fmtlib/fmt                 |
| `spdlog`        | 1.13.0      | MIT                              | https://github.com/gabime/spdlog              |
| `tomlplusplus`  | 3.4.0       | MIT                              | https://github.com/marzer/tomlplusplus        |
| `yaml-cpp`      | 0.8.0       | MIT                              | https://github.com/jbeder/yaml-cpp            |

### Test feature (`tests`, default-on)

| Dependency      | Version (≥) | License                          | Upstream                                      |
| --------------- | ----------- | -------------------------------- | --------------------------------------------- |
| `gtest`         | 1.14.0      | BSD-3-Clause                     | https://github.com/google/googletest          |
| `benchmark`     | 1.8.3       | Apache-2.0                       | https://github.com/google/benchmark           |

### Adapter features (off by default)

| Feature        | Dependency      | Version (≥) | License                          | Notes                                                    |
| -------------- | --------------- | ----------- | -------------------------------- | -------------------------------------------------------- |
| `opencascade`  | `opencascade`   | 7.7.0       | LGPL-2.1 (with custom OCCT exception) | Linked dynamically; OCCT exception permits commercial use. |
| `gmsh`         | `gmsh`          | 4.12.0      | GPL-2.0+ (with library exception)| Linked dynamically; library-use exception applies.        |
| `petsc`        | `petsc`         | 3.20.0      | BSD-2-Clause                     | Default LinearAlgebra backend.                            |
| `eigen`        | `eigen3`        | 3.4.0       | MPL-2.0                          | Header-only; alternate small-problem backend.             |
| `vtk`          | `vtk`           | 9.3.0       | BSD-3-Clause                     | Output writer for ParaView.                               |
| `python`       | `pybind11`      | 2.11.1      | BSD-3-Clause                     | Required for `pysouxmar`.                                 |

## Subprocess-isolated dependencies (NOT linked)

These tools are invoked as subprocesses via adapters; their code never enters the souxmar address space. License therefore does not affect souxmar's licensing posture.

| Tool       | License            | How invoked                                              |
| ---------- | ------------------ | -------------------------------------------------------- |
| OpenFOAM   | GPL-3.0            | `simpleFoam` / `pimpleFoam` / `foamToVTK` as subprocess. |
| Blender    | GPL-3.0            | `.blend` files parsed via standalone reader (no link).   |

This isolation is a deliberate design decision; see [ADR-0003 (BYOK as AI default)](docs/adr/0003-byok-as-ai-default.md) for the analogous discussion of process boundaries, and the OpenFOAM adapter's own README (when it lands in Sprint 8) for the legal review of process isolation.

## Runtime dependencies of distributed binaries

The desktop app additionally bundles:

| Dependency        | License            | Notes                                                       |
| ----------------- | ------------------ | ----------------------------------------------------------- |
| Tauri 2           | MIT or Apache-2.0  | Shell runtime. Embedded in the desktop installer.           |
| React + ReactDOM  | MIT                | Frontend. Bundled by Vite.                                  |
| Three.js          | MIT                | 3D viewport.                                                |
| VTK.js            | BSD-3-Clause       | `.vtu` reader and field rendering.                          |
| Radix UI          | MIT                | Accessible component primitives.                            |
| Tailwind CSS      | MIT                | Styling utility framework.                                  |
| Lucide icons      | ISC                | Icon set.                                                   |
| Inter             | OFL-1.1            | UI typeface.                                                |
| JetBrains Mono    | OFL-1.1            | Code / console typeface.                                    |

The system WebView is OS-provided and not bundled (WKWebView on macOS, WebView2 on Windows, WebKitGTK on Linux).

## License compatibility summary

souxmar's Apache-2.0 license is compatible with all dependencies listed above:

- **MIT / BSD / ISC / Apache-2.0** — straightforward; no obligations beyond attribution.
- **MPL-2.0** (Eigen) — file-level copyleft; we do not modify Eigen sources, so no impact.
- **LGPL-2.1 with OCCT exception** (OpenCASCADE) — permits both static and dynamic linking from non-LGPL code under the OCCT exception; we link dynamically.
- **GPL with library exception** (Gmsh) — permits library use without GPL contagion under the explicit exception.
- **GPL-3.0** (OpenFOAM, Blender) — process-isolated only; no linking; no licensing impact on souxmar.

For any new dependency, an ADR documents the license, the linkage model, and the licensing impact. See [`docs/ENGINEERING_PRACTICES.md`](docs/ENGINEERING_PRACTICES.md).

## Updating this file

This file is updated in the same PR that adds, removes, or upgrades a dependency. CI's license-scan job (added in Sprint 1) compares the dependency set in `vcpkg.json` and the desktop app's `package.json` against this manifest and fails on drift.
