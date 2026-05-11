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
version       = "0.3.1"                 # SemVer (major.minor.patch[-pre][+build])
abi           = 1                       # major souxmar ABI it targets
license       = "Apache-2.0"
homepage      = "https://example.com/netgen-mesher"

# Optional metadata (Sprint 6 push 2 — additive). The host treats these
# as advisory and never gates the load on them.
description           = "Netgen-backed tetrahedral mesher with feature recovery."
documentation         = "https://example.com/netgen-mesher/docs"
tags                  = ["mesher", "tetrahedral"]
min_souxmar_abi_minor = 0               # require at least this minor ABI

[plugin.binary]
file          = "libnetgen_mesher.so"   # relative to manifest

[plugin.capabilities]
provides = [
  "mesher.tetra.netgen",                # namespace must be one of: reader, writer,
                                         # mesher, element, solver, postproc.
]

[plugin.threading]
model = "internal-parallel"             # one of: reentrant, single-threaded, internal-parallel

[plugin.dependencies]
souxmar = ">=1.0,<2.0"
```

The host validates the manifest, refuses to load on ABI mismatch, and surfaces the metadata to `souxmar plugin list`. Every rejection carries a stable code — see `ManifestRejection` in `souxmar/plugin/manifest.h` — so `souxmar plugin list` reports the structured class plus a free-form reason:

```
2 plugin(s) rejected:
  - /opt/.../souxmar-plugin.toml: [manifest_parse_failed/invalid_capability_namespace]
      'plugin.capabilities.provides' has 'garbage.foo' — namespace not in the host allow-list ...
  - /opt/.../souxmar-plugin.toml: [binary_not_found]
      declared binary 'libfoo.so' does not exist at ...
```

Tooling parses the bracketed codes; the rest of the line is for humans.

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

#### Reader plugins — Sprint 6 push 4 (ABI minor v1.1)

Reader plugins consume a path on disk + a value-bag of options and produce **either** a Mesh (tessellated formats — STL / OBJ / PLY) **or** a Geometry (CAD formats — STEP / IGES / BREP). The vtable has two output slots; the plugin fills exactly one, and the host's dispatcher routes the result to the matching `StageOutput` kind. See `souxmar-c/reader.h` for the full contract and `examples/plugins/stl-reader/` for the reference implementation.

The `reader.*` surface landed as the **first additive minor ratchet event** during the v1 freeze-candidate soak — `SOUXMAR_ABI_VERSION_MINOR` bumped 0 → 1. A v1.0 plugin keeps loading on a v1.1 host (every new symbol is opt-in); a v1.1 plugin attempting to register a reader against a v1.0 host fails at symbol resolution time, which conformance check C004 catches.

## Current freeze status: **frozen FINAL at v1.1** (since Sprint 7 push 1, 2026-05-11)

The C ABI surface in `include/souxmar-c/` is locked for the entire 1.x release series. The binding declaration lives in [ADR-0008](adr/0008-abi-v1-final-freeze.md); the commit landing that ADR is tagged `abi-v1-frozen`.

Post-freeze rules — the ratchet, unchanged from the soak period:

- **Additive minor surfaces** (new headers, new function declarations, new constants under a fresh prefix, new optional fields appended to forward-compatible structs) are allowed and bump `SOUXMAR_ABI_VERSION_MINOR`. PRs that touch a frozen header in an additive way must carry the `Ratchet: additive minor surface (ADR-0008)` marker in the commit message — `scripts/check-frozen-headers.sh` enforces it in CI.
- **Bug fixes** to comments, docs, or non-load-bearing declaration details are allowed under the `Ratchet: bug-fix (ADR-0008) — <reason>` marker. The Sprint 5 push 4 `souxmar_value_t` typedef restoration is the precedent.
- **Anything else** requires a Tier-3 ADR per [`docs/GOVERNANCE.md`](GOVERNANCE.md) and is in practice the trigger for a v2 ABI conversation.

The `SOUXMAR_ABI_FREEZE_CANDIDATE` macro is **gone**. Plugin authors that branched on it during soak can now drop the conditional — the freeze is permanent.

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

Sprint 5 ships **conformance suite v1** as a binary plugin authors run against any plugin tree:

```
$ souxmar-conformance ./build/dev/examples/plugins

plugin: dev.souxmar.examples.hello-mesher
  manifest: .../examples/plugins/hello-mesher/souxmar-plugin.toml
  check    outcome  detail
  -------- -------  ------------------------------------------------------------
  C001     PASS
  C002     PASS
  C003     PASS
  C004     PASS
  C005     PASS
  C006     PASS
  C007     PASS
  C008     PASS
  C009     PASS
  C010     PASS
  10 passed, 0 failed, 0 skipped

3 plugin(s) scanned, 0 failed
```

The v1 catalogue (see [ADR-0004](adr/0004-plugin-conformance-suite.md) for the contract each check enforces):

| ID    | What it checks                                                        |
| ----- | --------------------------------------------------------------------- |
| C001  | Manifest's `abi` field matches `SOUXMAR_ABI_VERSION_MAJOR`            |
| C002  | Manifest's `binary` file resolves to an existing path on disk         |
| C003  | Plugin binary loads (dlopen / LoadLibraryExW succeeds)                |
| C004  | `souxmar_plugin_register_v1` symbol is exported                       |
| C005  | Registration returns success                                          |
| C006  | Every capability announced in the manifest is registered              |
| C007  | No unannounced capabilities are registered                            |
| C008  | Each registered capability's threading model matches the manifest    |
| C009  | Plugin unload removes every capability owned by this plugin           |
| C010  | Three load/unload cycles leave the registry at the same baseline      |

Flags:

- `--plugin-id <id>` — run only against the plugin with this manifest id.
- `--summary-only` — one line per plugin instead of the full table.
- `--quiet` — suppress per-plugin output; emit only the final tally.

Exit codes: `0` everything passed · `1` at least one check failed · `2` usage error · `3` no plugins discovered.

Conformance is **required** for plugins published to the plugin index. A plugin that fails any v1 check is rejected at publish time. The same suite is the gate for the ABI v1 freeze candidate per [ADR-0004](adr/0004-plugin-conformance-suite.md).

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
