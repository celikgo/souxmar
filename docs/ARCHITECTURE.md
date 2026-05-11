# Architecture

This document describes the layered architecture of souxmar, the in-memory data model, and the responsibilities of each module. Plugin ABI details live in [`PLUGIN_SDK.md`](PLUGIN_SDK.md); contribution and review processes live in [`GOVERNANCE.md`](GOVERNANCE.md).

## High-level layering

```
+------------------------------------------------------------------+
|                       Desktop Application                        |
|  Tauri 2.x shell (Rust)  +  React/TS frontend (Twitter dim UI)   |
|  Chat panel | 3D viewport (Three.js + VTK.js) | Inspector | DAG  |
|  Detailed in DESKTOP_APP.md, UI_DESIGN.md                        |
+------------------------------------------------------------------+
|                       AI Integration Layer                       |
|  Provider abstraction (Anthropic / OpenAI / Ollama, BYOK)        |
|  Agent tool dispatcher | Confirmation policy | Audit log         |
|  Detailed in AI_INTEGRATION.md                                   |
+------------------------------------------------------------------+
|                          Frontends                               |
|  souxmar (CLI)              |    pysouxmar (Python bindings)     |
+------------------------------------------------------------------+
|                     Pipeline Orchestrator                        |
|  - DAG of stages, declarative on-disk format (YAML/TOML)         |
|  - Content-addressed cache of intermediates                      |
|  - Streaming / out-of-core driver for large meshes               |
+------------------------------------------------------------------+
|                         Plugin Host                              |
|  - Discovery (search paths, manifests)                           |
|  - Versioned C ABI loader, lifecycle, error isolation            |
|  - Capability registry (mesher, solver, element, writer, ...)    |
+------------------------------------------------------------------+
|                       Core Abstractions                          |
|  Geometry  |  Topology  |  Mesh  |  Field  |  LinearAlgebra      |
|  (B-rep)      (entity     (nodes,   (DOF        (matrix/vector   |
|               graph)      cells,    fields,     handles, solver  |
|                           tags)     time series) backends)       |
+------------------------------------------------------------------+
|                    Reference Implementations                     |
|  Native mesher  |  Linear elasticity  |  Heat conduction         |
|  (in-tree, ship as the same plugin shape as third-party code)    |
+------------------------------------------------------------------+
|                         Adapters                                 |
|  OpenCASCADE | Gmsh | FEniCSx | OpenFOAM | Blender | VTK | PETSc |
+------------------------------------------------------------------+
|                          Platform                                |
|  CMake + vcpkg + Tauri 2 + Vite, CI (Linux/macOS/Windows), pybind11 |
+------------------------------------------------------------------+
```

The boundaries that matter most are:

1. The **Plugin Host / Core Abstractions** boundary — the C ABI. Stable across all minor versions of a major. Detailed in [`PLUGIN_SDK.md`](PLUGIN_SDK.md).
2. The **Pipeline / Plugin Host** boundary — the orchestrator only ever touches plugins through the host. It never links a mesher or solver directly. This is what makes the reference implementations replaceable.
3. The **Frontends / AI / Desktop** stack above the orchestrator. The desktop app, the CLI, the Python bindings, and the AI agent are all peers — they each call the orchestrator, none of them is special. The agent in particular has no privileged path into the core; it goes through the same tool surface a user could script. This is what keeps the AI auditable.

## Repository layout

