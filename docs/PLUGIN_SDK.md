# Plugin SDK

This document specifies the contract between souxmar and out-of-tree plugins. The contract is the **stable C ABI**. Anything written against this ABI compiles against major version `N` of souxmar and continues to load against every minor and patch release of `N`.

If you want to add a mesh algorithm, a finite element, a solver, a reader, or a writer to souxmar without modifying the core, this is the document you need.

## Why a C ABI

C++ has no stable ABI across compilers, standard library versions, or even debug/release builds of the same compiler. Python plugins are easy to write but cannot meet the latency requirements of inner-loop code (mesh refinement, matrix assembly). Rust has no stable ABI either, and its `extern "C"` boundary is itself C.

A C ABI is the lingua franca: a souxmar plugin can be implemented in C, C++, Rust, Fortran, or any language with C FFI, distributed as a single `.so` / `.dylib` / `.dll`, and loaded by any souxmar binary of the right major version regardless of how either was compiled.

## Plugin types

Each plugin advertises one or more **capabilities**. The host's registry is keyed by capability namespace.

| Namespace        | Examples                                            | Implements                                |
| ---------------- | --------------------------------------------------- | ----------------------------------------- |
| `reader.*`       | `reader.step`, `reader.iges`, `reader.stl`          | `ISouxmarReader`                          |
| `writer.*`       | `writer.vtu`, `writer.xdmf`, `writer.gltf`          | `ISouxmarWriter`                          |
| `mesher.*`       | `mesher.tetra.native`, `mesher.tetra.gmsh`          | `ISouxmarMesher`                          |
| `element.*`      | `element.solid.tet4`, `element.shell.mitc4`         | `ISouxmarElement`                         |
| `solver.*`       | `solver.elasticity.linear`, `solver.heat.transient` | `ISouxmarSolver`                          |
| `postproc.*`     | `postproc.von_mises`, `postproc.principal_stress`   | `ISouxmarFieldOp`                         |

A single plugin binary may register many capabilities — for example, an "elasticity pack" plugin might register `solver.elasticity.linear`, `element.solid.tet4`, and `postproc.von_mises` together.

## The single entry point

Every plugin exports exactly one symbol:

```c
SOUXMAR_PLUGIN_EXPORT
int souxmar_plugin_register_v1(souxmar_registry_t* registry,
                               const souxmar_host_info_t* host);
```

- Return `0` on success, non-zero on fatal init failure.
- Inspect `host->abi_version` and `host->capabilities` to decide what to register. The plugin may downgrade gracefully against older hosts.
- Call `souxmar_registry_add_capability(registry, ...)` once per capability.

The host calls this function exactly once per plugin, immediately after `dlopen`. After it returns, the plugin should hold no global state beyond what it registered.

## Plugin manifest

Alongside the binary, ship a `souxmar-plugin.toml`:

```toml
[plugin]
id            = "com.example.netgen-mesher"
name          = "Netgen-backed Tetra Mesher"
version       = "0.3.1"
abi           = 1                       # major souxmar ABI it targets
license       = "Apache-2.0"
homepage      = "https://example.com/netgen-mesher"

[plugin.binary]
file          = "libnetgen_mesher.so"   # relative to manifest

[plugin.capabilities]
provides = [
  "mesher.tetra.netgen",
]

[plugin.threading]
model = "internal-parallel"             # one of: reentrant, single-threaded, internal-parallel

[plugin.dependencies]
souxmar = ">=1.0,<2.0"
```

The host validates the manifest, refuses to load on ABI mismatch, and surfaces the metadata to `souxmar plugin list`.

## Memory ownership rules

These rules are absolute. They are checked by the conformance suite.

1. **The allocator owns the deallocation.** If the host passes a buffer to the plugin, the host frees it. If the plugin returns a buffer, the host calls back into the plugin to free it via the deallocator the plugin supplied alongside the pointer.
2. **No `free()` across the ABI.** Plugins must not call `free()` (or `delete`, or `operator delete[]`) on any pointer they did not allocate.
3. **Borrow vs. own is explicit.** Every input parameter is documented as either a borrow (lifetime tied to the call) or a move (ownership transfers). Outputs are always moves; the host receives ownership.
4. **No throwing across the ABI.** C++ plugins must catch every exception at the boundary and translate to a `souxmar_status_t`.

The SDK ships a `souxmar::ScopedHandle` RAII helper that automates this on the C++ side.

## Error handling

```c
typedef struct {
  int          code;        // 0 = OK, see souxmar_errors.h
  const char*  message;     // UTF-8, owned by plugin, valid until next call
  const char*  detail;      // optional, may be NULL
} souxmar_status_t;
```

Every plugin function returns a `souxmar_status_t` by value. The host wraps each call in a signal/SEH frame; a plugin segfault is caught, logged, and reported as `SOUXMAR_E_PLUGIN_FAULT` rather than killing the host process. This is the only crash isolation guarantee the host provides — it does not, and cannot, make a memory-corrupting plugin safe.

