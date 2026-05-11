---
name: developing-souxmar-plugin
description: Use when creating or modifying a souxmar plugin (mesher, solver, element, reader, writer, postproc). Walks through manifest, ABI, conformance, CMake, and testing. Triggers on "new plugin", "souxmar plugin", "register capability", "souxmar_plugin_register_v1".
---

# Developing a souxmar plugin

A souxmar plugin is a dynamic library (`.so` / `.dylib` / `.dll`) that registers one or more capabilities through the stable C ABI in `include/souxmar-c/`. Out-of-tree plugins compile against the published SDK and load at runtime.

## When to use this skill

- A user is creating a brand-new plugin from scratch.
- A user is adding a capability to an existing plugin binary.
- A user is converting an existing C++ algorithm into a souxmar plugin.
- A reviewer is auditing a plugin PR.

## When NOT to use this skill

- Modifying `libsouxmar-core` itself (that is library work, not plugin work).
- Modifying the C ABI in `include/souxmar-c/` — that requires an RFC; use the `writing-souxmar-rfc` skill.
- Adding an agent tool — use `adding-agent-tool` instead.

## Workflow

### 1. Pick the capability namespace

| Namespace        | Implements          | Example                                    |
| ---------------- | ------------------- | ------------------------------------------ |
| `reader.*`       | `ISouxmarReader`    | `reader.step`, `reader.iges`               |
| `writer.*`       | `ISouxmarWriter`    | `writer.vtu`, `writer.xdmf`                |
| `mesher.*`       | `ISouxmarMesher`    | `mesher.tetra.gmsh`, `mesher.shell.native` |
| `element.*`      | `ISouxmarElement`   | `element.solid.tet4`                       |
| `solver.*`       | `ISouxmarSolver`    | `solver.elasticity.linear`                 |
| `postproc.*`     | `ISouxmarFieldOp`   | `postproc.von_mises`                       |

A plugin binary may register many capabilities. If unsure, check `docs/PLUGIN_SDK.md`.

### 2. Scaffold the plugin

```
my-plugin/
  CMakeLists.txt
  souxmar-plugin.toml
  src/my_plugin.cpp
  tests/test_my_plugin.cpp
```

### 3. Manifest

`souxmar-plugin.toml` is mandatory. Required fields:

```toml
[plugin]
id            = "com.example.my-mesher"
name          = "Example Tetrahedral Mesher"
version       = "0.1.0"
abi           = 1                              # major souxmar ABI version
license       = "Apache-2.0"

[plugin.binary]
file          = "libmy_mesher.so"              # adjust per OS

[plugin.capabilities]
provides      = ["mesher.tetra.example"]

[plugin.threading]
model         = "internal-parallel"            # reentrant | single-threaded | internal-parallel

[plugin.dependencies]
souxmar       = ">=1.0,<2.0"
```

### 4. Single entry point

Every plugin exports exactly one symbol:

```cpp
#include <souxmar-c/plugin.h>

SOUXMAR_PLUGIN_EXPORT
int souxmar_plugin_register_v1(souxmar_registry_t* registry,
                               const souxmar_host_info_t* host) {
    if (host->abi_version < 1) return -1;
    return souxmar_registry_add_<capability>(registry, "<capability.id>", &kVtable, nullptr);
}
```

### 5. Memory ownership rules

These are hard rules. Violating them will fail conformance.

- The allocator owns the deallocation. Plugin-allocated buffers are freed by a plugin-supplied deallocator the host calls back.
- Never call `free` / `delete` across the ABI on something you did not allocate.
- Borrow vs. own is explicit. Inputs are borrows; outputs are moves.
- No throwing across the ABI. Catch every exception at the boundary; translate to `souxmar_status_t`.

### 6. CMake

```cmake
find_package(souxmar 1 REQUIRED)
souxmar_add_plugin(my_mesher
    SOURCES src/my_plugin.cpp
    MANIFEST souxmar-plugin.toml
    CAPABILITIES "mesher.tetra.example"
)
```

`souxmar_add_plugin` produces the shared library, copies the manifest next to it, and adds an `install` rule that places both in the per-user plugins directory.

### 7. Conformance

Run the conformance suite before opening a PR:

```bash
souxmar-conformance --plugin ./libmy_mesher.so --capability mesher.tetra.example
```

Required passing checks (this skill blocks the PR if any of these fail):
- Manifest present and parseable
- Single registration symbol present
- ABI version compatible
- Reentrancy contract honoured (matches manifest declaration)
- Tag inheritance preserved (mesher capabilities only)
- Empty input returns the correct error code
- Memory clean under ASAN over 100 invocations

### 8. Tests in the plugin's own repo

At minimum:
- One unit test per public function in the plugin.
- One integration test that loads the built `.so` via `souxmar_plugin_load` and exercises one capability end-to-end.
- One ASAN run on the integration test in the plugin's CI.

### 9. Publishing (optional)

To list in the open plugin index, open a PR against `docs/plugin-index.md` in the souxmar repo with the manifest summary and a link to the source. See `docs/GOVERNANCE.md`.

## Common mistakes to flag in code review

- Using `std::string` / `std::vector` across the ABI boundary (use raw pointers + length + deallocator).
- Throwing C++ exceptions out of an `extern "C"` function.
- Allocating in the host and freeing in the plugin (or vice versa).
- Holding global state in the plugin after `souxmar_plugin_register_v1` returns.
- Declaring `threading.model = "reentrant"` while using non-reentrant globals.
- Returning `SOUXMAR_OK` on error paths.
- Manifest `abi` field mismatched with the linked SDK headers.

## Reference

- `docs/PLUGIN_SDK.md` — full ABI specification.
- `docs/ARCHITECTURE.md` — how plugins fit into the broader system.
- `tests/plugin-conformance/` — the conformance suite source.
- `examples/plugins/hello-mesher/` — a minimal reference implementation (when present).
