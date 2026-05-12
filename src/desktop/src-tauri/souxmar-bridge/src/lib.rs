// SPDX-License-Identifier: Apache-2.0
//
// souxmar-bridge — the Rust ⇄ C++ FFI boundary the desktop shell
// calls through to reach libsouxmar-*. Sprint 12 push 2 skeleton;
// ADR-0016 ratifies the BridgeFeatureSet contract this crate
// exposes.
//
// What this crate is for:
//
//   * One named place every Tauri command goes through when it
//     needs to touch the C++ side. Today nothing actually
//     calls into libsouxmar (the FFI bindings are queued for
//     Sprint 13+), but the contract for *what's wired vs.
//     scaffolding* lives here so each new feature opts into a
//     single coordinated boolean rather than each surface
//     inventing its own toggle.
//
//   * Future home of the cbindgen-generated headers that let
//     Rust call into the libsouxmar-* C++ libraries. The
//     boundary will be C ABI (`extern "C"` on the Rust side,
//     plain functions exported from a future
//     `libsouxmar-c-bridge.so` on the C++ side).
//
// What this crate is NOT for:
//
//   * Tauri commands. Those live in `src-tauri/src/commands.rs`
//     and import this crate. Keeping the IPC layer separate
//     from the FFI layer lets unit tests exercise the bridge
//     without a Tauri runtime.
//
//   * Direct linking against libsouxmar-pipeline / -core / -ai
//     today. The skeleton stubs every feature off so the
//     workbench renders the "(Sprint 13+)" empty states until
//     each feature flips on individually.

pub mod ffi;

use serde::Serialize;

/// Which workbench surfaces are wired to real implementations
/// today. The desktop app calls `Bridge::feature_set()` at
/// startup and each panel renders accordingly.
///
/// **Stability:** the *names* and *type* of every field are
/// load-bearing — the React side queries them by name. Adding
/// a field is non-breaking; renaming or removing is a Tier-2
/// change requiring a deprecation cycle. See ADR-0016.
#[derive(Debug, Clone, Serialize)]
pub struct BridgeFeatureSet {
    /// Whether the Three.js viewport receives real Mesh handles
    /// from libsouxmar-core. False today; Sprint 13 push 2
    /// flips it on alongside the cbindgen scaffolding.
    pub viewport_renderer:        bool,

    /// Whether the inspector panel sees real pipeline-runner
    /// state. False today; depends on libsouxmar-pipeline FFI
    /// (Sprint 13+).
    pub pipeline_introspection:   bool,

    /// Whether the chat panel routes to a real Provider call
    /// (libsouxmar-ai). False today; depends on the Provider
    /// abstraction from Sprint 10 push 9 being callable from
    /// Rust via cbindgen.
    pub provider_call:            bool,

    /// Whether the BYOK key flow uses the real OS keychain.
    /// True today — keychain access is via the Rust `keyring`
    /// crate, no C++ FFI needed (Sprint 10 push 10 wired this
    /// directly).
    pub keychain_write:           bool,

    /// Whether the auto-updater is wired into the desktop
    /// app's "Check for updates" menu. False today; depends
    /// on the auto-updater being callable from Rust through
    /// the bridge (Sprint 13+). The CLI's `souxmar update`
    /// path already works without this flag — this is
    /// specifically about the in-app menu surface.
    pub auto_updater_menu:        bool,

    /// Bridge version, for debug + bug reports. Bumps on
    /// every breaking change to the BridgeFeatureSet shape.
    pub bridge_protocol_version:  u32,
}

impl Default for BridgeFeatureSet {
    /// The scaffolding default. As individual surfaces wire up,
    /// the matching field flips to `true` in the relevant push's
    /// commit. The release CI workflow checks that for `stable`
    /// release builds at least `provider_call + pipeline_introspection`
    /// are true (or the desktop app is unfit for stable use).
    ///
    /// Sprint 13 push 3 — `pipeline_introspection` flips on when
    /// the `real-ffi` cargo feature is compiled in (the build was
    /// linked against libsouxmar-c-bridge). The flag is structural,
    /// not feature-flag-only — if compile failed, the symbol is
    /// absent and the desktop falls back to the skeleton path.
    fn default() -> Self {
        BridgeFeatureSet {
            viewport_renderer:       false,
            pipeline_introspection:  ffi::is_real_ffi_compiled_in(),
            provider_call:           false,
            keychain_write:          true,
            auto_updater_menu:       false,
            bridge_protocol_version: ffi::EXPECTED_ABI_VERSION,
        }
    }
}