## Versioning

souxmar follows semantic versioning **for the ABI**, decoupled from the release version of the project itself.

- `ABI v1` is frozen at souxmar 1.0 and remains source- and binary-compatible across all 1.x releases.
- New capabilities added in 1.x are additive: a 1.0 plugin still works on 1.5; a 1.5 plugin running on 1.0 may have some capabilities silently disabled (it inspects `host->capabilities`).
- A breaking ABI change requires major bump (souxmar 2.0). Plugins targeting `abi = 1` continue to load on souxmar 2.x via a built-in compatibility shim, marked deprecated, removed in 3.0. We commit to one full major of overlap.

## Plugin discovery

At startup the host walks, in order:

1. `$SOUXMAR_PLUGIN_PATH` (colon-separated, like `$PATH`).
2. `<install_prefix>/lib/souxmar/plugins/`.
3. Per-user: `~/.local/share/souxmar/plugins/` (Linux), `~/Library/Application Support/souxmar/plugins/` (macOS), `%APPDATA%\souxmar\plugins\` (Windows).
4. Current working directory's `./plugins/` (only if the binary is run from a project root).

Each subdirectory containing a `souxmar-plugin.toml` is a plugin. Manifests are read first; binaries with mismatched ABI are skipped without ever being loaded.

`souxmar plugin list` shows everything discovered, loaded or not, with the reason for any rejections.

## A minimal mesher plugin

Skeleton for an out-of-tree tetrahedral mesher in C++:

```cpp
// my_mesher.cpp
#include <souxmar-c/plugin.h>
#include <souxmar-c/mesher.h>

namespace {

souxmar_status_t my_mesh(const souxmar_geometry_t* geom,
                         const souxmar_mesher_options_t* opts,
                         souxmar_mesh_t** out_mesh,
                         void* /*user_data*/)
{
    try {
        // ... your meshing algorithm ...
        // Allocate result via souxmar_mesh_new(); fill nodes, cells, tags.
        // *out_mesh = result;
        return SOUXMAR_OK;
    } catch (const std::exception& e) {
        return souxmar_status_make(SOUXMAR_E_INTERNAL, e.what());
    }
}

const souxmar_mesher_vtable_t kVtable = {
    .abi_version = 1,
    .mesh_fn     = &my_mesh,
    .destroy_fn  = nullptr,
};

}  // namespace

SOUXMAR_PLUGIN_EXPORT
int souxmar_plugin_register_v1(souxmar_registry_t* registry,
                               const souxmar_host_info_t* host)
{
    if (host->abi_version < 1) return -1;
    return souxmar_registry_add_mesher(
        registry, "mesher.tetra.example", &kVtable, /*user_data=*/nullptr);
}
```

CMake build with the SDK:

```cmake
find_package(souxmar 1 REQUIRED)
souxmar_add_plugin(my_mesher
    SOURCES my_mesher.cpp
    MANIFEST souxmar-plugin.toml
    CAPABILITIES "mesher.tetra.example"
)
```

The `souxmar_add_plugin` macro produces a shared library, copies the manifest next to it, and adds an `install` rule that places both in the per-user plugins directory. That is the entire build.

## Conformance suite

`tests/plugin-conformance/` ships an executable, `souxmar-conformance`, that any plugin author can run against their plugin:

```
$ souxmar-conformance --plugin ./libmy_mesher.so --capability mesher.tetra.example
[ OK ] Manifest present and parseable
[ OK ] Single registration symbol present
[ OK ] ABI version compatible with souxmar 1.x
[ OK ] Reentrancy contract honoured (10 parallel meshes, 0 races detected by TSAN)
[ OK ] Tag inheritance: 6 input geometric faces -> 6 distinct tag IDs on output
[FAIL] Empty geometry returns SOUXMAR_E_EMPTY_INPUT (got SOUXMAR_OK)
[ OK ] Memory: 0 leaks under ASAN over 100 invocations
```

A plugin that passes the suite for its capability gets a "conformant" badge in the plugin index. Conformance is recommended, not required — but unconformant plugins should expect bug reports.

## Python plugins (escape hatch)

For prototyping and teaching, Python plugins are supported via a built-in C shim that wraps a Python callable as a `souxmar_mesher_vtable_t` (or solver, etc.). They have a real performance ceiling — the GIL alone rules them out for high-throughput meshing — but they are perfect for teaching, exploration, and orchestration-level capabilities.

```python
import pysouxmar as sx

@sx.plugin.mesher("mesher.tetra.python_demo")
def my_python_mesher(geometry, options):
    # ... return an sx.Mesh ...
    return mesh
```

These register on Python interpreter startup and are otherwise indistinguishable from native plugins to the user. They are not eligible for the conformance badge.