```
souxmar/
  CMakeLists.txt
  LICENSE
  README.md
  docs/                    # design documents (this folder)
  cmake/                   # CMake helper modules, plugin SDK macros
  include/souxmar/         # public C++ headers
  include/souxmar-c/       # public C ABI headers (plugin SDK)
  src/
    core/                  # libsouxmar-core: data model, no I/O
    plugin-host/           # libsouxmar-plugin: discovery, loading, ABI
    pipeline/              # libsouxmar-pipeline: DAG, cache, runner
    adapters/
      opencascade/         # CAD import via OCCT
      gmsh/                # mesh import/export, embedded Gmsh
      fenicsx/             # FEM solver adapter
      openfoam/            # CFD solver adapter (case generation, run, read)
      blender/             # .blend importer (concept/architectural geometry)
      vtk/                 # writer for ParaView
    plugins/               # in-tree reference plugins
      mesher-native/
      solver-elasticity/
      solver-heat/
    cli/                   # `souxmar` executable
    bindings/python/       # pybind11 module `pysouxmar`
    ai/                    # libsouxmar-ai: provider abstraction, agent loop, tools
      providers/           # anthropic/, openai/, ollama/
      tools/               # typed agent tools (read_geometry, mesh, solve, ...)
      keychain/            # OS keychain integration for BYOK keys
    desktop/               # Tauri shell + React frontend
      src-tauri/           # Rust shell, FFI to libsouxmar-*, IPC handlers
      src/                 # React + TypeScript frontend
        ui/                # tokens, primitives (Radix + Tailwind, dim theme)
        viewport/          # Three.js + VTK.js scene
        chat/              # chat panel UI
        pipeline-editor/
        inspector/
      ui-storybook/        # component gallery, accessibility tests
  tests/
    unit/                  # gtest, per-module
    integration/           # end-to-end pipelines, golden outputs
    plugin-conformance/    # ABI conformance suite, runnable by external plugins
  examples/
    cantilever-beam/       # canonical "hello world" pipeline
    thermal-fin/
    heat-exchanger-mesh-study/
  benchmarks/              # google-benchmark, perf regressions
```

This layout is enforced by `CMakeLists.txt` boundaries — `core` does not depend on `pipeline`, `pipeline` does not depend on adapters, etc. Cycles are a build error, not a code-review comment.

## Module responsibilities

### `libsouxmar-core`

The data model. Header-only where possible. **No I/O, no plugin loading, no Python.** This is the layer that defines what a mesh *is* in souxmar memory; everything else operates on it.

Owns:

- `Geometry` — B-rep entities (vertices, edges, faces, solids), parametric history, named tags. Backed by OpenCASCADE handles via opaque pointer; the OCCT dependency is hidden behind a wrapper so that a future kernel swap is contained.
- `Topology` — abstract entity graph independent of geometry, used by meshes that have no underlying CAD (e.g. a raw STL).
- `Mesh` — nodes, cells, mixed-element support (tet/hex/prism/pyramid/tri/quad/edge), per-entity tag inheritance from Geometry.
- `Field` — DOF data over a mesh: scalar / vector / tensor, nodal / cell-centred / Gauss-point, optional time dimension.
- `LinearAlgebra` — opaque matrix/vector handles, backend-pluggable (default: PETSc; alt: Eigen for small problems).

All five types use a small, stable C-friendly layout under the hood so the same memory can be exposed across the plugin ABI without copies.

### `libsouxmar-plugin` (Plugin Host)

Discovery, loading, and lifecycle for `.so` / `.dylib` / `.dll` plugins. Maintains the **capability registry** — a runtime catalogue of every plugin function the system can dispatch to (`mesher.tetra.netgen`, `solver.linear.fenicsx`, `writer.vtu`, etc.). Dispatched by name, not by linkage.

Responsibilities:

- Walk plugin search paths (`SOUXMAR_PLUGIN_PATH`, plus a default per-OS list).
- Read manifest (`souxmar-plugin.toml`) for each candidate, validate ABI version.
- `dlopen` the binary, resolve `souxmar_plugin_register_v1`, populate registry.
- Wrap every plugin call in a longjmp/SEH-style error frame so a plugin segfault does not take down the host.
- Enforce thread-safety contracts declared in the manifest (some algorithms are reentrant, some are not).

### `libsouxmar-pipeline`

A directed acyclic graph of stages. Each stage is a typed call into the plugin registry plus its inputs. Pipelines are declarative on disk:

