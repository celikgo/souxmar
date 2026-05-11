# pysouxmar

Python bindings for [souxmar](https://souxmar.dev), the open-source CAE
platform. Wraps the souxmar C++ libraries (core, plugin host, pipeline
runner) via pybind11 so Python users can write CAE workflows without
touching CMake.

## Status

**Pre-alpha** (Sprint 4 push 1). The binding surface is intentionally
narrow — it exposes the pieces needed to parse a pipeline YAML, discover
and load plugins, and run a pipeline end to end. Direct access to mesh /
geometry / field handles from Python lands in Sprint 5 once plugin-side
serializers exist.

## Install

### From a souxmar checkout

```sh
cd bindings/python
pip install .
```

`pip install .` invokes [scikit-build-core](https://scikit-build-core.readthedocs.io/),
which calls CMake to build the souxmar libraries + the `_pysouxmar`
extension and installs them into the active environment.

The build needs:

- **CMake** ≥ 3.25
- **A C++20 compiler** (GCC 11+, Clang 14+, MSVC 19.30+)
- **vcpkg**, with `$VCPKG_ROOT` set, for the few C++ dependencies
  (`fmt`, `tomlplusplus`, `yaml-cpp`, `pybind11`)

If `$VCPKG_ROOT` is unset, point CMake at a system pybind11 + the souxmar
deps yourself via `pip install --config-settings=cmake.define.CMAKE_TOOLCHAIN_FILE=...`.

### From the dev preset (no pip)

```sh
cd <souxmar-repo>
cmake --preset dev-python
cmake --build --preset dev-python
PYTHONPATH=$(pwd)/build/dev-python/bindings/python python -c 'import pysouxmar; print(pysouxmar.version())'
```

The `dev-python` preset enables `SOUXMAR_BUILD_PYTHON=ON` and tells vcpkg
to install the `python` feature alongside the rest.

## Quick start

```python
import pysouxmar as sx

# 1. Discover and load every plugin under ./plugins/
registry = sx.Registry()
loader   = sx.PluginLoader(registry, sx.version())
loaded   = [loader.load(p) for p in sx.discover_plugins(["./plugins"]).loaded]
print("capabilities:", registry.list_capabilities())

# 2. Parse a YAML pipeline
pipeline = sx.parse_pipeline_file("cantilever.yaml")

# 3. Run it
result = sx.run_pipeline(pipeline, sx.RegistryDispatcher(registry), sx.Cache())

# 4. Inspect outputs
for stage in result.stage_results:
    print(f"{stage.stage_id}: {stage.status.name}  {stage.content_hash.hex[:12]}…")
```

See `examples/cantilever.py` for the same flow in 20 lines.

## API surface

| Group                | Exposed from `pysouxmar`                                               |
| -------------------- | ---------------------------------------------------------------------- |
| Versioning           | `version()`, `version_tuple()`, `abi_version()`                        |
| Pipeline parsing     | `parse_pipeline`, `parse_pipeline_file`, `Pipeline`, `Stage`, `StageRef` |
| Plugin discovery     | `discover_plugins`, `default_search_paths`, `DiscoveryOptions`, `DiscoveredPlugin` |
| Plugin host          | `Registry`, `PluginLoader`, `LoadedPlugin`, `Manifest`, `ThreadingModel`, `CapabilityKind` |
| Cache                | `Cache`, `DiskCache`, `ContentHash`                                    |
| Runner               | `RegistryDispatcher`, `RunOptions`, `RunResult`, `StageRunResult`, `RunStatus`, `StageStatus` |

### Pipeline values

Stage inputs come back as native Python objects:

| C++ Value type | Python type                          |
| -------------- | ------------------------------------ |
| `Null`         | `None`                               |
| `Bool`         | `bool`                               |
| `Number`       | `float`                              |
| `String`       | `str`                                |
| `StageRef`     | `pysouxmar.StageRef`                 |
| `List`         | `list`                               |
| `Map`          | `dict`                               |

The `{from: stage_id}` YAML shorthand round-trips as a `StageRef`. A plain
Python dict with a single string-valued `"from"` key is also accepted as a
`StageRef` so you can write pipelines without importing the type.

## Lifetime rules

- **`Registry` outlives every `PluginLoader` and `LoadedPlugin`** built
  against it. pybind11's `keep_alive` enforces this, so a Python-level
  collection of the registry while a `LoadedPlugin` is still live is
  prevented.
- **`LoadedPlugin` is move-only and RAII.** Drop the Python reference and
  the registry forgets the plugin's capabilities + the OS module is
  closed. Hold them in a list bound to the registry's lifetime.
- **`Cache` is process-local.** For cross-process cache hits, set
  `RunOptions.disk_cache_dir`; the binding wires the StageOutput
  serializer/deserializer for you.

## Tests

```sh
cmake --preset dev-python
cmake --build --preset dev-python
cd bindings/python
PYTHONPATH=../../build/dev-python/bindings/python pytest
```

The plugin-loading tests skip cleanly if no built plugins are found, so
the unit-test layer runs fine on a clean install.

## Agent tools (sx.ai)

The agent tool surface from `docs/AI_INTEGRATION.md` is exposed under
`sx.ai`. Sprint 4 push 3 ships the five v1 tools:

```python
import pysouxmar as sx

reg    = sx.ai.default_v1_tools()
ctx    = sx.ai.ToolContext()
ctx.session_state = {
    "geometry": {"num_vertices": 8, "num_edges": 12, "num_faces": 6, "num_solids": 1}
}
policy = sx.ai.ConfirmationPolicy()
# `set_bc` defaults to ConfirmOnce; flip to Auto for headless use.
policy.overrides = {"set_bc": sx.ai.Confirmation.Auto,
                    "solve":  sx.ai.Confirmation.Auto}

out = sx.ai.dispatch_tool(reg, "read_geometry_summary", None, ctx, policy)
print(out.summary)
print(out.data)
```

The v1 catalogue (see `docs/AI_INTEGRATION.md` for the full design):

| Tool                  | Category | Confirmation     | Notes                                                            |
| --------------------- | -------- | ---------------- | ---------------------------------------------------------------- |
| `read_geometry_summary` | Read   | Auto             | Inspects `session_state['geometry']`.                            |
| `mesh`                | Mesh     | Auto             | Dispatches a registered `mesher.*` plugin via `ToolContext`.    |
| `set_bc`              | BC       | ConfirmOnce      | Appends to `session_state['boundary_conditions']`.              |
| `solve`               | Solve    | ConfirmAlways    | Dispatches `solver.*`; requires `mesh` was called first.        |
| `screenshot_viewport` | Read     | ConfirmOnce      | Stub in headless / pip builds; available in the desktop app.    |
| `query_field`         | Read     | Auto             | min/max/mean over `ctx.field_handle`; reports NaN count.        |
| `compute_field`       | Postproc | ConfirmOnce      | Stub awaiting the postproc C ABI (Sprint 5 push 3).              |
| `propose_pipeline`    | Pipeline | Auto             | Round-trips a spec through the YAML parser; returns canonical text. |

### Audit + budget

Every dispatch is recorded to an `AuditLog` (one YAML one-liner per call) when one is wired into the ToolContext:

```python
ctx = sx.ai.ToolContext()
ctx.audit_log = sx.ai.AuditLog(sx.ai.AuditLog.default_path())  # ~/project/.souxmar/chat/audit.log
ctx.budget    = sx.ai.SessionBudget()
ctx.budget.max_total_tokens = 100_000

# Tools that call AI providers update the budget themselves:
ctx.budget.record(prompt_tokens, completion_tokens)
```

`SessionBudget` fires `on_threshold` at 50% / 80% / 100% of `max_total_tokens` (and the per-axis maxima). The callback is not yet bound from Python — for v1, watch `consumed_total` after each `record()`. A first-class Python callback lands in Sprint 6.

Python v1 limitations:

* `ConfirmationPolicy.prompter` is not exposed yet — use the `overrides`
  dict to whitelist tools to `Confirmation.Auto`. A first-class Python
  prompter callback lands in Sprint 5 alongside the desktop tool UI.
* Mesh / Field handles created by `mesh` / `solve` are stashed on the
  `ToolContext` and consumed by the next tool, but are not directly
  inspectable from Python yet (Sprint 5: numpy-backed accessors).

## Parallel runs

The runner is parallel under the hood — set `RunOptions.max_workers > 1`
and independent DAG branches dispatch concurrently:

```python
opts = sx.RunOptions()
opts.max_workers = 4
result = sx.run_pipeline(pipeline, dispatcher, cache, opts)
```

Reentrancy is enforced from each capability's `Manifest.threading`:
`SingleThreaded` and `InternalParallel` plugins serialise across stages
(per plugin id, so two stages of *different* single-threaded plugins still
overlap); `Reentrant` plugins overlap freely. A custom `IDispatcher`
override of `plugin_threading()` lets non-registry-backed dispatchers
declare the same contract.

## Roadmap

- **Sprint 5** — Python-subclassable `IDispatcher` (trampoline + payload
  story now that plugin-side serialization is on deck), the
  `@sx.plugin.mesher` / `@sx.plugin.solver` decorator (write a plugin
  in pure Python), Python prompter callback for `ConfirmationPolicy`,
  direct Mesh / Geometry / Field handle access through the C ABI
  accessors, and numpy-backed coordinate arrays via the buffer protocol.
