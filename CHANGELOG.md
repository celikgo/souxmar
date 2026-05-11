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

#### Sprint 3 (in progress) — pipeline orchestrator critical path

- New library `libsouxmar-pipeline` (`src/pipeline/`).
- **[pipeline-format v1]** Pipeline data model + YAML parser:
  - `include/souxmar/pipeline/value.h` — typed Value tree (Null / Bool / Number / String / StageRef / List / Map). Heavy types (Geometry/Mesh/Field) stay out of Value; they live in the runner's result store and are referenced by StageRef.
  - `include/souxmar/pipeline/pipeline.h` — `Stage` (id + capability + input map) and `Pipeline` (version + stages).
  - `include/souxmar/pipeline/parser.h` — strict YAML parser via yaml-cpp. Recognises the `{ from: stage_id }` StageRef shorthand. Validates required fields, version = 1, non-empty stages, unique ids. Errors carry source line/column when yaml-cpp surfaces them.
- DAG validation + topological sort (`include/souxmar/pipeline/dag.h`):
  - All errors collected per call (not just the first).
  - Self-reference, dangling-reference, cycle detection.
  - Stable topological order (declaration-order tie-break) so determinism gate has a fighting chance.
  - `collect_stage_refs(Value)` exposed for tests and tooling.
- Content-addressed cache (`include/souxmar/pipeline/cache.h`):
  - 64-bit FNV-1a `ContentHash` over (capability_id + plugin_version + recursive Value tree). Upstream stage hashes folded transitively so an upstream change invalidates downstream cache keys automatically. (Cryptographic hashing — BLAKE3 or SHA256 — lands with the on-disk cache in Sprint 3 push 2.)
  - `Cache` class: thread-safe (`std::shared_mutex`), in-memory put / get / contains / size / clear.
- Runner (`include/souxmar/pipeline/runner.h`):
  - `IDispatcher` interface — runner is plugin-agnostic. Sprint 3 push 1 ships with mock dispatcher in tests; Sprint 3 push 2 adds `RegistryDispatcher` that goes through the C ABI to loaded plugins.
  - Sequential execution in topological order. Per-stage `StageRunResult` records Cached / Executed / Failed / Skipped. Optional `RunOptions::stop_on_first_failure` (default ON) and `use_cache` (default ON).
  - Aggregate `RunResult` carries validation errors + per-stage results + outputs by stage id.
- New default dependency: `yaml-cpp >= 0.8.0` (MIT). Documented in `THIRD_PARTY_LICENSES.md`.
- Tests (all in `tests/unit/`, ~50 new test cases):
  - `test_pipeline_value` — kind builders, accessors, try-access, equality, kind names.
  - `test_pipeline_parser` — cantilever-beam YAML round-trip, StageRef shorthand, missing/duplicate/empty-stages rejection, unknown-version explicit rejection, line-reporting on malformed YAML.
  - `test_pipeline_dag` — straight-line, diamond, self-reference, dangling-reference, cycle detection, deeply-nested StageRef collection, declaration-order topological tie-break.
  - `test_pipeline_cache` — context distinguishes, upstream change propagates, map-key ordering doesn't matter, hex format, put/get/overwrite/clear.
  - `test_pipeline_runner` — validation skip, topological dispatch order, output threading, fail-fast skip-downstream, full cache hit, input-change invalidation, plugin-version invalidation, upstream-change invalidation, cache-disabled mode.

#### Sprint 3 (in progress, push 2) — C ABI handles + RegistryDispatcher + end-to-end