```yaml
# cantilever.souxmar.yaml
version: 1
stages:
  - id: import
    plugin: reader.step
    input: { path: beam.step }

  - id: mesh
    plugin: mesher.tetra.native
    input:
      geometry: { from: import }
      target_size: 5.0e-3

  - id: solve
    plugin: solver.elasticity.linear
    input:
      mesh:    { from: mesh }
      bcs:
        - { tag: clamped_face, type: dirichlet, value: [0, 0, 0] }
        - { tag: tip_face,     type: neumann,   value: [0, -1000, 0] }
      material: { youngs_modulus: 210e9, poissons_ratio: 0.3 }

  - id: export
    plugin: writer.vtu
    input:
      mesh:   { from: mesh }
      fields: { from: solve }
      path:   results/cantilever.vtu
```

The orchestrator:

- Resolves `from:` references into a DAG, validates types match plugin signatures.
- Hashes each stage by (plugin id, plugin version, input hash). Cache hits skip execution.
- Runs stages in dependency order, parallelising independent branches.
- Streams large intermediates through memory-mapped temp files when they exceed a threshold, so a 200M-cell mesh does not have to fit in RAM.

### Frontends

- **`souxmar` CLI** — runs a pipeline file, reports progress, manages the cache. Thin wrapper over the C++ pipeline library.
- **`pysouxmar` Python module** — pybind11 bindings for the data model and the orchestrator. Lets users build pipelines programmatically, integrate with Jupyter, and write Python-side plugins (which are wrapped to look like C ABI plugins to the host).
- **Desktop application** — Tauri 2 shell (Rust) hosting a React + TypeScript frontend. Exposes the chat panel, 3D viewport, pipeline editor, and inspector. The Tauri shell calls the same `libsouxmar-core`, `libsouxmar-plugin`, and `libsouxmar-pipeline` libraries the CLI links to — there is **no separate GUI codebase**. Detailed in [`DESKTOP_APP.md`](DESKTOP_APP.md). Visual design in [`UI_DESIGN.md`](UI_DESIGN.md).

### `libsouxmar-ai` (AI Integration)

A C++/Rust library exposing the agentic chat layer. Loaded by the desktop app and accessible from the CLI for headless agent runs. Three responsibilities:

- **Provider abstraction.** A single `IAIProvider` interface implemented by Anthropic, OpenAI, and Ollama backends. Streaming completions, tool calls, prompt caching where supported. Configured per project; credentials never leave the OS keychain.
- **Agent tool dispatcher.** A typed catalogue of agent-callable tools (`mesh`, `solve`, `set_bc`, `read_geometry_summary`, …) that map onto orchestrator and plugin-host calls. The agent has no privileged path — it goes through this dispatcher, which enforces the per-tool confirmation policy (`auto` / `confirm-once` / `confirm-always`).
- **Audit log.** Every tool invocation is appended to `.souxmar/chat/audit.log` with input hash, runtime, and (for managed AI) token cost. The user can inspect, export, or wipe it at any time.

The AI layer is **optional**: the CLI, Python bindings, and desktop app all function fully with no provider configured. Detailed in [`AI_INTEGRATION.md`](AI_INTEGRATION.md).

## Data model in detail

### Geometry → Mesh tag inheritance

The single most-asked question in any FEM workflow is "what face is this in the original CAD?" souxmar answers it with **tag inheritance**: every `Face` in the imported `Geometry` carries an integer ID and an optional human name (`"clamped_face"`); the mesher copies that ID to every cell-face it generates from that geometric face. Boundary conditions then refer to tags, not coordinates.

This is the contract the plugin ABI enforces: a mesher that loses tags is non-conforming.

### Field semantics

A `Field` always carries:

- A reference to its `Mesh`.
- A location: nodal, cell, face, or Gauss-point (with quadrature rule reference).
- A value type: `f64` scalar / `f64[3]` vector / `f64[3][3]` tensor / arbitrary user struct.
- An optional time series — implemented as a separate "time axis" object so that out-of-core time series do not blow memory.

Solvers consume and produce `Field`s. This is how a multi-stage pipeline (mesh → linear solve → mode shape extraction → response history) shares data without serialisation.

### Linear algebra abstraction

