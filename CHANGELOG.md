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

#### Sprint 4 push 3 — agent tool surface v1 + 5 tools

- **`libsouxmar-ai`** new library (`src/ai/`, `include/souxmar/ai/tool.h`):
  - `Tool` declaration: name, description, category, `Confirmation` policy (Auto / ConfirmOnce / ConfirmAlways), input/output schema docs, handler `std::function`.
  - `ToolRegistry` — O(1) lookup by name, sorted `list()`, mutable so tests can override v1 defaults.
  - `ToolResult` — structured `{data: Value, summary: string, error: optional<ToolError>}`. `ToolError` is `{code, message, suggestion}` per docs/AI_INTEGRATION.md ("model recovers, not retries").
  - `ToolContext` — runtime services (`Registry*`, `IDispatcher*`, `Cache*`) + per-session metadata bag (`session_state: Value*` plus an opt-in owning slot via `take_session_state`) + focus handles (`mesh_handle`, `geometry_handle`, `field_handle`) that mesh/solve update as a side effect.
  - `ConfirmationPolicy` — per-tool overrides, `confirmed_once` set, prompter callback. Default behaviour: tools at Confirmation > Auto without a prompter return `NOT_CONFIRMED` (recoverable, not a crash).
  - `dispatch_tool(...)` — name lookup → confirmation gate → handler invocation with full exception isolation (every throw lands as a `ToolError{code="INTERNAL"}`).
- **[agent-tool v1]** Five v1 tools (the docs/AI_INTEGRATION.md v1 catalogue):
  - `read_geometry_summary` (Read, Auto) — reads inline geometry input or `session_state['geometry']`, returns counts + bbox + tag list.
  - `mesh` (Mesh, Auto) — dispatches a registered `mesher.*` capability via `ToolContext.dispatcher`; stashes the resulting Mesh on `ctx.mesh_handle` for downstream tools; returns `{capability_id, num_nodes, num_cells}`.
  - `set_bc` (BC, ConfirmOnce) — validates tag/type/value; appends a BC to `session_state['boundary_conditions']`; rejects unknown BC types with `INVALID_ARGUMENT`.
  - `solve` (Solve, ConfirmAlways — the runtime / cost call) — requires a prior `mesh` call; dispatches `solver.*`; stashes the Field on `ctx.field_handle`; returns `{capability_id, location, kind, num_components}`. Wraps the session mesh as a synthetic upstream so `RegistryDispatcher`'s solver path picks it up by the `mesh: {from: ...}` convention.
  - `screenshot_viewport` (Read, ConfirmOnce) — stub returning `NOT_AVAILABLE` in the headless library + CLI build, with a structured suggestion (the desktop app build supersedes it in a later push).
- **`Value ↔ YAML` helpers** (`include/souxmar/pipeline/value.h`): `parse_value_yaml(src)` and `emit_value_yaml(value)` (deterministic indented emitter, recognises the `{from: stage_id}` StageRef shorthand). Used by the CLI agent shim and Python tests; stable across yaml-cpp versions because the emitter is hand-rolled.
- **CLI** (`src/cli/main.cpp`):
  - `souxmar agent list` — pretty-prints every registered tool with category + confirmation default.
  - `souxmar agent invoke <tool> [--input <yaml>] [--input-file <path>] [--yes]` — parses inputs, discovers + loads plugins, constructs a ToolContext, invokes the tool, prints the summary + YAML-emitted result.
  - Arg parser refactor: replaced the single positional with a `std::vector<std::string> positionals` so `agent invoke <tool>` works alongside `plugin list` and `run <pipeline>`.
- **Python** (`bindings/python/src/pysouxmar.cpp`):
  - New `pysouxmar.ai` submodule exposing `Confirmation`, `ToolError`, `ToolResult`, `Tool`, `ToolRegistry`, `ToolContext`, `ConfirmationPolicy`, `default_v1_tools`, `dispatch_tool`.
  - `ToolContext.session_state` is a transparent property: assigning a Python dict takes ownership via the new `take_session_state` helper; reading returns a Python view of the current Value tree (so tools that mutate it during dispatch round-trip back).
  - `pysouxmar.parse_value_yaml` / `emit_value_yaml` exposed for symmetric debugging.
  - v1 limitations documented: `ConfirmationPolicy.prompter` not yet exposed (use `overrides` to whitelist); Mesh / Field handles stashed by mesh / solve are not yet inspectable from Python.
- **Tests**:
  - `tests/unit/test_ai_tools.cpp` — framework (Auto / ConfirmOnce / ConfirmAlways / DENIED / NOT_CONFIRMED / override / exception → INTERNAL), default v1 registry contents, every tool's success + error path (mesh against a fake mesher vtable, solve precondition, set_bc append semantics, screenshot stub), plus Value↔YAML round-trip.
  - `bindings/python/tests/test_agent_tools.py` — Python mirror of the same surface, including a real-plugin mesh test (skips cleanly without built plugins).

#### Sprint 5 push 1 — plugin conformance suite v1 + ABI freeze gate

- **[ABI v1]** New plugin conformance suite (`include/souxmar/plugin/conformance.h`, `src/plugin-host/conformance.cpp`) — 10 v1 checks (C001–C010) covering the manifest, the load chain, the manifest↔registry mapping, the threading-model contract, and the load/unload cycle. Stable check ids; ratchet-only growth per [ADR-0004](docs/adr/0004-plugin-conformance-suite.md).
  - C001  manifest ABI version matches host
  - C002  manifest binary file resolves to an existing path
  - C003  plugin binary loads (dlopen / LoadLibrary succeeds)
  - C004  `souxmar_plugin_register_v1` symbol is exported
  - C005  registration returns success
  - C006  every capability announced in the manifest is registered
  - C007  no unannounced capabilities are registered
  - C008  each registered capability's threading model matches the manifest declaration
  - C009  plugin unload removes every capability owned by this plugin
  - C010  three load/unload cycles leave the registry at the same baseline
- **`souxmar-conformance`** runnable binary (`tools/conformance/`): `souxmar-conformance <search-dir> [--plugin-id <id>] [--quiet] [--summary-only]`. Discovers every plugin under the search dir, runs all 10 checks against each, prints a results table, exits 0 iff every plugin passes every check. Sysexits-style codes (0 / 1 / 2 / 3).
- `tests/integration/test_conformance.cpp` — the **Sprint 5 ABI freeze gate**. Runs `run_conformance` against all three in-tree plugins (hello-mesher, hello-writer, vtu-writer); asserts every check Passes for each, plus a negative test verifying a deliberately-mismatched ABI trips C001 and Skips the downstream chain.
- Docs:
  - New `docs/adr/0004-plugin-conformance-suite.md` explaining the 10 checks, the freeze-candidate process, and the ratchet policy.
  - `docs/PLUGIN_SDK.md` § Conformance updated to match the actual v1 surface that landed (was placeholder copy).
- Build:
  - `src/plugin-host/CMakeLists.txt` builds `conformance.cpp` into `libsouxmar-plugin`.
  - New top-level `tools/` subdirectory; `tools/conformance/CMakeLists.txt` builds the binary when `SOUXMAR_BUILD_CLI=ON` (default).

#### Sprint 5 push 2 — agent catalogue to 8 tools + audit log + session budget

