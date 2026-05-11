# hello-mesher

The canonical souxmar reference plugin. The smallest possible thing that exercises the full plugin SDK contract:

- One exported symbol (`souxmar_plugin_register_v1`).
- One mesher vtable.
- One registration call against the host's registry.

`mesh_fn` is a placeholder that returns `SOUXMAR_E_NOT_IMPLEMENTED`. Producing a real mesh from this plugin requires the host-side Mesh accessor C ABI which lands in Sprint 3 (see [`docs/SPRINT_PLAN.md`](../../../docs/SPRINT_PLAN.md)). The purpose of *this* plugin is to exercise:

- The `souxmar_add_plugin` CMake macro
- The plugin loader's binary open + symbol resolve + register call sequence
- The capability registry's `add_mesher` path
- The crash-isolation guard around the registration call

## Building

When the main souxmar build has `SOUXMAR_BUILD_EXAMPLES=ON`:

```bash
cmake -DSOUXMAR_BUILD_EXAMPLES=ON --preset dev
cmake --build --preset dev --target hello_mesher
```

The build places `libhello_mesher.{so,dylib,dll}` and `souxmar-plugin.toml` together in the build directory; the integration test under `tests/integration/` points `SOUXMAR_PLUGIN_PATH` at that directory and verifies discovery + load + registration.

## Out-of-tree variant

The same source compiles unchanged in a third-party repo against an installed souxmar:

```cmake
find_package(souxmar 1 REQUIRED)
souxmar_add_plugin(hello_mesher
    SOURCES   hello_mesher.cpp
    MANIFEST  souxmar-plugin.toml
    CAPABILITIES "mesher.tetra.hello"
)
```

## What this is NOT

- Not a quality-passing mesher. It does not produce meshes.
- Not the conformance baseline. Conformant plugins must pass `souxmar-conformance` (Sprint 5); this plugin is registration-only.
- Not a tutorial. The tutorial lives at `docs/PLUGIN_SDK.md` and the `developing-souxmar-plugin` skill.
