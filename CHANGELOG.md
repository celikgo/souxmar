# Changelog

All notable changes to souxmar are documented here. The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

The plugin C ABI version is tracked separately and is independent of the project version. ABI v1 is frozen for the entire 1.x series; see [ADR-0001](docs/adr/0001-c-abi-for-plugins.md).

## [Unreleased]

### Added

#### Sprint 1 (in progress) — data model + plugin host + C ABI

- `libsouxmar-core` data model in `include/souxmar/core/`:
  - `tag.h` — strong-typed `EntityTag`, `NodeIndex`, `CellIndex`, `VertexIndex`, `EdgeIndex`, `FaceIndex`, `SolidIndex` with std::hash specialisations.
  - `element_type.h` — `ElementType` enum (numerically stable across the on-disk format and C ABI) plus per-type `ElementInfo` (dimension, node count, order, canonical name).
  - `geometry.h` — `Geometry` (PIMPL'd; vertex coordinates, opaque bookkeeping for edges/faces/solids, per-entity tag / name, bounding box, adapter-data slot for native handles).
  - `topology.h` — `Topology` (kind-indexed entity graph for meshes without a CAD model).
  - `mesh.h` — `Mesh` (mixed-element, contiguous flat storage, tag inheritance, element histogram, bounding box).
  - `field.h` — `Field` (scalar/vector/tensor over nodes/cells/faces/Gauss points, optional time series, contiguous storage with VTK-compatible stride).
- **[ABI v1]** Public C ABI headers in `include/souxmar-c/`: `abi.h` (export macros, version constants), `status.h` (numerically-stable error codes), `plugin.h` (entry-point declaration + host-info struct), `registry.h` (capability registration entry), `mesher.h` (first capability vtable), `types.h` (opaque handle declarations). ABI v1 frozen-candidate begins; final freeze at Sprint 7 per `docs/SPRINT_PLAN.md`.
- `libsouxmar-plugin`:
  - `plugin/manifest.h` + `manifest.cpp` — `souxmar-plugin.toml` parser via tomlplusplus, with `ParseError` carrying line context. Validates required fields, capability list, threading model, ABI compatibility.
  - `plugin/discovery.h` + `discovery.cpp` — search-path computation (`SOUXMAR_PLUGIN_PATH`, install prefix, per-OS user prefix, optional CWD), directory walker, manifest validation, binary-existence check, structured `DiscoveryReport` of loaded + rejected.
- Unit tests (GoogleTest) covering the above:
  - `test_tag`, `test_element_type` — strong types + element metadata
  - `test_geometry`, `test_topology` — counts, tag/name roundtrips, bounding-box, move semantics, adapter-data deleter
  - `test_mesh` — node/cell add, mixed elements, histogram, validation errors
  - `test_field` — metadata, components by kind, time-step indexing, error paths
  - `test_manifest` — valid parse, multi-capability, missing-field, threading-model parsing, ABI mismatch, malformed-TOML line reporting
  - `test_discovery` — empty paths, missing path, valid plugin, missing binary, wrong extension, malformed manifest, multi-root aggregation

#### Sprint 2 (in progress) — capability registry, plugin loader, hello-mesher

- `libsouxmar-plugin` capability registry (`include/souxmar/plugin/registry.h`, `src/plugin-host/registry.cpp`):
  - `Registry` class: thread-safe (`std::shared_mutex`, read-mostly), `add_mesher` with structured `RegistryError`, `find` / `find_mesher` / `list_capabilities` / `list_capabilities_in_namespace` / `remove_plugin`.
  - C ABI bridge: `extern "C" souxmar_registry_add_mesher` casts the opaque `souxmar_registry_t*` back to `Registry*`, enforces ABI version + null-vtable + null-mesh_fn checks, translates failures to `souxmar_status_t` with thread-local error strings.
- Crash-isolation guard (`include/souxmar/plugin/guard.h`, `src/plugin-host/guard.cpp`):
  - `guard_call(fn) -> GuardResult`. Sprint 2 implementation catches C++ exceptions (`std::exception` and unknown-throw) as `SOUXMAR_E_PLUGIN_FAULT`-equivalent. Sprint 5 hardening adds POSIX `sigaction`/`sigsetjmp` and Windows SEH around the same surface; the public API stays stable.
- Plugin loader (`include/souxmar/plugin/loader.h`, `src/plugin-host/loader.cpp`):
  - `LoadedPlugin` (move-only RAII) owns the dlopen / `LoadLibrary` handle and removes its capabilities from the registry on destruction.
  - `PluginLoader::load(DiscoveredPlugin)` opens the binary, resolves `souxmar_plugin_register_v1`, calls it inside `guard_call`, and threads the plugin id through the registry so `add_mesher` attribution is automatic.
  - Cross-platform: POSIX `dlopen` + `dlsym` + `dlclose`; Windows `LoadLibraryExW` + `GetProcAddress` + `FreeLibrary`. Linked against `${CMAKE_DL_LIBS}` on POSIX.
- `souxmar_add_plugin` CMake macro (`cmake/SouxmarPlugin.cmake`):
  - Declares a `SHARED` library, hides default symbol visibility, defines `SOUXMAR_BUILD_PLUGIN`, copies the manifest beside the binary, and stashes the announced capabilities as a target property for tooling.
  - Same macro is intended for in-tree examples and out-of-tree third-party plugins.
- `examples/plugins/hello-mesher/`:
  - Reference plugin proving the full SDK contract — single exported symbol, vtable, registration call against the host registry. `mesh_fn` is a placeholder until host-side mesh accessors land in Sprint 3.
  - Built with `souxmar_add_plugin`. CMake auto-enables examples when `SOUXMAR_BUILD_TESTS=ON` so the integration suite always has the plugin to load.
- Tests:
  - `test_registry` — empty / add / duplicate / null-vtable / null-mesh_fn / ABI-mismatch / namespace-listing / find / `remove_plugin`.
  - `test_guard` — Ok / std::exception / non-std exception / nested guard.
  - `tests/integration/test_load_hello_mesher` — end-to-end discovery + load + register + capability-listing + RAII unload + registry empties on `LoadedPlugin` drop.

### Changed

- (None this release.)

### Fixed

- (None this release.)

### Removed

- (None this release.)

### Security

- (None this release.)

---

## How to read this file

- `[Unreleased]` accumulates changes between releases. At release time, the section is renamed to `[X.Y.Z] - YYYY-MM-DD` and a fresh `[Unreleased]` is opened.
- Sections are added only when they have content; empty sections are omitted from a tagged release.
- Each entry is one line, present tense, references the PR or issue when meaningful.
- ABI / pipeline-format / agent-tool changes are called out explicitly with a `**[ABI v1]**` / `**[pipeline-format v1]**` / `**[agent-tool v1]**` prefix so plugin authors and integrators can scan the file for impact.