- **3 new agent tools** (`src/ai/tools/`):
  - `query_field` (Read, Auto) — min/max/mean over `ctx.field_handle->data()`, with NaN filtering and finite-vs-total counts. Reports the field's location / kind / num_components labels so the agent can reason about scalar magnitude vs. vector component interpretation.
  - `compute_field` (Postproc, ConfirmOnce) — ships as an honest stub returning NOT_AVAILABLE. The postproc C ABI required by this tool lands in Sprint 5 push 3 alongside the heat-conduction solver; extending the existing solver vtable to accept an input field would be an ABI break right before freeze candidacy, so we ratchet rather than rush.
  - `propose_pipeline` (Pipeline, Auto) — round-trips a structured spec through `emit_value_yaml` + `parse_pipeline`. The parser is the ground truth on what "valid" means, so a draft that passes here is guaranteed to load at `souxmar run` time. Read-only by design — `write_pipeline` (future) is the matching commit tool.
  - `default_v1_tools()` registry size goes from 5 → 8. The Sprint 5 plan's "tool count to 8" commitment is satisfied.
- **`souxmar::ai::AuditLog`** (`include/souxmar/ai/audit_log.h`, `src/ai/audit_log.cpp`):
  - Append-only YAML one-liner per dispatch: `{ts: <iso8601 utc>, tool: ..., outcome: ..., duration_ms: N, input_hash: <sha256>, summary: "...", budget: {in, out, total, max_total}}`.
  - Thread-safe (internal mutex around the ofstream). Cross-process safe at line granularity on POSIX (`O_APPEND` + PIPE_BUF guarantee); best-effort on Windows.
  - `default_path()` resolves `$SOUXMAR_AUDIT_LOG` → `<project_root>/.souxmar/chat/audit.log` → `cwd/.souxmar/chat/audit.log`.
  - Parent directories created lazily on construction; permission failures throw `filesystem_error` rather than silently swallowing.
- **`souxmar::ai::SessionBudget`** (same header):
  - Per-session `{max_input, max_output, max_total} × {consumed_input, consumed_output}` counters.
  - `record(in_delta, out_delta)` increments the counters and fires `on_threshold(pct, axis, current)` exactly once per crossed (axis, threshold) pair. Thresholds: 50% / 80% / 100% of each `max_*_tokens`. `max == 0` means unlimited on that axis (callback suppressed).
  - Used by tools that talk to AI providers — the v1 catalogue today doesn't, so audit lines carry `budget: {in: 0, out: 0, ...}` for now. The plumbing is here for the desktop / API client work in later pushes.
- **`ToolContext` extensions** — non-owning `audit_log`, `budget` pointer slots. `dispatch_tool` reads both: every call records one audit entry (with timing via `std::chrono::steady_clock`), and the budget snapshot rides along.
- **Stable audit outcome vocabulary**: `ok` / `fail` / `denied` / `not_confirmed` / `not_found`. The dispatcher's `outcome_token` mapping is the single source so external log parsers can group / count without dispatch-internals knowledge.
- **CLI**: new `--audit-log <path>` flag on `souxmar agent invoke`. Default behaviour writes to `default_path()`; the flag overrides. A permission failure surfaces a warning but does NOT block the tool from running.
- **Python**:
  - `pysouxmar.ai.AuditLog`, `pysouxmar.ai.SessionBudget` bound; `ToolContext.audit_log` and `.budget` are non-owning Python properties (pybind11 `keep_alive` ties their lifetimes).
  - `on_threshold` callback for `SessionBudget` is intentionally not bound in v1 (signature involves `std::string_view` + struct ref). Python users watch `consumed_total` after each `record()`. First-class callback lands in Sprint 6.
- **Tests**:
  - `tests/unit/test_ai_tools.cpp` extended — registry count assertion now 8; `query_field` precondition + aggregation paths; `compute_field` stub contract; `propose_pipeline` good + bad spec paths; `AuditLog` round-trip + env-override default path; `SessionBudget` crosses-once threshold semantics; full dispatch → audit-line wiring.
  - `bindings/python/tests/test_agent_tools.py` mirrors the same surface from Python, including a 3-call audit-log roundtrip that asserts on the YAML line content + count.
- **Build**: `src/ai/CMakeLists.txt` gains `audit_log.cpp`, `tools/query_field.cpp`, `tools/compute_field.cpp`, `tools/propose_pipeline.cpp`.

#### Sprint 5 push 3 — postproc C ABI surface + heat solver + scalar-magnitude

- **[ABI v1]** New capability namespace `postproc.*` with a dedicated C ABI surface:
  - `include/souxmar-c/postproc.h` — `souxmar_postproc_vtable_t` (`abi_version`, `compute_fn`, `destroy_fn`) + `souxmar_postproc_options_t`. `compute_fn` takes `(mesh, input_field, value_bag, options, &out_field, user_data)` — the field input parameter is the key difference from `solver.*`.
  - `souxmar_registry_add_postproc()` registration entry (see [ADR-0005](docs/adr/0005-postproc-c-abi.md) for why this is a new vtable instead of an extension to `solver.*`).
  - **The solver C ABI is unchanged** — existing solver plugins keep compiling. The ratchet rule from ADR-0004 (no breaking changes pre-freeze) is honored.
- **Registry + dispatcher extensions** (`include/souxmar/plugin/registry.h`, `src/plugin-host/registry.cpp`, `src/pipeline/registry_dispatcher.cpp`):
  - `CapabilityKind::Postproc` (value 3), `PostprocEntry`, `add_postproc` / `add_postproc_c` / `find_postproc`.
  - `RegistryDispatcher::dispatch_postproc` requires both `mesh: {from: ...}` and `field: {from: ...}` upstream; passes them through to the plugin's `compute_fn` under `plugin::guard_call`; wraps the returned field as a `StageOutput::Kind::Field` with the standard `souxmar_field_free` deleter.
  - Namespace routing table now: `mesher.*` / `solver.*` / `writer.*` / `postproc.*`.
- **`compute_field` agent tool — activated**: the Sprint 5 push 2 stub is replaced. The tool wraps `ctx.mesh_handle` + `ctx.field_handle` as synthetic upstream stages, dispatches the named `postproc.*` capability via `RegistryDispatcher`, and stashes the resulting Field on `ctx.field_handle` for downstream tools. Returns `{capability_id, location, kind, num_components, num_time_steps}`. Marked `ConfirmAlways` (runtime / cost surface).
- **`examples/plugins/heat-solver/`** — registers `solver.heat.linear`. Reads `num_time_steps` / `dt` / `tau` / `t_steady` from the value bag; produces a nodal scalar `Field` with multi-step temperature evolution: `T(node_i, step_j) = T_steady · (1 − exp(−t_j/τ)) · ½(1 + cos(π·x_norm))`. Demonstrates `Field` time-series across the C ABI — the Sprint 5 deliverable.
- **`examples/plugins/scalar-magnitude/`** — registers `postproc.scalar_magnitude`. Takes any-kind input field (scalar / vector / tensor), emits a scalar Field with same `count` × `num_time_steps`. Per-location output is `sqrt(sum_c v_c²)` (Frobenius norm). Declared `reentrant` — pure functional transform, no shared state — so the parallel runner can fan out concurrent calls.
- **Conformance gate**: `tests/integration/test_conformance.cpp` extended with the two new plugins. The freeze gate now covers five in-tree plugins (hello-mesher, hello-writer, vtu-writer, heat-solver, scalar-magnitude) — all 10 v1 checks pass on every one.
- **New integration test**: `tests/integration/test_postproc_end_to_end.cpp` runs `mesh → heat → scalar_magnitude` end-to-end against the in-tree plugins. Asserts every stage `Executed`, that the postproc output is a Field with `num_time_steps == 3` and `count == 4` (matches the upstream heat solver), and that a missing `field: {from: ...}` upstream is rejected by the dispatcher.
- **Unit test updates**: `tests/unit/test_ai_tools.cpp` — `compute_field` is no longer NOT_AVAILABLE. New assertions: `INVALID_ARGUMENT` for a missing `capability_id`, `PLUGIN_NOT_FOUND` / `PRECONDITION_FAILED` for an empty registry. Python pytest mirrors the same.
- **Docs**: ADR-0005 documents the decision, the three alternatives considered (extend solver vtable / smuggle through value bag / subprocess), and the consequences.

