# ADR-0018: libsouxmar-c-bridge — the Rust ⇄ C++ FFI surface

- **Status:** Accepted
- **Date:** 2026-05-12 (Sprint 13 push 3)
- **Author:** souxmar desktop + platform teams
- **Deciders:** desktop, platform, AI
- **Tier:** 2 (the C ABI declared in
  `include/souxmar-c-bridge/pipeline.h` is on a stable-contract
  path; the `souxmar_bridge_abi_version()` byte cross-checks at
  every load — adding a function is non-breaking; changing a
  signature is a Tier-2 break)
- **Affects:** `src/c-bridge/` (new CMake target);
  `include/souxmar-c-bridge/` (new public header set); the Rust
  `souxmar-bridge` crate's `ffi` module + `build.rs` + the
  `real-ffi` cargo feature; the `pipeline_introspection` field in
  `BridgeFeatureSet::default()`.

## Context

ADR-0016 (Sprint 12 push 2) ratified the `BridgeFeatureSet`
contract — the typed struct that names which workbench surfaces
are wired vs. scaffolding. Every flag in that struct started at
`false` except `keychain_write` (which talks to the OS keychain
directly, no C++ FFI required).

Sprint 13 push 3 lands the **first** real flip — the
`pipeline_introspection` flag goes from "always false" to
"true when the bridge was built against libsouxmar-c-bridge."
The path the actual call takes:

1. React's Inspector panel calls `invokeCommand("pipeline_summary", …)`.
2. Tauri routes to `commands::pipeline_summary` in Rust.
3. The Rust handler calls `Bridge::pipeline_summary` in the
   `souxmar-bridge` crate.
4. The Bridge calls `ffi::parse_pipeline_stages(yaml)`.
5. The FFI module calls `souxmar_bridge_pipeline_parse(yaml, &mut err)`
   over the C ABI.
6. The C bridge library wraps `souxmar::pipeline::parse_pipeline`
   from libsouxmar-pipeline + adapts to C handles.
7. The result threads back up as a typed `PipelineSummary` struct.

The decision points worth recording in an ADR — beyond the
implementation choices that are obvious in code:

1. **Why a separate library, not the plugin C ABI?**
2. **Why static, not dynamic linking?**
3. **Why hand-written FFI, not bindgen / cbindgen-generated?**
4. **Why a Cargo feature flag, not always linking?**
5. **Why an explicit ABI version byte at every call?**

## Decision

### 1. Separate `libsouxmar-c-bridge` from the plugin C ABI

The plugin C ABI in `include/souxmar-c/` is for plugins authored
*against* souxmar — its surface is centred on what a plugin
needs to register meshers / writers / readers against a host.
Audience: plugin authors. Surface: `souxmar_register_*`,
`souxmar_value_*`, `souxmar_mesh_*`, etc.

The bridge C ABI in `include/souxmar-c-bridge/` is for the
desktop's Rust side calling *into* the engine. Audience: the
souxmar-bridge crate (and whatever other host wrappers we
write). Surface: `souxmar_bridge_pipeline_parse`,
`souxmar_bridge_pipeline_stage_at`, etc.

Two libraries means:

- A plugin doesn't accidentally link the bridge (and pull in
  pipeline / core / AI transitive deps it doesn't need).
- A desktop build doesn't accidentally bring the plugin-host
  ABI's transitive deps (`dlopen`, manifest TOML, conformance
  surface).
- The two surfaces can version independently — the plugin ABI
  is frozen final at v1.3; the bridge ABI starts at v1 and
  will evolve through the v1.0 release.

### 2. Static linking

`libsouxmar-c-bridge.a` is a static archive, not a `.so` /
`.dylib`. Reasons:

- The Tauri-packaged desktop binary is a single executable on
  every platform. Shared-object resolution rules differ
  meaningfully across Linux distros, macOS Gatekeeper, and
  Windows DLL search paths; static linking sidesteps all three.
- The bridge does not need to be replaceable independently of
  the desktop binary — they are released together, signed
  together, and bump version together. The plugin C ABI has
  the *opposite* property (plugins ship independently of the
  host) which is why it lives in `.so`/`.dll`/`.dylib`.
- The bridge's transitive deps (libsouxmar-pipeline, libsouxmar-
  core) are likewise statically linked into the bundle — the
  bridge's archive composes naturally with the rest.

### 3. Hand-written FFI bindings, no bindgen / cbindgen

The bridge's C ABI surface is small — six functions at v1.
Hand-writing the Rust `extern "C"` block in
`souxmar-bridge/src/ffi.rs` is faster than wiring bindgen, makes
the binding diff readable in code review, and keeps the FFI
boundary's invariants (string ownership, error path) explicit at
the function-by-function level.

