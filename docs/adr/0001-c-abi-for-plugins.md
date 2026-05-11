# ADR-0001: C ABI for the plugin extension surface

- **Status:** Accepted
- **Date:** 2026-05-11
- **Author:** Founders
- **Deciders:** All maintainers
- **Tier:** 3
- **Affects:** ABI, data model, governance

## Context

souxmar's defining commitment is that researchers and engineers can ship out-of-tree plugins (meshers, solvers, elements, readers, writers) and have an unmodified souxmar install load them at runtime. The extension surface across the project's lifetime needs to be:

- **Stable** across compilers, standard library versions, and OS toolchains.
- **Cross-language** — plugins implemented in C, C++, Rust, Fortran, and any language with a C FFI.
- **Cross-binary-version** — a plugin built against souxmar 1.0 must load against souxmar 1.5 unchanged.
- **Inspectable** — plugins must be auditable artefacts; no opaque package manifest games.

The project must commit to one extension model now, in Phase 0, before any plugin code is written. Reverting later costs every plugin author and every downstream packager.

## Decision

souxmar uses a **stable C ABI** as the sole extension surface. Plugins are dynamic libraries (`.so` / `.dylib` / `.dll`) that export a single symbol (`souxmar_plugin_register_v1`) and interact with the host through `extern "C"` interfaces defined in `include/souxmar-c/`. The ABI is versioned by major-version integer; ABI v1 freezes at souxmar 1.0 and remains binary-compatible across the entire 1.x series.

The host is C++; the ABI is C; plugin internals can be anything that emits `extern "C"`.

## Alternatives considered

### A C++ ABI

Define interfaces as C++ abstract classes (`ISouxmarMesher`, etc.) and let plugins inherit. **Rejected.** C++ has no stable ABI across compilers (libstdc++ vs libc++), across major standard-library versions, or even across debug/release builds of the same compiler. A plugin compiled with GCC 13 + libstdc++ 13 cannot reliably load into a host compiled with Clang 17 + libc++ 17. This is the single biggest reason FreeCAD plugin authors cannot share binaries.

### Python-only plugins

All plugins are Python objects registered via entry points. **Rejected as the primary surface, kept as an escape hatch.** Python plugins are easy to write but cannot meet the latency requirements of inner-loop algorithms (matrix assembly, mesh refinement). The GIL alone disqualifies Python from the host's hot path. We expose a Python decorator shim (`@sx.plugin.mesher`) for prototyping and teaching, but the canonical surface is C.

### WebAssembly modules

Plugins as WASM, loaded via wasmtime. **Rejected.** WASM is a strong sandboxing story but performance overhead vs native is still 1.5–3× for FEM workloads, and the toolchain story for FORTRAN-heavy scientific deps is immature. Worth revisiting post-1.0; not for v1.

### A bespoke text-format protocol (a la LSP)

Plugins are subprocesses speaking JSON over stdio. **Rejected.** Protocol overhead per call is order-of-magnitude wrong for the call rates we need (millions of element-assembly calls per second). Subprocess isolation is genuinely valuable but pays for itself only in adversarial scenarios; for an out-of-tree plugin the user *chose* to install, in-process is the right default.

## Consequences

### Positive

- Plugin binaries are portable across compiler/OS/runtime combinations of the same major.
- Hot-path performance matches native (no IPC, no GIL, no marshalling).
- The community contract is explicit and documented in headers; plugin authors do not need to read host source.
- The ABI versioning story is simple and SemVer-compatible.

### Negative

- The C ABI is more verbose to consume than C++ inheritance. Mitigated by shipping a C++ wrapper SDK (`souxmar::Mesher` / `souxmar::Solver` RAII helpers) that maps onto the C ABI underneath.
- Memory ownership rules are stricter (deallocator alongside every cross-ABI buffer). Mitigated by `souxmar::ScopedHandle` and aggressive testing in the conformance suite.
- We give up some C++ feature richness at the boundary (no exceptions, no templates, no STL types crossing the ABI).
- Adding a new capability namespace (`mesher.*`, `solver.*`, …) is an ABI change requiring an RFC.

### Risks

- **Plugins exploit memory model bugs and crash the host.** Mitigation: signal/SEH frame around every plugin call (in-process crash isolation), conformance suite under ASAN, encouragement (not mandate) to pass conformance.
- **Plugin authors find the C ABI inconvenient and the ecosystem languishes.** Mitigation: invest heavily in the C++ wrapper SDK and the Python escape hatch; ship a `souxmar_add_plugin` CMake macro that hides 90 % of the boilerplate.
- **A discovered ABI bug post-freeze cannot be fixed without breaking compatibility.** Mitigation: 2-sprint freeze candidate period before ABI v1 final lock; one full major of overlap (v2 still loads v1 plugins via shim).

## Pre-mortem

*One year from today.* The ABI v1 freeze proved premature. We discovered after launch that the manifest schema was missing a `cancellable` flag for long-running plugins, and we have no way to add it without breaking every existing plugin's manifest parser. We end up shipping ABI v1.5 with a side-channel registration call, which is ugly and confuses authors. Leading indicators to watch: every conformance-suite gap finding in the first 6 months should be evaluated against "could this become an ABI churn item?"

## References

- `docs/PLUGIN_SDK.md` — full ABI specification.
- `docs/GOVERNANCE.md` — RFC process for ABI changes (Tier 3).
- `include/souxmar-c/` — implementation, once written.
- Comparable-project precedent: VST plugins (audio), LADSPA, GStreamer, Postgres extension API.

## History

- 2026-05-11: Proposed and accepted concurrently with the Phase-0 design freeze. Founders + initial maintainers in agreement; no dissenting alternative carried sufficient weight to defer.