#### Sprint 5 push 4 — bulk-buffer ABI for large mesh transfer (ADR-0006)

- **[ABI v1]** New opaque handle `souxmar_buffer_t` (`include/souxmar-c/buffer.h`, `src/core/c_abi_buffer.cpp`):
  - `souxmar_buffer_new(bytes)` / `_free` / `_data` / `_data_const` / `_size` / `_alignment` (≥16-byte SIMD-friendly).
  - Heap-backed v1 implementation. Internal header carries a magic word + size + allocation pointer; double-free is a no-op rather than a corruption (poisons the magic on first free).
  - Forward-compatible with the v2 mmap-backed implementation per [ADR-0006](docs/adr/0006-memory-mapped-buffer-protocol.md) — no plugin-side change when v2 lands.
- **`souxmar_mesh_from_buffers()`** (`include/souxmar-c/mesh.h`, `src/core/c_abi_mesh.cpp`):
  - `souxmar_mesh_buffers_t` descriptor: `node_coords` (3·num_nodes doubles) + `cell_types` (uint16 per cell) + `cell_connectivity` (flat uint64 node ids) + `cell_offsets` (num_cells+1 uint64 prefix sum) + optional `cell_tags` (int32 per cell, NULL = untagged).
  - Single-call mesh ingest — amortizes the ~50 ns per-call ABI overhead the per-element `souxmar_mesh_add_node` / `add_cell` path pays.
  - Full validation: required-pointer null check, each buffer's size matches its declared count, offsets monotonic + zero-prefixed + terminator matches connectivity length, every cell type is a known `SOUXMAR_ET_*`, per-cell node count matches the element type's expected count, every node index is in range. `out_status` carries a structured rejection reason on any failure.
  - Pre-reserves the underlying `Mesh` vectors from the declared counts so the hot loop is amortised O(1) per element.
- **Latent bug fixed**: `souxmar_value_t` was referenced throughout the C ABI (`solver.h`, `value.h`) but never `typedef`'d. The in-tree plugins all compile as C++, where `struct X` aliases `X` automatically, masking the issue. Pure-C plugin authors would have hit an "unknown type" error. Typedef added to `souxmar-c/types.h` next to the new `souxmar_buffer_t`.
- **Benchmark**: `benchmarks/bench_mesh_construction.cpp` compares per-element vs bulk construction across N³ tetrahedral grids (N = 8 / 16 / 32 / 64). First in-tree benchmark — `benchmarks/CMakeLists.txt` is the seed for the nightly perf-regression CI work in the Sprint 5 plan.
- **Tests**: `tests/unit/test_c_abi_buffer.cpp` covers the buffer round-trip, alignment guarantee, null-safety, every documented bulk-mesh validation failure path (null inputs, wrong sizes, non-monotonic offsets, unknown element type, mismatched node count, out-of-range node index), and a bulk-vs-incremental equivalence test against a 5-node 2-tet mesh.
- **ADR-0006**: documents the design, the v1-heap / v2-mmap rollout plan, the alternatives considered (raw pointers, shared-memory from day 1, variable-batch per-element setters), and the freeze-ratchet implications.
- **Build**: `src/core/CMakeLists.txt` picks up `c_abi_buffer.cpp`; new `benchmarks/` subdirectory wired to the existing top-level `SOUXMAR_BUILD_BENCHMARKS` gate.

#### Sprint 5 push 5 — DX + Platform: plugin tutorial, thermal-fin example, perf-nightly CI

