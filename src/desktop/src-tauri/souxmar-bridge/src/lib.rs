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
// Sprint 17 push 1 — typed mirror of the CLI's --json shapes
// (ADR-0025). Consumed by the desktop's shell-out paths per
// ADR-0022's MVC-via-subprocess pattern.
pub mod cli_shapes;

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
            provider_call:           ffi::is_real_ffi_compiled_in(),
            keychain_write:          true,
            // Sprint 15 push 4 — third flag flips structural.
            auto_updater_menu:       ffi::is_real_ffi_compiled_in(),
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

    /// Send one chat-completion request through the engine's
    /// Provider abstraction (libsouxmar-ai).
    ///
    /// Sprint 14 push 4 — second real FFI call. Same template as
    /// Sprint 13 push 3's pipeline_introspection. The engine
    /// today routes to StubProvider; Sprint 15 push 1 swaps in
    /// a per-project provider lookup.
    /// Answer a pending confirmation and continue the suspended turn.
    ///
    /// The turn's session — including the mesh and field handles earlier
    /// tools produced — is held in the C bridge, keyed by project, so a
    /// pause costs nothing but the wait.
    pub fn chat_confirm(&self, project_id: &str, allow: bool) -> Result<ChatSummary, BridgeError> {
        match ffi::chat_confirm(project_id, allow) {
            ffi::FfiOutcome::SkeletonNoFfi => {
                Err(BridgeError::FeatureNotWired("provider_call".into()))
            }
            ffi::FfiOutcome::AbiMismatch { expected, actual } => {
                Err(BridgeError::FfiCallFailed(format!(
                    "souxmar-c-bridge ABI mismatch: bridge built against \
                     v{}, library reports v{}", expected, actual
                )))
            }
            ffi::FfiOutcome::FfiError(msg) => Err(BridgeError::FfiCallFailed(msg)),
            ffi::FfiOutcome::FfiOk(Ok(ok)) => Ok(chat_summary_from(ok)),
            ffi::FfiOutcome::FfiOk(Err(err)) => Ok(ChatSummary {
                reply_text: String::new(),
                provider:   format!("{:?}", err.provider).to_lowercase(),
                tokens_in:  0,
                tokens_out: 0,
                tool_calls: Vec::new(),
                pending:    None,
                error: Some(ChatErrorSummary {
                    kind: format!("{:?}", err.kind),
                    text: err.text,
                }),
            }),
        }
    }

    pub fn chat_send(
        &self,
        request_json: &str,
        project_id:   &str,
    ) -> Result<ChatSummary, BridgeError> {
        match ffi::chat_send(request_json, project_id) {
            ffi::FfiOutcome::SkeletonNoFfi => {
                Err(BridgeError::FeatureNotWired("provider_call".into()))
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
            ffi::FfiOutcome::FfiOk(Ok(ok)) => Ok(chat_summary_from(ok)),
            ffi::FfiOutcome::FfiOk(Err(err)) => Ok(ChatSummary {
                reply_text: String::new(),
                provider:   format!("{:?}", err.provider).to_lowercase(),
                tokens_in:  0,
                tokens_out: 0,
                tool_calls: Vec::new(),
                pending:    None,
                error: Some(ChatErrorSummary {
                    kind: format!("{:?}", err.kind),
                    text: err.text,
                }),
            }),
        }
    }
}

#[derive(Debug, Clone, Serialize)]
pub struct ChatSummary {
    pub reply_text: String,
    pub provider:   String,
    pub tokens_in:  i64,
    pub tokens_out: i64,
    pub error:      Option<ChatErrorSummary>,
    /// Tools the agent ran during this turn, in order.
    #[serde(default)]
    pub tool_calls: Vec<ChatToolCallSummary>,
    /// Present when the turn paused for the user's approval. The panel
    /// renders a confirmation card and calls `chat_confirm`.
    #[serde(default)]
    pub pending:    Option<PendingToolSummary>,
}