- **[ABI v1]** Public C ABI handle accessors:
  - `include/souxmar-c/mesh.h` + `src/core/c_abi_mesh.cpp` — `souxmar_mesh_new`/`free`/`add_node`/`add_cell`/`reserve_*`/`num_*`/`node`/`cell_type`/`cell_node_count`/`cell_nodes`/`cell_tag`/`nodes_flat` plus `SOUXMAR_ET_*` numerically-stable element-type constants.
  - `include/souxmar-c/geometry.h` + `src/core/c_abi_geometry.cpp` — `souxmar_geometry_new`/`free`/`add_vertex`/`add_edge`/`add_face`/`add_solid`/`set_tag`/`set_name`/read accessors/`bounding_box`. `SOUXMAR_GK_*` entity-kind constants.
  - `include/souxmar-c/field.h` + `src/core/c_abi_field.cpp` — `souxmar_field_new`/`free` + read accessors. `SOUXMAR_FL_*` and `SOUXMAR_FK_*` enums.
  - `include/souxmar-c/value.h` + `src/pipeline/c_abi_value.cpp` — `souxmar_value_kind` + per-kind accessors (`as_bool`/`as_number`/`as_string`/`as_stage`) + list/map navigation. `SOUXMAR_VK_*` enums. Value handles are read-only across the ABI.
- **[ABI v1]** New capability vtable headers:
  - `include/souxmar-c/solver.h` — `souxmar_solver_vtable_t` + options struct. Solver inputs reach the plugin as a `souxmar_value_t` bag; the host extracts the `mesh: {from: ...}` upstream by convention.
  - `include/souxmar-c/writer.h` — `souxmar_writer_vtable_t`. Writer takes mesh + optional field + value bag; output is side-effect (typically a file at `path`).
- Registry extensions (`include/souxmar/plugin/registry.h`, `src/plugin-host/registry.cpp`):
  - `CapabilityKind` extended to {Mesher, Solver, Writer}.
  - `add_solver` / `add_writer` (C++) and `add_solver_c` / `add_writer_c` (C ABI bridges) with the same validation pattern as `add_mesher`.
  - `extern "C" souxmar_registry_add_solver` / `add_writer` extern wrappers.
  - `find_solver` / `find_writer` typed lookup.
- `RegistryDispatcher : IDispatcher` (`include/souxmar/pipeline/registry_dispatcher.h`, `src/pipeline/registry_dispatcher.cpp`):
  - Routes by namespace prefix (`mesher.*` / `solver.*` / `writer.*`).
  - Convention-based handle extraction: mesher reads optional `geometry: {from: ...}`; solver requires `mesh: {from: ...}`; writer requires `mesh: {from: ...}` + optional `field: {from: ...}`.
  - All plugin calls go through `plugin::guard_call` so a plugin throw cannot unwind into the host.
  - `StageOutput` typed wrapper (Mesh / Geometry / Field / Path) is the universal payload threaded through the runner's `shared_ptr<void>`.
  - Custom `shared_ptr` deleters call `souxmar_mesh_free` / `souxmar_field_free` so plugin-allocated handles round-trip the C ABI ownership rules.
- Reference plugins:
  - `examples/plugins/hello-mesher/hello_mesher.cpp` upgraded — `mesh_fn` now produces a real 1-tet mesh through the C ABI (`souxmar_mesh_new` → `souxmar_mesh_add_node` × 4 → `souxmar_mesh_add_cell` SOUXMAR_ET_TET4).
  - `examples/plugins/hello-writer/` — minimal writer plugin reading the mesh through C ABI accessors and writing a 2-line summary to the path supplied via the `souxmar_value_t` input bag. Built with `souxmar_add_plugin`; manifest declares `writer.text-summary`.
- Tests:
  - `tests/unit/test_c_abi_handles` — full coverage of the C ABI accessors for mesh, geometry, field, and value handles. Round-trip, error paths, NULL-pointer safety, capacity checks.
  - `tests/integration/test_pipeline_end_to_end` — the **canary** for the whole stack: discover hello-mesher + hello-writer, load both into a Registry, parse a 2-stage YAML pipeline, run through `RegistryDispatcher` + `Cache`, verify the output file exists with the expected content. Plus: cache-hit-on-rerun verification, missing-mesh-reference rejection.

