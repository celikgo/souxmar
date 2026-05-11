# Plugin authoring tutorial

This walkthrough takes a third-party developer from an empty
directory to a `souxmar-conformance`-passing plugin in one sitting.
The contract is documented in [`PLUGIN_SDK.md`](../PLUGIN_SDK.md);
this tutorial is the imperative version of the same information.

## What you're building

A souxmar plugin is a shared library (`.so` / `.dylib` / `.dll`) that
the host's `souxmar-plugin` loader opens, calls one registration
function on, and uses to dispatch one or more **capabilities** —
named operations like `mesher.tetra.netgen` or `solver.heat.linear`.

The current ABI v1 supports four capability namespaces:

| Namespace    | Inputs                            | Output           | Reference plugin                    |
| ------------ | --------------------------------- | ---------------- | ----------------------------------- |
| `mesher.*`   | optional geometry + options       | mesh             | `examples/plugins/hello-mesher/`    |
| `solver.*`   | mesh + value bag + options        | field            | `examples/plugins/heat-solver/`     |
| `writer.*`   | mesh + optional field + value bag | side effect      | `examples/plugins/hello-writer/` / `vtu-writer/` |
| `postproc.*` | mesh + field + options            | field            | `examples/plugins/scalar-magnitude/` |

A single plugin may register any number of capabilities across any
mix of namespaces.

## Prerequisites

You need:

- A C++20 compiler (GCC 11+, Clang 14+, MSVC 19.30+).
- CMake 3.25+.
- souxmar's public headers — either installed (`find_package(souxmar)`)
  or vendored as a submodule. The walkthrough below assumes vendored.

You do **not** need to link any souxmar implementation library —
plugins talk to the host through the C ABI only. The compile + link
surface is just `souxmar-c/*.h` headers and a single registration
function call.

## 1. Project layout

```text
my-plugin/
├── CMakeLists.txt
├── souxmar-plugin.toml
└── src/
    └── my_mesher.cpp
```

That's the entire surface area for a v1 plugin.

## 2. The manifest

`souxmar-plugin.toml` declares what's in the box. The host's
discovery layer reads it before it touches the binary.

```toml
[plugin]
id            = "com.example.netgen"      # globally unique reverse-DNS
name          = "Netgen Tetrahedral Mesher"
version       = "0.1.0"                   # SemVer
abi           = 1                         # SOUXMAR_ABI_VERSION_MAJOR
license       = "Apache-2.0"
homepage      = "https://example.com/netgen-plugin"

[plugin.binary]
file          = "libnetgen.so"            # path relative to the manifest

[plugin.capabilities]
provides      = ["mesher.tetra.netgen"]   # must match what you register

[plugin.threading]
model         = "single-threaded"         # reentrant | single-threaded | internal-parallel

[plugin.dependencies]
souxmar       = ">=1.0,<2.0"              # which host versions you support
```

Three details worth flagging:

- **`provides` is authoritative.** Conformance check C006 will fail
  if you register a capability not listed here, and C007 will fail
  if you list one you don't register.
- **`threading.model`** governs how the parallel runner calls you.
  `reentrant` means "go ahead, fan me out across threads."
  `single-threaded` and `internal-parallel` both mean "serialize my
  calls across the runner's workers" — the difference is internal
  (whether you use threads inside the call).
- **`dependencies.souxmar`** is a version-range string. The loader
  refuses to load you if the host doesn't match.

## 3. The single export

Every plugin exports exactly one symbol:

```c
SOUXMAR_PLUGIN_EXPORT
int souxmar_plugin_register_v1(souxmar_registry_t*        registry,
                               const souxmar_host_info_t* host);
```

Return `0` on success, non-zero on fatal init failure. The host calls
this once, immediately after `dlopen`, with the registry the plugin
should register its capabilities into.

The canonical preamble (from `hello-mesher`):

```cpp
extern "C" SOUXMAR_PLUGIN_EXPORT
int souxmar_plugin_register_v1(souxmar_registry_t*        registry,
                               const souxmar_host_info_t* host) {
  if (!host || host->abi_version_major < SOUXMAR_ABI_VERSION_MAJOR) {
    return -1;  // host is older than what we target
  }
  const souxmar_status_t s = souxmar_registry_add_mesher(
      registry, "mesher.tetra.netgen", &kVtable, /*user_data=*/nullptr);
  return s.code == SOUXMAR_OK ? 0 : 1;
}
```