Solvers do not own their matrices. The host hands a solver a `MatrixBuilder` and `VectorBuilder`; the solver calls `add_block(...)` for each element contribution; the host decides whether the backing store is PETSc, Eigen, or a future GPU backend. This is what makes a single solver plugin work across problem sizes from 10³ to 10⁹ DOFs.

## Adapter layer

Each adapter is a thin shim that converts between souxmar's data model and a third-party library. Adapters are statically linked into the binaries that need them, not loaded as plugins, because their stability is tied to the upstream library version, not to the souxmar ABI version.

| Adapter        | Wraps                          | Why an adapter, not a plugin                                                |
| -------------- | ------------------------------ | --------------------------------------------------------------------------- |
| `opencascade`  | OCCT 7.x                       | Geometry kernel — load-bearing, version-coupled with the rest of OCCT.      |
| `gmsh`         | Gmsh 4.x as a library          | Optional alternative mesher; ships when Gmsh is found at build time.        |
| `fenicsx`      | DOLFINx                        | High-quality FEM solver, but its Python-heavy API doesn't match the plugin ABI. |
| `openfoam`     | OpenFOAM v12+                  | CFD reference solver. Adapter generates `case/` directories, runs `simpleFoam`/`pimpleFoam`/etc., reads results back through `foamToVTK`. |
| `blender`      | Blender 4.x via `.blend` reader | Concept-stage and architectural geometry source. Read-only; we don't drive Blender's UI. |
| `vtk`          | VTK 9.x writers                | Output to ParaView. Read side is symmetric.                                 |

Each adapter is feature-gated by a CMake option (`-DSOUXMAR_WITH_GMSH=ON`). A souxmar build with no adapters is still functional — it has the native mesher and reference solvers — but is then a closed ecosystem.

## Threading and concurrency model

- The orchestrator is async and parallelises independent DAG branches across a thread pool.
- Each plugin declares its thread-safety in its manifest:
  - `reentrant` — multiple instances may run in parallel on disjoint inputs.
  - `single-threaded` — host serialises calls.
  - `internal-parallel` — the plugin manages its own threads; host runs only one instance.
- The data model itself is immutable from the plugin's perspective. Inputs are `const`; outputs are owned by the plugin until ownership is transferred to the host. This eliminates entire categories of data races.
- No global state in the core. All state hangs off a `souxmar_context_t` that callers pass explicitly — this also makes the library safely embeddable inside a larger host process.

## Memory model

- Every cross-ABI allocation carries an explicit deallocator function pointer alongside it. Plugins free what plugins allocate; the host frees what the host allocates. Never `free()` across a DLL boundary, ever.
- Large mesh and field buffers are passed as `(ptr, length, deallocator)` triples. The host can choose to take ownership (move) or borrow (`const` view) per call.
- For very large data, an mmap-backed buffer type is used so that a 50 GB mesh on disk can be passed to a plugin without ever fully residing in RAM.

## Build, packaging, distribution

- **Build system:** CMake 3.25+. Plugins are first-class CMake targets via a helper `souxmar_add_plugin(...)` macro that bakes in the manifest, ABI version check, and install rules.
- **Dependencies:** vcpkg manifests in-tree, with conan as a documented alternative. No `git submodule` for build deps.
- **CI:** GitHub Actions matrix across Ubuntu 22.04 / macOS 14 / Windows Server 2022, x86_64 and arm64. Every PR runs unit + integration + plugin-conformance suites and a small benchmark sentinel.
- **Distribution:**
  - Source releases: tarball + sha256, signed.
  - Binary core: per-OS tarballs.
  - Python: wheels on PyPI as `pysouxmar`.
  - Plugins: each plugin author ships their own wheel/tarball; we host an index of known plugins, not the binaries themselves.

## What is intentionally absent

- **No DI framework, no service locator.** The capability registry is the only runtime indirection. Adding more would obscure the data flow.
- **No bespoke reflection / metaprogramming framework.** Plain C structs at the ABI boundary, plain C++ inside.
- **No internal IPC.** The pipeline runs in one process. Distributed execution is a future plugin, not a core concern.
- **No telemetry.** The library never phones home.