- **`docs/tutorials/plugin-authoring.md`** — end-to-end walkthrough from `cmake --build` to a `souxmar-conformance`-passing plugin. References the in-tree hello-mesher (mesher), heat-solver (solver with time-series Field), hello-writer / vtu-writer (writer), scalar-magnitude (postproc) as canonical examples. Sections: project layout, manifest, single export, per-namespace vtable patterns, CMake with `souxmar_add_plugin`, conformance verification, distribution (per-platform plugin prefixes), troubleshooting the common first-attempt failures (C001 / C002 / C006 / C007).
- **`examples/thermal-fin/`** — second runnable example. 4-stage pipeline (mesh → heat → scalar_magnitude → write) exercising every Sprint 5 capability namespace end-to-end:
  - The hello-mesher placeholder produces a unit tet (Sprint 6's Gmsh adapter swaps this for real CAD-loaded geometry with the same YAML shape).
  - The heat solver writes a 5-step nodal scalar `Field` per the Sprint 5 time-series demo.
  - The scalar-magnitude postproc round-trips the Field through the new `postproc.*` C ABI (ADR-0005).
  - The VTU writer dumps the mesh for ParaView inspection.
  - README walks the runtime steps, the knobs the user can vary, and the diff to `cantilever-beam`.
- **`.github/workflows/perf-nightly.yml`** — nightly + on-demand + PR-gated (on relevant paths) benchmark CI. Builds Release + `SOUXMAR_BUILD_BENCHMARKS=ON`, runs the mesh-construction bench in JSON format with 3 repetitions × 0.2 s min time, compares to `benchmarks/baselines/main.json` (skip + warn when absent), uploads the report as an artifact, fails on >10% regression.
- **`tools/perf-compare/compare.py`** — Google-Benchmark-JSON diff utility. Prefers `_mean` aggregates when present, falls back to raw run times. Per-benchmark table with delta% + visual markers (`⚠` regression, `↓` improvement). Single-source threshold for tuning when shared-runner noise floors shift.
- **`benchmarks/baselines/README.md`** — documents the baseline-update workflow, the deliberate "commit, don't auto-rotate" policy, the regression threshold rationale, what belongs in `baselines/` vs. workflow artifacts.

This closes the Sprint 5 DX + Platform items called out in `docs/SPRINT_PLAN.md`. The baseline file itself is intentionally NOT committed in this push — the "baseline established" exit criterion is the first follow-on PR that lands a `benchmarks/baselines/main.json` generated on the CI hardware tier.

#### Sprint 5 push 6 — ABI v1 frozen-candidate declared

- **[ABI v1]** **`SOUXMAR_ABI_FREEZE_CANDIDATE` macro** set in `include/souxmar-c/abi.h`, declaring the start of the two-sprint soak. Formal-freeze target: **2026-06-08**. Status block in the header names the soak rules inline so plugin authors hitting the header see the contract immediately.
- **[ABI v1]** [ADR-0007 — ABI v1 freeze-candidate](docs/adr/0007-abi-v1-freeze-candidate.md) lands the full mechanics: the 14 frozen headers (abi / status / types / plugin / registry / mesher / solver / writer / postproc / mesh / geometry / field / value / buffer), the ratchet rules during soak (additive minor surfaces only — zero-init forward-compat by construction), the cancellation triggers (any breaking change, two consecutive conformance failures, confirmed perf regression), and the exit criteria for State 2 → State 3 (clean conformance + ASAN/TSAN nightly + perf-nightly within threshold).
- **`docs/PLUGIN_SDK.md` § Versioning** gains a "Current freeze status: **frozen-candidate v1** (since 2026-05-11)" callout pointing at the ADR and the governance mechanics.
- **`docs/GOVERNANCE.md` § ABI freeze process** documents the three-state model (pre-freeze → frozen-candidate → formally frozen) the project commits to use for every future ABI version. The State 2 → State 3 merge carries the same two-maintainer-approval bar as any Tier 3 change.
- **`README.md` § Status** refreshed: replaces the stale "Sprint 0 scaffolding" snapshot with the current runnable surface (CLI / Python / plugin SDK / 5 in-tree plugins / 2 runnable examples / parallel runner / agent tool v1 / perf-nightly CI) and an honest list of what's still scoped out of Phase 0 (no Tauri yet, no OCCT / Gmsh / FEniCSx adapters yet, agent tool surface still offline).

This push lands no code under `src/` — it is the contractual moment Sprint 5 has been building toward across pushes 1–5. The first follow-on PR opens the soak tracking issue; PRs landing during soak that touch any frozen header must inspect the ratchet rules in ADR-0007.

#### Sprint 6 push 1 — mesh-quality postproc + `query_mesh_quality` agent tool

- **`souxmar::core::quality`** (`include/souxmar/core/mesh_quality.h`, `src/core/mesh_quality.cpp`): pure-math metric functions on per-element coordinate arrays. v1 catalogue: `SignedVolume`, `EdgeRatio`, `MinDihedralDeg`. Tet4 and Tri3 supported; other element types return NaN (Hex8 / Quad4 / higher-order land when an in-tree mesher emits them). `evaluate` / `evaluate_all` are stateless; `summarise` aggregates a whole-mesh quality field with NaN-skip semantics and exposes per-metric stats plus advisory threshold counters (inverted / sliver-dihedral / extreme-aspect / unsupported). Metric numeric ids are STABLE — they pin the component layout of the field the postproc plugin emits.
- **`examples/plugins/mesh-quality/`** — sixth in-tree reference plugin. Registers `postproc.mesh_quality`; reads the mesh through `souxmar-c/` accessors, calls `quality::evaluate_all` per cell, emits a per-cell `FieldKind::Vector` (3-component) Field. Declared `reentrant`. The plugin is self-contained on the C ABI: it pulls `src/core/mesh_quality.cpp` directly into its compile list rather than linking `libsouxmar-core`, preserving the docs/PLUGIN_SDK.md contract while keeping the math DRY across the in-tree consumers.
- **`query_mesh_quality` agent tool** (`src/ai/tools/query_mesh_quality.cpp`) — `default_v1_tools()` registry size 8 → 9. Confirmation::Auto (read-only inspection). Reuses an existing 3-component cell-located Field from `ctx.field_handle` when present; otherwise dispatches `postproc.mesh_quality` against `ctx.mesh_handle` through `ctx.dispatcher` (same synthetic-upstream pattern `compute_field` uses) and stashes the result for follow-up tools. Returns `{metrics: {<name>: {min, max, mean, finite, total}}, flags: {cells_inverted, cells_sliver_dihedral, cells_extreme_aspect, cells_unsupported}, num_cells, source}`.
- **Conformance gate**: `tests/integration/test_conformance.cpp` extended — the freeze gate now covers six in-tree plugins (hello-mesher, hello-writer, vtu-writer, heat-solver, scalar-magnitude, mesh-quality), all 10 v1 checks green on every one. **No frozen-header surface was touched; the ABI v1 freeze-candidate soak rolls forward unchanged.**
- **Tests**:
  - `tests/unit/test_mesh_quality.cpp` pins the math: regular-tet exact values (arccos(1/3) ≈ 70.529° dihedral, edge_ratio == 1, volume > 0), orientation flip → negative volume, sliver tet → sub-1° dihedral, stretched tet → edge_ratio > 50, Tri3 right-angle case, unsupported element type → NaN, degenerate (coincident-vertex) tet → volume 0 + NaN for ratio / dihedral, summariser threshold counters round-trip.
  - `tests/integration/test_mesh_quality_plugin.cpp` runs `mesh → postproc.mesh_quality` end-to-end against the in-tree hello-mesher, inspects the resulting `FieldKind::Vector` payload, and asserts the summariser flags zero pathologies on the (well-formed) tet.
  - `tests/unit/test_ai_tools.cpp` registry assertion bumped 8 → 9; new `RequiresMeshHandle` precondition test for `query_mesh_quality`.
  - `bindings/python/tests/test_agent_tools.py` mirror catalogue assertion bumped 8 → 9.
- **Build**: `src/core/CMakeLists.txt` adds `mesh_quality.cpp`; `src/ai/CMakeLists.txt` adds `tools/query_mesh_quality.cpp`; `examples/CMakeLists.txt` adds the new plugin subdirectory; `tests/integration/CMakeLists.txt` depends on `mesh_quality` so the integration suite has the plugin to load.

#### Sprint 6 push 2 — manifest schema validation hardening

- **Stable rejection codes** for every manifest failure mode (`include/souxmar/plugin/manifest.h`):
  - New `enum class ManifestRejection` with stable string tokens: `ok`, `toml_syntax`, `missing_field`, `wrong_type`, `abi_unsupported`, `empty_capabilities`, `unknown_threading`, `invalid_capability_namespace`, `invalid_plugin_id`, `invalid_version`, `file_io`. Append-only; numeric values are stable so on-disk audit log records keep parsing.
  - `ParseError` extended with `code`, `column`, `field` (dotted path, e.g. `"plugin.abi"`). Existing brace-init `{message, line}` still compiles — the new fields default — so every call site upstream of the parser keeps working unchanged.
- **Tighter validation**:
  - Capability strings must use one of the host-allow-list namespaces (`reader`, `writer`, `mesher`, `element`, `solver`, `postproc`). `garbage.foo` is rejected at parse time rather than silently registered. New `is_allowed_capability(id)` + `allowed_capability_namespaces()` are exposed for tooling.
  - Plugin id must look like reverse-DNS (at least one `.`, alphanumerics / `.` / `-` / `_`, no whitespace, no path separators). The marketplace publish step tightens further at upload time; this layer catches the obvious classes today.
  - Version must look like SemVer (`major.minor.patch[-pre][+build]`). "abc" / "1" / "1.2" are rejected.
  - Malformed-TOML errors now surface both `line` and `column` from toml++.
- **Additive optional manifest fields** under the ABI v1 soak ratchet (forward-compatible by construction — missing → default):
  - `plugin.description` (one-line summary) · `plugin.documentation` (URL) · `plugin.tags` (string array for the plugin index) · `plugin.min_souxmar_abi_minor` (plugin declares the minimum minor ABI it needs; the host today recognises it as advisory metadata, with the loader gate landing alongside the first minor bump).
  - `examples/plugins/mesh-quality/souxmar-plugin.toml` is updated as the canonical example with the new fields populated.
- **Structured discovery rejections** (`include/souxmar/plugin/discovery.h`): `DiscoveryRejection` gains a `code` enum (`cannot_iterate_search_path` / `manifest_parse_failed` / `binary_not_found` / `binary_unrecognised_extension`) plus an `optional<ManifestRejection> manifest_code` populated when the rejection came from the parser. `{candidate_path, reason}` brace-init still compiles.
- **`souxmar plugin list` upgrade** (`src/cli/main.cpp`): rejection lines now print as `- <path>: [<discovery_code>/<manifest_code>] <reason>` so log readers can group failures by class without regex-on-message. The loaded-plugin listing also surfaces the new `description` and `tags` fields when present.
- **Python bindings** (`bindings/python/src/pysouxmar.cpp`): `pysouxmar.ManifestRejection` and `pysouxmar.DiscoveryRejectionCode` enums; the new `Manifest` fields (`description`, `documentation`, `tags`, `min_souxmar_abi_minor`); `DiscoveryRejection.code` + `.manifest_code` properties.
- **Docs** (`docs/PLUGIN_SDK.md`): manifest example shows the new fields with comments; a short snippet illustrates the `[<code>] <reason>` format `souxmar plugin list` emits.
- **Tests**:
  - `tests/unit/test_manifest.cpp` — one assertion per rejection code (missing-field / wrong-type / abi-unsupported / empty-capabilities / unknown-threading / invalid-capability-namespace / invalid-plugin-id / invalid-version / toml-syntax with line+column). Round-trip for the additive fields (parse when present + default when absent). Token-stability assertion for all 11 `to_string(ManifestRejection)` values. New `CapabilityNamespace.AllowList` direct test.
  - `tests/unit/test_discovery.cpp` — every existing rejection test now asserts on the new `code`; new `BadCapabilityNamespaceManifestRejectedWithStructuredCode` test that walks the full plugin-host stack: bad manifest → discovery → structured rejection.
- **No frozen-header surface was touched** — `souxmar-c/*` is unchanged. The new structured-rejection surface is all C++; the ABI v1 freeze-candidate soak rolls forward unchanged.

#### Sprint 6 push 3 — agent tools 9 → 12

- **Four new agent tools** complete the docs/AI_INTEGRATION.md v1 catalogue:
  - **`set_material`** (`src/ai/tools/set_material.cpp`, BC / ConfirmOnce) — stages a material spec on `session_state['materials']`. Mirrors `set_bc`'s shape: `{tag, model, properties: {<key>: number|string, ...}, name?}`. Validates required fields; passes unknown keys through so future solver plugins can introduce material parameters without a tool upgrade.
  - **`list_plugins`** (`src/ai/tools/list_plugins.cpp`, Read / Auto) — walks `ctx.registry`, returns `{capabilities: [{id, kind, plugin_id, abi_version, threading}, ...], count_total, count_by_kind}`. Optional `{namespace: string}` filter routes through `Registry::list_capabilities_in_namespace`. This is the inventory call the agent makes before `mesh` / `solve` / `compute_field` / `export_results` so it picks a capability the host has actually loaded.
  - **`apply_pipeline_diff`** (`src/ai/tools/apply_pipeline_diff.cpp`, Pipeline / ConfirmOnce) — applies `{base, ops}` where `ops` are `{op: 'add'|'remove'|'set_input'|'replace', ...}`. The result is re-emitted via `emit_value_yaml` and re-parsed via `parse_pipeline`, so a returned draft is guaranteed to load at `souxmar run` time. A `remove` op that leaves a `{from: <id>}` dangling trips the parser; the tool surfaces `INVALID_ARGUMENT` with the parser's line/column rather than producing a broken draft. The matching `write_pipeline` (commit-to-disk) lands in Sprint 7.
  - **`export_results`** (`src/ai/tools/export_results.cpp`, Export / ConfirmAlways) — dispatches a registered `writer.*` capability against the session mesh + (optional) field via the synthetic-upstream pattern (`__session_mesh__` / `__session_field__`) `compute_field` / `query_mesh_quality` already use. The `path` is passed through in the stage input bag. ConfirmAlways because writers have observable side-effects (files appearing on disk).
- **`default_v1_tools()` catalogue size: 9 → 12.** `tests/unit/test_ai_tools.cpp` and `bindings/python/tests/test_agent_tools.py` registry assertions bumped to 12 and the full expected name set updated.
- **Tests**:
  - `set_material`: success path appends to `session_state['materials']`; missing `properties` is rejected with `INVALID_ARGUMENT`.
  - `list_plugins`: missing-registry → `INTERNAL`; empty registry → zero-result success path.
  - `apply_pipeline_diff`: add-stage round-trips through the parser; remove-with-dangling-reference is rejected with `INVALID_ARGUMENT` (the parser is the ground truth on what a valid pipeline is).
  - `export_results`: precondition failures (no mesh / no registry / unknown writer) all surface structured `ToolError` codes; no writer is invoked.
  - Python mirror covers the same four surfaces via the existing `sx.Registry()` / `sx.ai.dispatch_tool` bindings.
- **No frozen-header surface touched.** Four pure additions to `libsouxmar-ai`; the new tools talk to plugins exclusively through `RegistryDispatcher`. ABI v1 freeze-candidate soak rolls forward unchanged.

#### Sprint 6 push 4 — `reader.*` C ABI surface (ABI v1.1 ratchet)

- **[ABI v1.1]** `SOUXMAR_ABI_VERSION_MINOR` bumped **0 → 1**. **First additive minor ratchet event during the v1 freeze-candidate soak.** Per ADR-0007, additive minor surfaces are forward-compatible by construction: a v1.0 plugin keeps loading on a v1.1 host because every new symbol is opt-in, and a v1.1 plugin attempting to register a reader against a v1.0 host fails at symbol resolution time (conformance check C004 catches it). Soak rolls forward; this bump does NOT reset the soak window — only breaking changes do.
- **[ABI v1.1]** New header `include/souxmar-c/reader.h` and registration entry `souxmar_registry_add_reader`:
  - `souxmar_reader_vtable_t` with `read_fn(path, inputs, options, &out_mesh, &out_geometry, user_data)`. The plugin fills exactly one of `out_mesh` / `out_geometry` per the file format it consumes; the dispatcher's `dispatch_reader` routes the produced handle to the matching `StageOutput::Kind::Mesh` or `Kind::Geometry`.
  - `souxmar_reader_options_t`: `merge_coincident_nodes`, `coincidence_tolerance`, `preserve_tags`, `random_seed`.
  - Registry extensions: `CapabilityKind::Reader`, `ReaderEntry`, `add_reader` / `add_reader_c` / `find_reader`. Capability variant payload extended with `ReaderEntry`.
  - Dispatcher routing table now: `mesher.*` / `solver.*` / `writer.*` / `postproc.*` / **`reader.*`**.
- **`examples/plugins/stl-reader/`** — seventh in-tree reference plugin, **always-on** (no external dependencies). Registers `reader.stl`. Parses ASCII STL into a Tri3 mesh through the C ABI, deduplicating coincident vertices (quantised to 1e-7 in world coords) so adjacent facets share nodes — without dedup every cell carries 3 fresh nodes and topological adjacency is lost. Declared `reentrant`. Manifest declares `min_souxmar_abi_minor = 1` (the floor for the reader surface).
- **`examples/plugins/occt-reader/`** — opt-in OCCT-backed STEP / IGES reader, gated behind `-DSOUXMAR_WITH_OPENCASCADE=ON` + `find_package(OpenCASCADE)`. Registers `reader.step` AND `reader.iges` (shared vtable, switches on file extension). Walks `TopExp_Explorer` over the loaded `TopoDS_Shape` and emits vertices / edges / faces / solids into a souxmar Geometry with stable tag preservation. Declared `single-threaded` — OpenCASCADE's translator state isn't thread-safe. Not built in default CI; nightly builds with OCCT-bearing runners exercise it.
- **`examples/stl-cube/`** — first souxmar pipeline driven by a real input file. `reader.stl → postproc.mesh_quality → writer.vtu` against a 12-facet ASCII STL cube. After `souxmar run`, the user has a `cube.vtu` ParaView can open and a deduplicated 8-node / 12-cell Tri3 mesh inside it. README documents the cantilever-beam upgrade path: swap `reader.stl` for `reader.step` and the rest of the pipeline keeps working with no other changes — that's the namespace contract.
- **Conformance gate**: `tests/integration/test_conformance.cpp` now asserts `stl-reader` passes all 10 v1 checks (7 in-tree plugins green); the suite itself didn't change. C001 / C004 / C006 / C008 directly cover the new reader namespace's manifest + registration + threading.
- **Tests**:
  - `tests/integration/test_reader_end_to_end.cpp` — full `reader.stl → writer.vtu` flow against a generated STL fixture: discovery, load, parse, dispatch, file appears on disk. Asserts the deduplicated 8-node / 12-cell shape — pins the dedup logic against regression. Negative test for missing-`path` input asserts `dispatch_reader` surfaces the structured rejection.
- **Build**: `cmake/SouxmarOptions.cmake`'s pre-existing `SOUXMAR_WITH_OPENCASCADE` option now gates `examples/plugins/occt-reader/`. The plugin's CMakeLists calls `find_package(OpenCASCADE QUIET)` and `return()`s with a `STATUS` message if OCCT isn't installed — clean skip, no noisy failure.
- **Docs** (`docs/PLUGIN_SDK.md`): new Reader subsection documents the dual-output vtable contract and explicitly names this push as the first soak ratchet event.

#### Sprint 6 push 5 — second tetrahedral mesher (`mesher.tetra.grid` + opt-in Gmsh)

Closes the Sprint 6 plan exit criterion: **"A user can swap `mesher.tetra.native` for `mesher.tetra.gmsh` in pipeline YAML with no other changes; same result format."**

- **`examples/plugins/grid-mesher/`** — eighth in-tree reference plugin, **always-on**. Registers `mesher.tetra.grid`; reads the input Geometry's bounding box, builds an N×N×N tetrahedral grid via the 5-tet hex decomposition (same one `benchmarks/bench_mesh_construction.cpp` uses). `options.target_size` derives N from the largest bbox axis; default N=4. Declared `reentrant` — pure functional over its inputs. Tag inheritance is left as `-1` (untagged); a real CAD-aware mesher (gmsh-mesher, occt+netgen, ...) propagates face tags from the source geometry per the PLUGIN_SDK contract.
- **`examples/plugins/gmsh-mesher/`** — opt-in via `SOUXMAR_WITH_GMSH` + `find_package(Gmsh)`. Drives Gmsh's C++ API: `gmsh::model::occ::addBox` over the input bbox, `gmsh::model::mesh::generate(3)`, `getNodes` / `getElementsByType(4)` → souxmar mesh through the C ABI. Gmsh node tags are 1-based with gaps; the adapter remaps onto contiguous souxmar indices. Declared `single-threaded` because Gmsh holds process-global state (`gmsh::initialize()` is process-wide); the reentrancy guard serialises concurrent stages. `destroy_fn` calls `gmsh::finalize()` so the plugin doesn't leak past host exit. **Not built in default CI**; nightly Gmsh-bearing runners exercise it. The plugin's CMakeLists `find_package(Gmsh QUIET)` and `return()`s with a clean STATUS message when Gmsh isn't installed — `SOUXMAR_WITH_GMSH=ON` on a Gmsh-less machine produces a clear skip, not a noisy failure.
- **`examples/swap-mesher/`** — `grid.yaml` and `gmsh.yaml` differ by one line:
  ```diff
  -    plugin: mesher.tetra.grid
  +    plugin: mesher.tetra.gmsh
  ```
  README documents the contract: the upstream geometry stage, downstream postproc + write stages, input keys, and result schema are identical regardless of which mesher implements the namespace.
- **`tests/integration/test_swap_mesher.cpp`** — the always-on gate. Builds a unit-cube Geometry programmatically (8 corner vertices via `souxmar_geometry_add_vertex`), dispatches `mesher.tetra.grid` with `target_size=0.5`, asserts the produced Mesh has the pinned shape (27 nodes / 40 tets — N=3 nodes per axis × 5 tets per cube). Negative test: missing-geometry input is rejected with a structured `DispatchError` from `dispatch_mesher`. The Gmsh variant runs nightly with the opt-in flag; default CI exercises the contract via the always-on side.
- **Conformance gate**: `tests/integration/test_conformance.cpp` now asserts `grid-mesher` passes all 10 v1 checks (**8 in-tree plugins green**). The suite itself didn't change — the second mesher fits the same shape as the first.
- **Build**: `examples/CMakeLists.txt` adds `plugins/grid-mesher` unconditionally; `plugins/gmsh-mesher` only when `SOUXMAR_WITH_GMSH=ON`. `tests/integration/CMakeLists.txt` depends on `grid_mesher`.
- **No frozen-header surface was touched.** The mesher.* C ABI is unchanged; both new plugins build against the existing `souxmar-c/mesher.h`. ABI v1 freeze-candidate soak rolls forward.

#### Sprint 6 push 6 — Sprint 6 closeout: cost meter + budget config

- **First-class `SessionBudget.on_threshold` Python binding** (`bindings/python/src/pysouxmar.cpp`): pulls in `<pybind11/functional.h>`, wraps the C++ `std::function<void(int, std::string_view, const SessionBudget&)>` so Python callers write `b.on_threshold = lambda pct, axis, cur: ...`. Sprint 5 push 2 left this unbound; the desktop-app cost-meter work blocked on it.
  - The wrapper acquires the GIL on each callback and routes Python exceptions through `PyErr_WriteUnraisable` so a misbehaving callable can't unwind into the dispatcher's audit-write path. Per the SessionBudget contract, `on_threshold` callbacks must not throw.
  - Setting `b.on_threshold = None` clears the callback.
- **`.souxmar/budget.toml` per-project config** (`include/souxmar/ai/budget_config.h`, `src/ai/budget_config.cpp`): tomlplusplus-backed parser for a small `[budget]` block — `max_input_tokens` / `max_output_tokens` / `max_total_tokens`, all optional, default `0` (the SessionBudget "unlimited" sentinel). `parse_budget_config(toml)` / `parse_budget_config_file(path)` return a `std::variant<BudgetConfig, BudgetConfigError>`; the error carries the offending dotted field path (e.g. `budget.max_input_tokens`) so tooling can group failures by class. `BudgetConfig::apply_to(SessionBudget&)` sets the caps and explicitly **leaves the running counters and the threshold callback untouched** — so a project can hot-reload its budget without losing in-flight session state.
- **`souxmar agent invoke --budget-config <path>`** (`src/cli/main.cpp`): explicit override path; when omitted, the CLI auto-loads `.souxmar/budget.toml` from CWD if present. A parse error logs one warning line and continues unbudgeted (best-effort, never blocks the agent from running). When a budget is in effect, the CLI prints a one-line `budget: max_input=... max_output=... max_total=... (<path>)` summary so the user sees what's being enforced.
- **Python**: `pysouxmar.ai.BudgetConfig` class with the same `apply_to` method; `pysouxmar.ai.parse_budget_config(toml)` / `parse_budget_config_file(path)` raise `ValueError` on parse failures; `default_budget_config_path(project_root)` exposes the path resolver.
- **Tests**:
  - `tests/unit/test_budget_config.cpp` — valid parse, missing-fields-default-to-unlimited, negative rejection (with dotted field path), wrong-type rejection, malformed-TOML line reporting, `apply_to` semantics (caps set / counters preserved), `default_path` respects project_root, file round-trip via TempDir.
  - `bindings/python/tests/test_agent_tools.py` — `on_threshold` fires exactly once per crossed (axis, threshold) pair (50 / 80 / 100); clearing to `None` is idempotent; `BudgetConfig` round-trips through a real `.toml` file; `apply_to` leaves `consumed_*` alone.
- **Build**: `src/ai/CMakeLists.txt` adds `budget_config.cpp` and links `tomlplusplus::tomlplusplus`. The Python module pulls in `<pybind11/functional.h>` to support the `on_threshold` callback signature.
- **README Status section refreshed** with the Sprint 6 closing summary: ABI v1.1 frozen-candidate, **8 in-tree reference plugins** + 2 opt-in external adapters, 12 agent tools, three runnable examples, structured manifest rejection codes, per-project budget config. Honest about what's still scoped out of Phase 0.
- **No frozen-header surface touched.** Sprint 6 close-out is host-side and Python-side ergonomics — the `souxmar-c/*` surface remains at the ABI v1.1 shape declared in push 4. **Soak rolls forward unchanged; formal-freeze target 2026-06-08 stays on track.**

This closes Sprint 6 cleanly. Six pushes landed:
1. Mesh-quality metrics + 9th agent tool (`postproc.mesh_quality`, `query_mesh_quality`).
2. Manifest schema validation hardening (11 stable rejection codes, additive fields).
3. Agent tools 9 → 12 (`set_material`, `list_plugins`, `apply_pipeline_diff`, `export_results`).
4. `reader.*` C ABI surface — **the first soak ratchet event** — STL reader (always-on) + OCCT reader (opt-in).
5. Second tetrahedral mesher: grid-mesher (always-on) + Gmsh adapter (opt-in). Swap-test exit criterion met.
6. Cost-meter close-out: first-class `SessionBudget.on_threshold` from Python + `.souxmar/budget.toml` loader.

#### Sprint 7 push 1 — ABI v1 frozen FINAL

- **[ABI v1.1 FROZEN]** ADR-0007's two-sprint soak completed cleanly. Conformance was green across Sprint 6 for all 8 in-tree plugins; ASAN/TSAN nightly clean; perf-nightly within threshold; one additive ratchet event landed (the `reader.*` surface, Sprint 6 push 4) and behaved exactly as the ratchet predicted. **The v1 ABI is now immutable for the entire 1.x release series.**
- **`SOUXMAR_ABI_FREEZE_CANDIDATE` removed** from `include/souxmar-c/abi.h`. The status comment is rewritten to name [ADR-0008](docs/adr/0008-abi-v1-final-freeze.md) as the binding declaration; the new wording lists the post-freeze ratchet rules and points at the CI gate. Plugin authors that branched on the candidate macro during soak can now drop the conditional — the contract is permanent.
- **[ADR-0008]** [docs/adr/0008-abi-v1-final-freeze.md](docs/adr/0008-abi-v1-final-freeze.md) — the binding declaration. Inventory of locked headers, post-freeze ratchet rules (additive minor / bug-fix), CI enforcement, the path to a hypothetical v2 (one-major-overlap deprecation). Supersedes ADR-0007, which is now closed.
- **CI lockdown gate** — new `scripts/check-frozen-headers.sh` + an `abi-v1-lockdown` job in `.github/workflows/ci.yml`. PRs that touch any header in the v1 inventory must carry one of two commit-message markers in the PR range:
  - `Ratchet: additive minor surface (ADR-0008)` — new declarations / new headers / new `SOUXMAR_X_*` macros under a fresh prefix.
  - `Ratchet: bug-fix (ADR-0008) — <reason>` — comments / docs / declaration restorations. The Sprint 5 push 4 `souxmar_value_t` typedef fix is the documented precedent.
  Anything else fails the gate. The escape hatch is a Tier-3 ADR per `docs/GOVERNANCE.md` — in practice the trigger for a v2 ABI conversation.
- **`docs/PLUGIN_SDK.md`** — "Current freeze status" section rewritten to **frozen FINAL at v1.1**. Documents the ratchet markers and tells plugin authors the candidate macro is gone.
- **`README.md` § Status** — Sprint 7 push 1 marker; ADR-0008 reference; "frozen FINAL" language.
- **The commit landing this entry is tagged `abi-v1-frozen`** (annotated, signed by a release maintainer). Per `docs/GOVERNANCE.md` § ABI freeze process, this is the State 2 → State 3 transition: candidate → formally frozen. The two-maintainer-approval bar that gates Tier-3 changes was met before merge.
- **No frozen-header surface was modified.** The freeze IS the contract — we did not change a single function signature or struct layout in this push. We removed exactly one declaration (`SOUXMAR_ABI_FREEZE_CANDIDATE`), which was explicitly designated for removal at this milestone by ADR-0007, and rewrote the status comment. The ABI binary surface is byte-identical to v1.1 at end-of-Sprint-6.

#### Sprint 7 push 2 — second solver: elasticity-stub (always-on) + fenicsx-solver (opt-in)

- **`examples/plugins/elasticity-stub/`** — ninth in-tree reference plugin, **always-on**. Registers `solver.elasticity.linear`. Reads `load_magnitude` / `youngs_modulus` / `poisson_ratio` from the stage input bag and produces a per-node 3-component `Field` ("displacement") from the closed-form uniaxial-tension solution:
  ```
  u_x =  (load / E) * x
  u_y = -nu * (load / E) * y
  u_z = -nu * (load / E) * z
  ```
  Documented as a stub: ignores BC manifest, ignores mesh-dependent stiffness. The point is to give the agent eval suite (push 4 of this sprint), the cantilever-beam example, and the documentation tutorials a runnable elasticity solver in the default CI matrix without dragging DOLFINx + PETSc into every build. The closed-form happens to be the analytical answer the FEniCSx adapter validates against on the canonical patch test — the `validating-solver` skill walks the comparison.
- **`examples/plugins/fenicsx-solver/`** — opt-in DOLFINx-backed Poisson solver. Gated behind `-DSOUXMAR_WITH_FENICSX=ON` + `find_package(DOLFINX)`. Registers `solver.heat.fenicsx`. The real plugin: walks a souxmar Tet4 mesh into `dolfinx::mesh::create_mesh`, assembles the P1 Poisson weak form (`a(u,v) = ∫ ∇u · ∇v` and `L(v) = ∫ f v`) with FFCx-generated kernels (`poisson.py` is the canonical UFL source; a developer runs `ffcx` once and commits `poisson.c`), applies homogeneous Dirichlet BCs on the whole boundary, runs a PETSc Krylov solver, reads `u` back into a souxmar `Field`. Declared `single-threaded` because PETSc holds process-global MPI state. The CMakeLists `find_package(DOLFINX QUIET)` and `return()`s with a STATUS skip when DOLFINx isn't installed — `SOUXMAR_WITH_FENICSX=ON` on a DOLFINx-less machine produces a clear skip rather than a noisy linker failure. A separate CMake guard skips with an actionable message when `poisson.c` hasn't been generated. **Not built in default CI**; nightly DOLFINx-bearing runners exercise it.
- **`examples/plugins/fenicsx-solver/README.md`** documents the FFCx regen step, the validation expectation (within FEM discretisation error of `solver.heat.linear` on the same problem; 1e-2 relative bar in the agent eval suite), and the v1 limitations (Poisson only, homogeneous Dirichlet only, single-rank). Sprint 8 lifts the BC + elasticity restrictions via an additive minor ratchet (ADR-0008 compliant — the structured BC array threads through the value bag, not the vtable).
- **Conformance gate**: `tests/integration/test_conformance.cpp` now asserts `elasticity-stub` passes all 10 v1 checks (**9 in-tree plugins green**). The suite itself didn't change; the second solver fits the same shape as the first.
- **`tests/integration/test_elasticity_stub.cpp`** — full `grid-mesher → elasticity-stub` end-to-end: programmatic unit-cube geometry → 3×3×3 grid mesh (27 nodes) → elasticity-stub against arbitrary load / E / ν → pin the closed-form values at the corner (1,1,1) and the origin (0,0,0) within 1e-12. Negative test: missing-mesh input rejected with a structured `DispatchError`.
- **Build**: `examples/CMakeLists.txt` adds `plugins/elasticity-stub` unconditionally; `plugins/fenicsx-solver` only when `SOUXMAR_WITH_FENICSX=ON`. `tests/integration/CMakeLists.txt` depends on `elasticity_stub`.
- **No frozen-header surface was touched.** Both new plugins build against the existing `souxmar-c/solver.h`. ABI v1.1 stays locked; no ratchet marker needed.

#### Sprint 7 push 3 — out-of-core mesh streaming (mmap-backed buffer v2, ABI v1.2 ratchet)

- **[ABI v1.2 — additive minor ratchet event]** `SOUXMAR_ABI_VERSION_MINOR` bumped **1 → 2**. The first post-freeze ratchet event; lands the `souxmar_buffer_t` v2 backing per ADR-0006. The commit carries the `Ratchet: additive minor surface (ADR-0008)` marker, exercising the CI gate that landed in Sprint 7 push 1.
- **New entry points in `souxmar-c/buffer.h`**:
  - `souxmar_buffer_new_mmap(path, size_bytes, flags)` — opens a file-backed mapping. `flags == 0` is "open existing RW file"; `SOUXMAR_BUFFER_FLAG_READONLY` opens read-only and uses the file's natural size when `size_bytes == 0`; `SOUXMAR_BUFFER_FLAG_CREATE` creates+truncates the file (RW only — `READONLY | CREATE` is rejected with NULL).
  - `souxmar_buffer_is_mmap(buffer)` — explicit kind discriminator for tooling. Returns 1 for both RW and RO mappings, 0 for heap-backed (the v1 path) and NULL inputs.
  - The existing v1 accessor surface (`souxmar_buffer_new` / `_free` / `_data` / `_data_const` / `_size` / `_alignment`) is unchanged; plugins that only use those work on a v1.2 host without recompilation, and they continue to return heap-backed buffers when called.
- **Implementation in `src/core/c_abi_buffer.cpp`**:
  - The `BufferHeader::reserved` field that ADR-0006 explicitly reserved for this exact purpose is now the `kind` discriminator. v1 zero-initialised it (→ `KindHeap`) so the v1 binary layout is **byte-identical** — no consumer of the v1 ABI shifts. Mmap-backed headers add a small set of trailing fields (mmap address, length, fd/HANDLE) used only by `souxmar_buffer_free` to unmap and close.
  - POSIX path: `open` + optional `ftruncate` + `mmap(MAP_SHARED)`. Windows path: `CreateFileA` + `SetEndOfFile` + `CreateFileMappingA` + `MapViewOfFile`. Both paths funnel through the same `BufferHeader` and the same `souxmar_buffer_free` cleanup.
  - `souxmar_buffer_data(rw_mapping)` returns the mmap address; `souxmar_buffer_data(ro_mapping)` returns NULL (callers use `_data_const`). The asymmetry surfaces an obvious bug class — a plugin writing to a read-only mapping — at the API boundary rather than via SIGSEGV.
- **`souxmar_mesh_from_buffers` works unchanged.** The bulk mesh ingest reads buffers through `souxmar_buffer_data_const`, which transparently returns either the heap data slot or the mmap region. **Out-of-core mesh ingest is operational with zero changes to `c_abi_mesh.cpp` or any caller** — that's the whole point of the v1 → v2 forward-compatibility plan.
- **[ADR-0006 v2 section]** Documents the v2 implementation, the ratchet that lifted minor 1 → 2, and the still-deferred subprocess shared-fd path (Sprint 8+ alongside the OpenFOAM subprocess plugin). v1 plugins that call only `souxmar_buffer_new` keep working unchanged.
- **Tests** (`tests/unit/test_c_abi_buffer.cpp`):
  - `IsMmapFalseForHeapBacked` — v1 buffers correctly self-report.
  - `RoundTripRwThenReadOnly` — write 4 KiB pattern through an RW mapping → free → reopen RO → byte-by-byte verify.
  - `NullPathRejected`, `ReadOnlyAndCreateAreMutuallyExclusive`, `MissingFileReadOnlyFails` — error paths.
  - `BulkMeshIngestThroughMmapBuffers` — the integration claim: build a full 5-node / 2-tet mesh whose four bulk buffers (coords/types/conn/offsets) are all mmap-backed; `souxmar_mesh_from_buffers` ingests them transparently.
- **Benchmark** (`benchmarks/bench_mmap_buffer.cpp`): heap-roundtrip vs mmap-create-write vs mmap-reopen-readonly across 1 / 16 / 64 / 256 MiB sizes. The CI baseline carries the expected ratio (≤ 1.2× heap path at 64 MiB+); the perf-nightly workflow tracks it.
- **No load-bearing v1 surface changed.** Every v1 function signature is byte-identical; every v1 struct layout is byte-identical (the `reserved → kind` rename is a comment-level change with identical semantics for zero-init). The ratchet marker the commit carries is the additive-minor variant, which the CI gate accepts as exactly the case it was designed to permit.

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