The `kVtable` is a `souxmar_mesher_vtable_t` (or `solver` / `writer`
/ `postproc` per your namespace) with the ABI version, the
implementation function pointer, and an optional destructor.

## 4. Implementing the vtable

### Mesher

```cpp
souxmar_status_t my_mesh(const souxmar_geometry_t*       geometry,
                        const souxmar_mesher_options_t* options,
                        souxmar_mesh_t**                out_mesh,
                        void*                           user_data) {
  if (out_mesh == nullptr) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "out_mesh is NULL");
  }
  souxmar_mesh_t* mesh = souxmar_mesh_new();
  if (!mesh) {
    return souxmar_status_error(SOUXMAR_E_OUT_OF_MEMORY, "mesh allocation failed");
  }

  // ... call your meshing kernel here ...
  // Per-element path:
  //   uint64_t n0 = souxmar_mesh_add_node(mesh, p0);
  //   souxmar_mesh_add_cell(mesh, SOUXMAR_ET_TET4, nodes, 4, tag, &cell_idx);
  //
  // Or, for large meshes (10k+ cells), the Sprint 5 push 4 bulk path:
  //   souxmar_mesh_buffers_t buffers{...};
  //   mesh = souxmar_mesh_from_buffers(&buffers, &status);
  //   if (!mesh) return status;

  *out_mesh = mesh;
  return souxmar_status_ok();
}

constexpr souxmar_mesher_vtable_t kVtable = {
    SOUXMAR_ABI_VERSION_MAJOR,
    &my_mesh,
    /*destroy_fn=*/nullptr,
};
```

Memory model: the host owns the `out_mesh` you hand back. Free with
`souxmar_mesh_free` (the host does this automatically as the pipeline
runner unwinds).

### Solver

The solver takes mesh + value bag + options + returns a field:

```cpp
souxmar_status_t my_solve(const souxmar_mesh_t*           mesh,
                          const souxmar_value_t*          inputs,
                          const souxmar_solver_options_t* options,
                          souxmar_field_t**               out_field,
                          void*                           user_data) {
  // Read non-mesh inputs from the value bag:
  const souxmar_value_t* v = souxmar_value_map_get(inputs, "tau");
  const double tau = (v && souxmar_value_kind(v) == SOUXMAR_VK_NUMBER)
                     ? souxmar_value_as_number(v) : 1.0;

  // Allocate output field: nodal scalar, count=num_nodes, n time steps.
  *out_field = souxmar_field_new("temperature",
                                 SOUXMAR_FL_NODAL, SOUXMAR_FK_SCALAR,
                                 souxmar_mesh_num_nodes(mesh),
                                 /*num_time_steps=*/4);
  if (!*out_field) {
    return souxmar_status_error(SOUXMAR_E_OUT_OF_MEMORY, "field alloc failed");
  }
  double* data = souxmar_field_data(*out_field);
  // ... write count * components * num_time_steps doubles ...
  return souxmar_status_ok();
}
```

See `examples/plugins/heat-solver/heat_solver.cpp` for a complete
field-time-series solver that drives the C ABI end-to-end.

### Postproc

Postproc is like solver but with a required input field:

```cpp
souxmar_status_t my_postproc(const souxmar_mesh_t*                mesh,
                             const souxmar_field_t*               input_field,
                             const souxmar_value_t*               inputs,
                             const souxmar_postproc_options_t*    options,
                             souxmar_field_t**                    out_field,
                             void*                                user_data) {
  const auto* in   = souxmar_field_data_const(input_field);
  const auto  size = souxmar_field_data_size(input_field);

  *out_field = souxmar_field_new("magnitude",
                                 souxmar_field_location(input_field),
                                 SOUXMAR_FK_SCALAR,
                                 souxmar_field_count(input_field),
                                 souxmar_field_num_time_steps(input_field));
  // ... compute and write ...
  return souxmar_status_ok();
}
```

See `examples/plugins/scalar-magnitude/scalar_magnitude.cpp` for a
complete reentrant postproc plugin.

### Writer

Writers don't allocate an output handle — they have a side effect
(writing to disk, network, etc.):