#[derive(Debug, Clone, Serialize)]
pub struct ChatToolCallSummary {
    pub name:    String,
    pub summary: String,
    pub ok:      bool,
    pub refused: bool,
}

#[derive(Debug, Clone, Serialize)]
pub struct PendingToolSummary {
    pub tool_name: String,
    pub arguments: String,
}

fn chat_summary_from(ok: ffi::ChatOk) -> ChatSummary {
    ChatSummary {
        reply_text: ok.reply_text,
        provider:   format!("{:?}", ok.provider).to_lowercase(),
        tokens_in:  ok.tokens_in,
        tokens_out: ok.tokens_out,
        error:      None,
        tool_calls: ok
            .tool_calls
            .into_iter()
            .map(|c| ChatToolCallSummary {
                name:    c.name,
                summary: c.summary,
                ok:      c.ok,
                refused: c.refused,
            })
            .collect(),
        pending: ok.pending.map(|p| PendingToolSummary {
            tool_name: p.tool_name,
            arguments: p.arguments,
        }),
    }
}

#[derive(Debug, Clone, Serialize)]
pub struct ChatErrorSummary {
    pub kind: String,
    pub text: String,
}

#[derive(Debug, Clone, Serialize)]
pub struct UpdateMenuStatus {
    pub state:             String,   // "unknown" | "up_to_date" | "available" | "staged" | "refused" | "corrupted"
    pub current_version:   String,
    pub available_version: String,
    pub detail:            String,
}