#### Sprint 3 push 3 — CLI, VTU writer, on-disk cache, runnable example

- `souxmar` CLI binary (`src/cli/main.cpp`, `src/cli/CMakeLists.txt`):
  - `souxmar run <pipeline.yaml>` — parses the pipeline, discovers + loads every available plugin, dispatches stages through `RegistryDispatcher`, prints per-stage status (`[OK]` / `[CACHED]` / `[FAILED]` / `[SKIPPED]`) with content-hash hex.
  - `souxmar plugin list` — pretty-prints discovered plugins, manifest paths, ABI versions, and announced capabilities.
  - `souxmar version` — version + ABI string.
  - Flags: `--plugin-path <dir>` (repeatable, prepends to discovery path), `--no-cache`, `--cache-dir <dir>`.
  - Exit codes follow sysexits.h (0 / 64 / 65 / 70).
  - Built only when `SOUXMAR_BUILD_CLI=ON` (default).
- **[pipeline-format v1]** `examples/plugins/vtu-writer/` — in-tree writer plugin emitting ParaView-readable VTK XML UnstructuredGrid (`.vtu`) without linking VTK. Hand-emits the ASCII format; covers all 17 `SOUXMAR_ET_*` element types via a stable souxmar→VTK cell-type map. Capability: `writer.vtu`. The full VTK-backed adapter (binary + appended data + parallel pieces) lands in Sprint 6.
- `examples/cantilever-beam/` — runnable end-to-end example with `pipeline.yaml` + README walking the user from a fresh `cmake --build` to a `cantilever.vtu` opened in ParaView. Today the mesh stage is the placeholder hello-mesher (single tet); Sprint 6 swaps in the OpenCASCADE-loaded geometry + native tetra mesher with the same YAML structure.
- **SHA-256 content hash** (`src/pipeline/cache.cpp`) — `ContentHash` is now backed by a 32-byte SHA-256 digest (in-tree FIPS 180-4 implementation, no external dep). Public surface unchanged; `hex()` returns 64 lowercase chars. Replaces the FNV-1a Sprint 3 push 1 hash, which was good enough for in-process buckets but not durable enough to be the key for the on-disk cache and (in Sprint 7+) the distributed artifact store.
- **On-disk cache** (`include/souxmar/pipeline/cache.h`, `src/pipeline/cache.cpp`):
  - New `DiskCache` class — directory-backed byte-blob KV (`<dir>/<hex>` per key), atomic per-key writes via temp+rename, no cross-process locking yet (Sprint 5 adds advisory locks alongside the parallel runner).
  - `DiskCache::default_dir()` — resolves `$SOUXMAR_CACHE_DIR` → `$XDG_CACHE_HOME/souxmar` (Linux) / `~/Library/Caches/souxmar` (macOS) / `%LOCALAPPDATA%\souxmar\cache` (Windows) → `<tmp>/souxmar-cache`.
  - `RunOptions::disk_backing` — opt-in `DiskBacking` struct carrying a `DiskCache` plus `serialize` / `deserialize` callbacks. The runner consults disk on in-memory miss and writes through after a successful dispatch. Both callbacks owned by the caller — keeps the runner unaware of `StageOutput`.
- StageOutput round-trip:
  - `serialize_stage_output` / `deserialize_stage_output` (`include/souxmar/pipeline/registry_dispatcher.h`) — wire format for `Path`-kind outputs (the v0.0.1 case writers cover). `Mesh`/`Geometry`/`Field` return `nullopt` on serialize until plugin-side serializers land in Sprint 5.
  - Deserialize verifies the referenced file still exists; a vanished artifact is treated as a cache miss so the writer re-runs.
