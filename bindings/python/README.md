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

- **Sprint 4 push 3** — Python-subclassable `IDispatcher` (the Python
  trampoline) and the `@sx.plugin.mesher` / `@sx.plugin.solver` decorator
  (write a plugin in pure Python). Plus the agent-tool dispatcher v1.
- **Sprint 5** — direct Mesh / Geometry / Field handle access through
  the C ABI accessors; numpy-backed coordinate arrays via the buffer
  protocol.