impl Bridge {
    /// Sprint 15 push 4 — third real FFI surface. Read-only
    /// update status for the desktop's "Check for updates" menu.
    /// Apply / rollback shell out to `souxmar update apply` —
    /// not double-implemented through FFI (see updater.h header
    /// rationale).
    pub fn update_status(
        &self,
        target_root: &str,
    ) -> Result<UpdateMenuStatus, BridgeError> {
        use ffi::UpdateState;
        match ffi::read_update_status(target_root) {
            ffi::FfiOutcome::SkeletonNoFfi => {
                Err(BridgeError::FeatureNotWired("auto_updater_menu".into()))
            }
            ffi::FfiOutcome::AbiMismatch { expected, actual } => {
                Err(BridgeError::FfiCallFailed(format!(
                    "souxmar-c-bridge ABI mismatch: bridge built against \
                     v{}, library reports v{}", expected, actual
                )))
            }
            ffi::FfiOutcome::FfiError(msg) => Err(BridgeError::FfiCallFailed(msg)),
            ffi::FfiOutcome::FfiOk(s) => Ok(UpdateMenuStatus {
                state: match s.state {
                    UpdateState::Unknown        => "unknown".into(),
                    UpdateState::UpToDate       => "up_to_date".into(),
                    UpdateState::Available      => "available".into(),
                    UpdateState::Staged         => "staged".into(),
                    UpdateState::Refused        => "refused".into(),
                    UpdateState::Corrupted      => "corrupted".into(),
                    UpdateState::UnknownCode(n) => format!("unknown_code_{}", n),
                },
                current_version:   s.current_version,
                available_version: s.available_version,
                detail:            s.detail,
            }),
        }
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

#[cfg(test)]
mod real_ffi_tests {
    use super::*;

    /// Under `real-ffi` every structural flag must be true; without it,
    /// false. This is the assertion that would have caught the link line
    /// being broken — the feature compiled, so the flags read true in
    /// source, but nothing ever linked to prove the symbols resolved.
    #[test]
    fn feature_flags_track_the_real_ffi_build() {
        let fs = Bridge::new().feature_set();
        let expect = cfg!(feature = "real-ffi");
        assert_eq!(fs.pipeline_introspection, expect);
        assert_eq!(fs.provider_call, expect);
        assert_eq!(fs.auto_updater_menu, expect);
    }

    /// End-to-end through the C bridge into the engine's provider layer.
    ///
    /// Opt-in because it needs an endpoint: point SOUXMAR_TEST_CHAT_BASE_URL
    /// at any OpenAI-compatible server (a local fake is enough) and the
    /// test writes a project.ai.toml selecting it, then sends a chat
    /// through exactly the path the desktop Chat panel uses.
    #[test]
    fn chat_send_reaches_a_configured_openai_compatible_provider() {
        if !cfg!(feature = "real-ffi") {
            eprintln!("skipping: built without real-ffi");
            return;
        }
        let base = match std::env::var("SOUXMAR_TEST_CHAT_BASE_URL") {
            Ok(v) if !v.is_empty() => v,
            _ => {
                eprintln!("skipping: set SOUXMAR_TEST_CHAT_BASE_URL");
                return;
            }
        };

        let dir = std::env::temp_dir().join(format!("souxmar-bridge-chat-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).expect("temp project");
        std::fs::write(
            dir.join("project.ai.toml"),
            format!(
                "schema = 1\nprovider = \"openai-compatible\"\nmodel = \"test-model\"\n\n\
                 [openai_compatible]\nbase_url = \"{base}\"\n"
            ),
        )
        .expect("write config");

        let request = r#"{"model":"test-model","messages":[{"role":"user","content":"ping"}]}"#;
        let out = Bridge::new().chat_send(request, &dir.to_string_lossy());
        let _ = std::fs::remove_dir_all(&dir);

        match out {
            Ok(summary) => {
                assert!(
                    !summary.reply_text.is_empty(),
                    "provider returned an empty reply: {summary:?}"
                );
            }
            Err(e) => panic!("chat_send failed: {e:?}"),
        }
    }
}

#[cfg(test)]
mod agent_loop_tests {
    use super::*;

    /// The whole desktop path: a chat turn that reaches a tool needing
    /// approval must pause (not deny, not block), and resuming must run
    /// the tool and let the model answer.
    ///
    /// Point SOUXMAR_TEST_AGENT_BASE_URL at an OpenAI-compatible server
    /// that asks for a guarded tool. Skipped otherwise so the suite
    /// stays hermetic.
    #[test]
    fn chat_pauses_for_confirmation_and_resumes() {
        if !cfg!(feature = "real-ffi") {
            eprintln!("skipping: built without real-ffi");
            return;
        }
        let base = match std::env::var("SOUXMAR_TEST_AGENT_BASE_URL") {
            Ok(v) if !v.is_empty() => v,
            _ => {
                eprintln!("skipping: set SOUXMAR_TEST_AGENT_BASE_URL");
                return;
            }
        };

        let dir = std::env::temp_dir().join(format!("souxmar-agent-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).expect("temp project");
        std::fs::write(
            dir.join("project.ai.toml"),
            format!(
                "schema = 1\nprovider = \"openai-compatible\"\nmodel = \"test-model\"\n\n\
                 [openai_compatible]\nbase_url = \"{base}\"\n"
            ),
        )
        .expect("write config");
        let project = dir.to_string_lossy().into_owned();

        let bridge = Bridge::new();
        let first = bridge
            .chat_send(
                r#"{"model":"test-model","messages":[{"role":"user","content":"solve it"}]}"#,
                &project,
            )
            .expect("chat_send");

        assert!(
            first.pending.is_some(),
            "expected the turn to pause on a confirmation, got {first:?}"
        );
        let pending = first.pending.clone().unwrap();
        assert_eq!(pending.tool_name, "solve");

        // Declining must reach the model, not abort the turn.
        let declined = bridge.chat_confirm(&project, false).expect("chat_confirm");
        assert!(
            declined.tool_calls.iter().any(|c| c.refused),
            "the declined call was not reported as refused: {declined:?}"
        );
        assert!(declined.pending.is_none(), "still paused after answering");

        let _ = std::fs::remove_dir_all(&dir);
    }
}