- Tests:
  - `tests/unit/test_pipeline_cache` extended — SHA-256 stability check (digest fills all 32 bytes, identical inputs produce identical digests), `DiskCache` round-trip / missing-key / empty-blob / `default_dir` honors override.
  - `tests/integration/test_cli_smoke` — invokes the real CLI binary via `std::system` against the cantilever-beam example. Asserts: `plugin list` enumerates in-tree plugins, `run` produces a well-formed VTU on disk, second `run` with the same `--cache-dir` marks the writer stage `[CACHED]`.

#### Sprint 4 push 1 — pysouxmar Python bindings

- New top-level subdirectory `bindings/python/` shipping the `pysouxmar` Python package.
  - `bindings/python/src/pysouxmar.cpp` — pybind11 module wrapping the C++ surface (parser, registry, loader, runner, cache).
  - `bindings/python/pysouxmar/__init__.py` — Python facade re-exporting the extension's symbols and documenting the API.
  - `bindings/python/pyproject.toml` — scikit-build-core build backend; `pip install ./bindings/python` produces a self-contained wheel with the souxmar libraries linked statically into the extension.
  - `bindings/python/CMakeLists.txt` — single CMakeLists that detects in-tree vs standalone (`pip install`) builds. Standalone bootstraps by adding the souxmar repo root with examples/tests/CLI/benchmarks off.
- **[pipeline-format v1]** Pipeline `Value` tree ↔ Python conversion in both directions:
  - `Null/Bool/Number/String/StageRef/List/Map` map to `None/bool/float/str/StageRef/list/dict`.
  - The `{from: stage_id}` YAML shorthand round-trips as a `pysouxmar.StageRef`. A plain dict `{"from": "stage_id"}` is also accepted on the Python side.
- Lifetime safety:
  - `pybind11::keep_alive<1, 2>` ties `PluginLoader` and `RegistryDispatcher` to the `Registry` they wrap, so a Python-level garbage collection of the registry cannot dangle a loader.
  - `LoadedPlugin` exposes the C++ move-only RAII semantics: drop the Python reference and the registry forgets the plugin's capabilities + the OS module is closed.
- Ergonomic surface:
  - `RunOptions.disk_cache_dir = "/path"` — assigning a path constructs a `DiskCache` and wires the `serialize_stage_output` / `deserialize_stage_output` callbacks automatically (no need to import the dispatcher serializers).
  - `RunResult.outputs` returns a `dict[str, dict]` keyed by stage id, with each value carrying `{"kind": "mesh"|"path"|...}` + kind-specific fields. Path-kind outputs include `{"path": "..."}`.
  - `parse_pipeline` / `parse_pipeline_file` raise `ValueError` carrying the YAML line+column on parse failure (no variant unwrapping needed Python-side).
- Build-system additions:
  - New `dev-python` CMake preset (in `CMakePresets.json`) — `Debug` build with `SOUXMAR_BUILD_PYTHON=ON` and `VCPKG_MANIFEST_FEATURES=tests;python` so vcpkg fetches `pybind11` automatically.
  - `cmake/SouxmarOptions.cmake`'s pre-existing `SOUXMAR_BUILD_PYTHON` option is now wired to `add_subdirectory(bindings/python)` from the top-level `CMakeLists.txt`.
- Tests:
  - `bindings/python/tests/test_basics.py` — pure-Python unit tests covering version round-trip, ABI version, pipeline parsing (good + error paths), `Value` tree symmetry through `Stage.input`, `StageRef` shorthand, registry-empty contract, `DiskCache.default_dir` override.
  - `bindings/python/tests/test_end_to_end.py` — integration tests against the in-tree `hello-mesher` + `vtu-writer` plugins. Asserts: discovery enumerates plugins, load registers capabilities, full pipeline run produces a well-formed VTU, second run with `disk_cache_dir` set hits the disk cache for the writer stage. Skips cleanly if no built plugins are found.