When the surface grows past ~30 functions (estimated: Sprint 14+
when viewport rendering and provider routing land), we'll
revisit. Until then: hand-written, one declaration block, one
file.

cbindgen *generating* the C header from the Rust side is
explicitly **not** the chosen direction — the ABI is C++→Rust,
not Rust→C++. The C header is the source of truth; Rust
mirrors it.

### 4. Cargo feature `real-ffi`, off by default

The `souxmar-bridge` crate compiles cleanly *without*
libsouxmar-c-bridge available. The `real-ffi` feature gates
linking. Reasons:

- `cargo check` / clippy / IDE intellisense across the Rust
  workspace doesn't need the C++ side built. Without the
  feature flag, the workspace becomes uneditable until CMake
  built the bridge.
- CI workflows that exercise only the React+Tauri visual layer
  (the visual-regression suite) don't need the C++ build.
- Future contributors who clone the repo and `cargo build` to
  poke at the Rust side get a clean build instead of an
  obscure linker error.

When the feature is **off**: `pipeline_summary` returns
`FeatureNotWired`; `BridgeFeatureSet::pipeline_introspection`
defaults to `false`; the React inspector renders the existing
"Sprint 13+" empty state. Identical behaviour to Sprint 12 push 2.

When the feature is **on**: build.rs reads
`SOUXMAR_C_BRIDGE_LIB_DIR` to find the archive,
`pipeline_summary` actually parses the pipeline,
`BridgeFeatureSet::pipeline_introspection` defaults to `true`,
the inspector renders the stage list.

The release CI build flips `real-ffi` on. Developer builds
default off (with an env var or `--features real-ffi` for
end-to-end exercise).

### 5. ABI version byte on every call

The bridge declares `souxmar_bridge_abi_version()` returning
a `uint32_t`. The Rust side compares this against
`EXPECTED_ABI_VERSION` at the top of every FFI wrapper call —
not just startup. Reasons:

- A partial-upgrade scenario (new desktop binary, stale
  `libsouxmar-c-bridge.a` cached somewhere in the build tree)
  shouldn't silently corrupt memory through a mismatched
  function signature.
- The check is cheap (one function call returning a u32).
- The check fails loudly with a typed `BridgeError::FfiCallFailed`
  message that names both versions — actionable for the
  reporter.

Aligned with `BridgeFeatureSet.bridge_protocol_version` — a
single number names "this build's bridge surface" across both
the feature-flag broadcast and the per-call cross-check.

## Consequences

- `BridgeFeatureSet::pipeline_introspection` flips from
  always-false to "true when the build was real-ffi". The flag
  is now structural rather than aspirational.
- The release CI build matrix grows by one dimension: each
  platform now builds `libsouxmar-c-bridge.a` first, then
  passes `SOUXMAR_C_BRIDGE_LIB_DIR` to the Rust build.
- The `eval-nightly` workflow gains a build of the `souxmar`
  CLI binary (already added in push 2 for the docs generator).
- Future FFI surfaces (`viewport_renderer` in Sprint 14,
  `provider_call` in Sprint 14, `auto_updater_menu` in Sprint
  15) follow the same template: header declaration in
  `souxmar-c-bridge/<surface>.h`, implementation in
  `src/c-bridge/<surface>.cpp`, Rust binding in
  `souxmar-bridge/src/ffi/<surface>.rs`, Bridge wrapper +
  Tauri command + React mirror.

## Risks

- **The hand-written FFI binding can drift from the header.**
  Mitigation: the ABI version check catches signature mismatch
  at runtime. Adding a compile-time check (e.g. parsing the
  header via a build.rs script + asserting against the Rust
  declarations) is queued for Sprint 14+ if drift bites.
- **Cargo feature flags can confuse new contributors.** A
  contributor who builds without `real-ffi` and sees the
  inspector render the scaffolding empty state may report it
  as a bug. Mitigation: the empty-state copy says
  "pipeline_introspection flag off" — surfaces the flag name
  the contributor can grep for.

## Related ADRs

- ADR-0001 (plugin C ABI) — the *other* C ABI in the project;
  this ADR explains why they're separate.
- ADR-0016 (BridgeFeatureSet contract) — the typed struct this
  ADR's `pipeline_introspection` flag lives in.
- ADR-0017 (public-alpha bug-discovery model) — synthetic-load
  harness will gain a bridge-FFI-pipeline-introspection target
  in Sprint 14 once the corpus is initialised.

— Sprint 13 push 3.