/// Top-level bridge handle. Today a zero-cost stateless type;
/// once FFI lands, this will own the libsouxmar-* handle pool +
/// the shared-mmap region map (per DESKTOP_APP.md § "Async / IPC").
#[derive(Debug, Default)]
pub struct Bridge;

impl Bridge {
    pub fn new() -> Self { Bridge }

    pub fn feature_set(&self) -> BridgeFeatureSet {
        BridgeFeatureSet::default()
    }

    /// Parse a pipeline YAML document and return a stage-by-stage
    /// summary for the inspector panel.
    ///
    /// Sprint 13 push 3 — first real FFI call. Routes through
    /// `libsouxmar-c-bridge`'s `souxmar_bridge_pipeline_parse()`
    /// when `real-ffi` is on; falls back to `FeatureNotWired` when
    /// it's off (the skeleton path). The Tauri command wrapper
    /// surfaces both states the same way to the React side so the
    /// inspector panel renders the same "scaffolding" empty state
    /// whether the C bridge is absent (build) or unavailable
    /// (runtime) — the user-visible distinction would not help
    /// them act differently.
    pub fn pipeline_summary(
        &self,
        project_id: &str,
        pipeline_yaml: &str,
    ) -> Result<PipelineSummary, BridgeError> {
        match ffi::parse_pipeline_stages(pipeline_yaml) {
            ffi::FfiOutcome::SkeletonNoFfi => {
                Err(BridgeError::FeatureNotWired("pipeline_introspection".into()))
            }
            ffi::FfiOutcome::AbiMismatch { expected, actual } => {
                Err(BridgeError::FfiCallFailed(format!(
                    "souxmar-c-bridge ABI mismatch: bridge built against \
                     v{}, library reports v{}", expected, actual
                )))
            }
            ffi::FfiOutcome::FfiError(msg) => {
                Err(BridgeError::FfiCallFailed(msg))
            }
            ffi::FfiOutcome::FfiOk(stages) => {
                let stages = stages
                    .into_iter()
                    .map(|s| PipelineStageSummary {
                        id:     s.id,
                        plugin: s.plugin,
                        status: "pending".into(), // run-state lands later
                    })
                    .collect::<Vec<_>>();
                Ok(PipelineSummary {
                    project_id:  project_id.to_string(),
                    stage_count: stages.len() as u32,
                    stages,
                })
            }
        }
    }

    /// Stub. Real implementation routes through
    /// `libsouxmar_ai_chat()` returning a streaming handle.
    pub fn chat_send(
        &self,
        _message: &str,
        _project_id: &str,
    ) -> Result<String, BridgeError> {
        Err(BridgeError::FeatureNotWired("provider_call".into()))
    }
}

#[derive(Debug, Clone, Serialize)]
pub struct PipelineSummary {
    pub project_id:  String,
    pub stage_count: u32,
    pub stages:      Vec<PipelineStageSummary>,
}

#[derive(Debug, Clone, Serialize)]
pub struct PipelineStageSummary {
    pub id:           String,
    pub plugin:       String,
    pub status:       String,   // pending | running | ok | failed | cached
}

#[derive(Debug)]
pub enum BridgeError {
    FeatureNotWired(String),
    FfiCallFailed(String),
}

// Hand-rolled Display + Error so the skeleton doesn't need
// `thiserror`. When the first real FFI call lands, swap to
// `#[derive(thiserror::Error)]` and add `thiserror` to the
// Cargo.toml deps — single-line patch.

impl std::fmt::Display for BridgeError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            BridgeError::FeatureNotWired(name) => {
                write!(f, "bridge feature '{}' is not yet wired in this build", name)
            }
            BridgeError::FfiCallFailed(msg) => {
                write!(f, "ffi call failed: {}", msg)
            }
        }
    }
}

impl std::error::Error for BridgeError {}