- Examples + docs:
  - `bindings/python/examples/cantilever.py` — 20-line script demonstrating the full discover/load/parse/run flow. Mirrors the Sprint 3 cantilever-beam C example.
  - `bindings/python/README.md` — install, quick start, API surface table, lifetime rules, roadmap to Sprint 4 push 2/3 and Sprint 5.

#### Sprint 4 push 2 — parallel runner + manifest-driven reentrancy

- **Parallel runner** (`include/souxmar/pipeline/parallel_runner.h`, `src/pipeline/parallel_runner.cpp`):
  - New `run_pipeline_parallel(...)` schedules independent DAG branches across an inline thread pool of size `RunOptions::max_workers`. Worker pulls from a ready queue, runs the stage (cache → dispatch under reentrancy guard → cache put), decrements dependents' in-degree, re-enqueues anything that becomes ready.
  - `run_pipeline(...)` (the public entry) dispatches into the parallel impl whenever `max_workers > 1`; `max_workers <= 1` keeps the original sequential path.
  - Stage results are emitted in declaration order regardless of completion order — same shape downstream consumers see from the sequential runner.
  - Stop-on-failure semantics: in-flight stages complete naturally, no new stage starts; downstream stages of a failed stage are marked `Skipped`.
- **`ReentrancyGuard`** (same header) — per-plugin `std::mutex` map. `acquire(plugin_id, threading)` returns a `unique_lock` that owns the per-plugin mutex for `SingleThreaded` / `InternalParallel`, and is empty for `Reentrant`. Plugin granularity means two capabilities from the same plugin share a lock; capabilities from different plugins overlap freely even when both declare single-threaded.
- **Threading model in the registry**:
  - `CapabilityEntry` gains a `threading` field (`include/souxmar/plugin/registry.h`).
  - `Registry::add_mesher` / `add_solver` / `add_writer` accept an optional `ThreadingModel` argument (defaults to `SingleThreaded` — the safe choice).
  - C ABI bridges (`add_mesher_c` etc.) read the loader-stamped `current_plugin_threading_` slot, mirroring the existing `current_plugin_id_` pattern.
  - `PluginLoader::load` sets `current_plugin_threading_ = discovered.manifest.threading` before invoking `souxmar_plugin_register_v1` and clears it after — same protocol as the plugin id slot.
  - New accessor: `Registry::find_threading(capability_id) -> std::optional<ThreadingModel>`.
- **`IDispatcher` extensions** (`include/souxmar/pipeline/runner.h`):
  - New optional virtual `plugin_id(capability_id)` — defaults to the capability id (over-serialises rather than under-).
  - New optional virtual `plugin_threading(capability_id)` — defaults to `SingleThreaded` (safe assumption when a dispatcher does not know).
  - `RegistryDispatcher` overrides both to consult the underlying registry.
- **`RunOptions::max_workers`** — `0`/`1` = sequential; `>1` = parallel up to that many workers (clamped to `min(max_workers, num_stages)`).
- Tests:
  - `tests/unit/test_parallel_runner.cpp` — mock dispatcher with sleep + atomic concurrency counter proves: independent stages run in parallel, dependent stages serialise, two `SingleThreaded` stages of the *same* plugin serialise, two `SingleThreaded` stages of *different* plugins still overlap, `stop_on_first_failure` marks downstream `Skipped`, `max_workers=1` produces a valid result, validation errors short-circuit before any worker spawns. Direct unit tests of `ReentrancyGuard` for `Reentrant` (no-op) and `SingleThreaded` (blocks) cases.
- Python:
  - `RunOptions.max_workers` exposed via `pysouxmar`. `bindings/python/tests/test_end_to_end.py` adds a parallel run of the cantilever pipeline asserting the expected result shape.
  - `bindings/python/README.md` documents `max_workers` + the per-plugin reentrancy contract.
- Build:
  - `src/pipeline/CMakeLists.txt` adds `parallel_runner.cpp` and links `Threads::Threads` (PUBLIC, so consumers don't have to repeat the find).

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
