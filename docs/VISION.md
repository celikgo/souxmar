# Vision

## Problem

The open-source CAE stack is a chain of excellent but disjoint projects:

| Stage             | Reference tool       | Maturity      | Primary scripting |
| ----------------- | -------------------- | ------------- | ----------------- |
| Parametric CAD    | FreeCAD              | Production    | Python            |
| Concept/freeform  | Blender              | Production    | Python            |
| Meshing           | Gmsh                 | Production    | GEO / Python      |
| FEM solving       | FEniCSx              | Production    | Python (UFL)      |
| CFD solving       | OpenFOAM             | Production    | C++ dictionaries  |
| Post-processing   | ParaView             | Production    | Python (VTK)      |

Each tool is well-engineered in isolation. Stitching them together is the user's job, and it is consistently the most time-consuming and error-prone part of any analysis. Three concrete pain points:

1. **No shared data model.** A B-rep solid in FreeCAD becomes a STEP file, becomes a Gmsh `.msh`, becomes an XDMF, becomes a VTU. Each conversion is lossy: tags, parametric history, material assignments, and boundary-condition labels are dropped or reinvented at every boundary.

2. **Algorithm experimentation is gated by tool boundaries.** A graduate student with a new anisotropic meshing idea must either fork Gmsh (a six-figure-line C++ codebase) or run their algorithm standalone and write yet another converter. Neither path produces something a downstream user can adopt.

3. **Reproducibility is fragile.** A working pipeline today depends on six package versions, four scripting environments, and the operator's tacit knowledge of which intermediate files to keep. CI is rare; binary distribution is rarer.

4. **There is no product for the engineer.** A working mechanical, structural, or aerospace engineer who is not also a Python developer has no Cursor-style "open the app, describe the problem, get results" surface. The available open tools are libraries pretending to be applications.

## What souxmar is

souxmar is **two things at once**: a scalable orchestration layer over the existing CAE stack, and a modern desktop application that puts that orchestration in the hands of working engineers. Built around four commitments:

- **A unified in-memory data model** for geometry, topology, mesh, and field data, so the boundary between CAD, mesh, and solver is a function call, not a file.
- **A stable C plugin ABI** so that anyone can implement a mesher, element, solver, reader, writer, or post-processing operator in C, C++, or Rust, ship it as a binary, and have it discovered at runtime by an unmodified souxmar install.
- **A pipeline as a first-class artefact** — declarative, content-addressed, reproducible, diffable, shareable, version-controllable.
- **An agentic desktop app** with a chat panel that can drive the entire pipeline through a typed tool surface, using the user's own AI provider (Anthropic, OpenAI, or local Ollama). The chat is a control surface, not a documentation lookup. See [`AI_INTEGRATION.md`](AI_INTEGRATION.md).

souxmar wraps OpenCASCADE for geometry, embeds Gmsh and a native mesher behind a common interface, talks to FEniCSx and a native solver through the same `ISolver` contract, adapts OpenFOAM for CFD problems, accepts Blender models for concept and architectural geometry, and emits VTK for ParaView and any future viewer.

## Product surfaces

| Surface             | For                                              | Status                  |
| ------------------- | ------------------------------------------------ | ----------------------- |
| Desktop app         | Engineers, architects — primary surface          | v1 deliverable. See [`DESKTOP_APP.md`](DESKTOP_APP.md). |
| Agentic AI chat     | Inside the desktop app; BYOK or managed          | v1. See [`AI_INTEGRATION.md`](AI_INTEGRATION.md). |
| `souxmar` CLI       | CI, batch runs, scripts                          | v1.                     |
| `pysouxmar` Python  | Notebooks, embedding, custom workflows           | v1.                     |
| Plugin SDK          | Researchers, third-party tool builders           | v1. See [`PLUGIN_SDK.md`](PLUGIN_SDK.md). |
| Managed services    | Pro/Team/Enterprise — managed AI, sync, compute  | Post-1.0. See [`BUSINESS_MODEL.md`](BUSINESS_MODEL.md). |

## What souxmar is not