```cpp
souxmar_status_t my_write(const souxmar_mesh_t*  mesh,
                          const souxmar_field_t* field,    // may be NULL
                          const souxmar_value_t* inputs,
                          void*                  user_data) {
  // The writer's `path` input is the standard place to find the target.
  const souxmar_value_t* p = souxmar_value_map_get(inputs, "path");
  if (!p || souxmar_value_kind(p) != SOUXMAR_VK_STRING) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT,
                                "writer requires `path: <string>`");
  }
  // ... write to souxmar_value_as_string(p) ...
  return souxmar_status_ok();
}
```

## 5. Building with CMake

The `souxmar_add_plugin` macro in `cmake/SouxmarPlugin.cmake` bakes
in the right defaults: hidden visibility, position-independent code,
the SOUXMAR_BUILD_PLUGIN compile flag, copy-manifest-beside-binary.
Both in-tree examples and out-of-tree third-party plugins should
use it.

```cmake
cmake_minimum_required(VERSION 3.25)
project(my_plugin LANGUAGES CXX)

find_package(souxmar REQUIRED)

souxmar_add_plugin(my_mesher
  SOURCES      src/my_mesher.cpp
  MANIFEST     ${CMAKE_CURRENT_SOURCE_DIR}/souxmar-plugin.toml
  CAPABILITIES "mesher.tetra.netgen"
)
```

Build:

```sh
cmake -B build -G Ninja
cmake --build build
```

## 6. Verify with `souxmar-conformance`

Before publishing, run the conformance suite against your build:

```sh
souxmar-conformance ./build
```

You should see ten `PASS` lines per discovered plugin. If any check
fails, the report includes a specific reason — see
[ADR-0004](../adr/0004-plugin-conformance-suite.md) for the full
contract each check enforces.

The most common first-attempt failures:

- **C001** — manifest `abi` doesn't match `SOUXMAR_ABI_VERSION_MAJOR`.
  Set `abi = 1` in the manifest.
- **C002** — manifest `binary.file` doesn't point at your built `.so`
  (typo, or platform suffix mismatch). The convention is `lib<name>.so`
  on Linux, `lib<name>.dylib` on macOS, `<name>.dll` on Windows.
- **C006 / C007** — your `provides` array doesn't match what you
  register. Compare the manifest line by line with your registration
  calls.

## 7. Distribution

Once `souxmar-conformance` is green, package the plugin as:

```text
my-plugin/
├── souxmar-plugin.toml
└── libnetgen.so          (or libnetgen.dylib / netgen.dll)
```

Drop that directory anywhere on the user's `$SOUXMAR_PLUGIN_PATH`
(the host walks immediate subdirectories of each search root), or
install it under the platform's plugin prefix:

- Linux: `~/.local/share/souxmar/plugins/<plugin-id>/`
- macOS: `~/Library/Application Support/souxmar/plugins/<plugin-id>/`
- Windows: `%APPDATA%\souxmar\plugins\<plugin-id>\`

Users can verify discovery via:

```sh
souxmar plugin list
```

## Going further

- **Bulk mesh construction** for >10k-cell meshes —
  [ADR-0006](../adr/0006-memory-mapped-buffer-protocol.md).
- **The agent surface** — your plugin's capability becomes invocable
  by the AI agent automatically; see [AI_INTEGRATION.md](../AI_INTEGRATION.md).
- **Python plugins** (prototyping only — the GIL rules them out for
  high-throughput meshing): see `bindings/python/README.md`'s roadmap
  section.

## Reference implementations

| Plugin                                       | Namespace    | Key technique                                  |
| -------------------------------------------- | ------------ | ---------------------------------------------- |
| `examples/plugins/hello-mesher/`             | `mesher.*`   | The minimum-viable plugin (single tet)         |
| `examples/plugins/hello-writer/`             | `writer.*`   | Reading mesh through the C ABI                 |
| `examples/plugins/vtu-writer/`               | `writer.*`   | ParaView-readable XML emission                 |
| `examples/plugins/heat-solver/`              | `solver.*`   | Field time-series across the C ABI             |
| `examples/plugins/scalar-magnitude/`         | `postproc.*` | First reentrant postproc plugin                |

When in doubt, copy the closest reference plugin and modify. Every
one of them passes the v1 conformance suite — they're the calibration
weights for your own implementation.