- **Not a CAD modeller.** Drawing parametric parts is FreeCAD's job; freeform and concept geometry is Blender's. We import; we do not draw. (Lightweight inspector edits — repositioning, simple feature suppression — are in scope; full parametric modelling is not.)
- **Not a new PDE language.** UFL exists; FEniCSx implements it well. We provide an adapter, not a competitor.
- **Not a new CFD solver.** OpenFOAM is the canonical open-source CFD; we adapt it.
- **Not a single-vendor solver.** souxmar does not pick winners. The reference solver is a teaching implementation; serious work plugs in MFEM, deal.II, FEniCSx, OpenFOAM, or a custom solver.
- **Not a chat client wrapping an LLM.** The AI is a control surface for the simulation backend, not a generic assistant. Without the simulation backend, the chat does nothing useful.
- **Not a SaaS.** There is no hosted "souxmar.app" at v1. The app is a desktop binary; the Pro tier adds optional cloud services it calls into.
- **Not a black box.** Every stage is inspectable. Every intermediate is dumpable. Every plugin is a separately auditable artefact. The desktop app itself is open source.

## Target users

Primary (the desktop app is built for them):

1. **Mechanical and structural engineers** running parts and assemblies through stress, deflection, vibration, and thermal analyses. They want a pipeline they can trust on Monday and re-run on Friday.
2. **Aerospace / aviation engineers** who need traceable, reproducible analysis pipelines for components and sub-assemblies, often under regulatory scrutiny. Open-source code they can audit is non-negotiable.
3. **Architectural and civil engineers** running building-component analyses, MEP airflow studies, and concept-stage structural sizing. Often coming from Blender or Revit on the geometry side.

Secondary (the library, CLI, Python, and SDK are built for them):

4. **Researchers** testing new mesh algorithms, elements, or solver formulations against real industrial geometry without writing a CAD importer or a viewer.
5. **Educators** who want students to understand each stage of the CAE pipeline without drowning them in glue code.
6. **Tool builders** embedding a CAE pipeline inside a larger product (digital twin, generative design, simulation-as-a-service) without licensing fees.

## Design principles

1. **Stable boundaries, fluid interiors.** The plugin ABI and on-disk pipeline format are stable across major versions. Internal data structures are free to evolve.
2. **Adapter over reinvention.** If a mature tool exists for a task, we adapt it. We only build native implementations when the abstraction itself is the contribution.
3. **Composable, not monolithic.** Every algorithm is a plugin. The reference implementations of meshing and solving live behind the same ABI as third-party plugins. We eat our own dog food.
4. **Performance is a feature.** Hot paths (assembly, mesh refinement) run in C++ with no Python in the loop. Python is for orchestration, not inner loops.
5. **Reproducibility is a feature.** Pipelines are content-addressed. The same inputs produce the same outputs across machines and OSes.
6. **Permissive licensing.** Apache 2.0 throughout. Commercial users can embed souxmar without legal review beyond a one-time NOTICE check.

## Non-goals at v1.0

- Multiphysics coupling beyond what an external solver can express.
- GPU-accelerated solvers (will arrive via plugin; not core).
- Cloud orchestration (single-machine or one HPC node; managed offload is a Pro service, not a core feature).
- A hosted SaaS version of the desktop app.
- A new CAD modeller. We import from FreeCAD, Blender, STEP, IGES, and STL.

(CFD, the desktop app, and the AI integration were originally non-goals at v1 and have been promoted into v1 scope. The roadmap reflects this.)

## Success criteria

souxmar v1.0 is successful if:

- An engineer can install the desktop app on macOS, Windows, or Linux, drop in a STEP file, type "mesh this and solve a linear-elastic problem with the bottom face clamped and 1 kN downward on the top" in chat, configure their own AI key once, and have a viewable result inside five minutes.
- The same workflow expressed as a YAML pipeline runs from `souxmar` CLI, from a Python script, and inside the desktop app, with byte-identical results on Linux, macOS, and Windows.
- A graduate student can publish a new mesh algorithm as a souxmar plugin and a downstream user can install it and use it the same day, from chat or from CLI.
- The free tier alone (BYOK chat, local compute, full plugin SDK) is a complete product. The paid tier is a convenience upgrade, not a feature unlock.
- At least three external projects ship souxmar plugins.
