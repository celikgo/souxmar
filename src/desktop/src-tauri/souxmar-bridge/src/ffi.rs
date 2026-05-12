// SPDX-License-Identifier: Apache-2.0
//
// FFI bindings for libsouxmar-c-bridge. Sprint 13 push 3.
//
// Mirrors include/souxmar-c-bridge/pipeline.h. The bindings are
// hand-written — small surface, no need for bindgen yet. When the
// surface grows past ~30 functions in Sprint 14+, we revisit and
// move to bindgen.
//
// This module is compiled unconditionally (so cargo check works
// without the C library available) but the `extern "C"` block is
// only **linked** when the `real-ffi` cargo feature is on. With
// the feature off, the high-level wrapper functions in this file
// return SkeletonNoFfi without ever calling into the externs.

#![cfg_attr(not(feature = "real-ffi"), allow(dead_code))]

use std::ffi::{c_char, CStr, CString};
use std::ptr;

#[repr(C)]
pub struct souxmar_bridge_pipeline_t {
    _private: [u8; 0],
}

#[repr(C)]
pub struct souxmar_bridge_chat_response_t {
    _private: [u8; 0],
}

#[repr(C)]
pub struct souxmar_bridge_update_status_t {
    _private: [u8; 0],
}

// Declared once. Only resolved at link time when `real-ffi` is on
// (build.rs gates the `cargo:rustc-link-lib`); when the feature
// is off, the high-level wrappers never call these, so the
// linker never tries to resolve them.
#[cfg(feature = "real-ffi")]
extern "C" {
    pub fn souxmar_bridge_abi_version() -> u32;

    pub fn souxmar_bridge_pipeline_parse(
        yaml: *const c_char,
        out_err: *mut *mut c_char,
    ) -> *mut souxmar_bridge_pipeline_t;

    pub fn souxmar_bridge_pipeline_stage_count(
        p: *const souxmar_bridge_pipeline_t,
    ) -> u32;

    pub fn souxmar_bridge_pipeline_stage_at(
        p: *const souxmar_bridge_pipeline_t,
        i: u32,
        out_id: *mut *const c_char,
        out_plugin: *mut *const c_char,
    ) -> i32;

    pub fn souxmar_bridge_pipeline_free(p: *mut souxmar_bridge_pipeline_t);

    pub fn souxmar_bridge_free_string(s: *mut c_char);

    // Sprint 14 push 4 — provider_call surface (bridge ABI v2).
    pub fn souxmar_bridge_chat_send(
        request_json: *const c_char,
        project_id:   *const c_char,
        out_err:      *mut *mut c_char,
    ) -> *mut souxmar_bridge_chat_response_t;

    pub fn souxmar_bridge_chat_error_kind(r: *const souxmar_bridge_chat_response_t) -> i32;
    pub fn souxmar_bridge_chat_error_text(r: *const souxmar_bridge_chat_response_t) -> *const c_char;
    pub fn souxmar_bridge_chat_reply_text(r: *const souxmar_bridge_chat_response_t) -> *const c_char;
    pub fn souxmar_bridge_chat_provider  (r: *const souxmar_bridge_chat_response_t) -> i32;
    pub fn souxmar_bridge_chat_tokens_in (r: *const souxmar_bridge_chat_response_t) -> i64;
    pub fn souxmar_bridge_chat_tokens_out(r: *const souxmar_bridge_chat_response_t) -> i64;

    pub fn souxmar_bridge_chat_response_free(r: *mut souxmar_bridge_chat_response_t);

    // Sprint 15 push 4 — auto_updater_menu surface (bridge ABI v3).
    pub fn souxmar_bridge_update_status_read(
        target_root: *const c_char,
        out_err:     *mut *mut c_char,
    ) -> *mut souxmar_bridge_update_status_t;

    pub fn souxmar_bridge_update_state             (s: *const souxmar_bridge_update_status_t) -> i32;
    pub fn souxmar_bridge_update_current_version   (s: *const souxmar_bridge_update_status_t) -> *const c_char;
    pub fn souxmar_bridge_update_available_version (s: *const souxmar_bridge_update_status_t) -> *const c_char;
    pub fn souxmar_bridge_update_detail            (s: *const souxmar_bridge_update_status_t) -> *const c_char;

    pub fn souxmar_bridge_update_status_free(s: *mut souxmar_bridge_update_status_t);
}

/// Bridge ABI version this Rust crate was built against. Compared
/// against `souxmar_bridge_abi_version()` at every call to refuse a
/// mismatched library. Aligned with
/// `BridgeFeatureSet::bridge_protocol_version` so that one number
/// names the bridge surface.
///
/// v1 (Sprint 13 push 3): pipeline introspection.
/// v2 (Sprint 14 push 4): + provider_call.
/// v3 (Sprint 15 push 4): + auto_updater_menu.
pub const EXPECTED_ABI_VERSION: u32 = 3;

/// Outcome of a real FFI call. The `Skeleton*` variants are
/// returned when the `real-ffi` feature is off; the `Ffi*`
/// variants are returned when it is on and a real call happened.
#[derive(Debug)]
pub enum FfiOutcome<T> {
    SkeletonNoFfi,
    FfiOk(T),
    FfiError(String),
    AbiMismatch { expected: u32, actual: u32 },
}

#[derive(Debug, Clone)]
pub struct ParsedStage {
    pub id:     String,
    pub plugin: String,
}

/// Parse a YAML pipeline and return its stages. The high-level
/// wrapper around the four extern functions above; copies all
/// strings eagerly so the caller doesn't have to track the
/// underlying handle's lifetime.
#[cfg(feature = "real-ffi")]
pub fn parse_pipeline_stages(yaml: &str) -> FfiOutcome<Vec<ParsedStage>> {
    // Step 0: ABI cross-check. Runs every call (cheap; saves us
    // from a desktop binary running against a stale C bridge
    // .so/.a deployed in a partial-upgrade scenario).
    let actual = unsafe { souxmar_bridge_abi_version() };
    if actual != EXPECTED_ABI_VERSION {
        return FfiOutcome::AbiMismatch {
            expected: EXPECTED_ABI_VERSION,
            actual,
        };
    }

    let cyaml = match CString::new(yaml) {
        Ok(c) => c,
        Err(_) => {
            return FfiOutcome::FfiError(
                "yaml contains an interior NUL byte".into(),
            )
        }
    };

    let mut err: *mut c_char = ptr::null_mut();
    let handle = unsafe {
        souxmar_bridge_pipeline_parse(cyaml.as_ptr(), &mut err as *mut _)
    };
    if handle.is_null() {
        let msg = if err.is_null() {
            "souxmar-c-bridge: parse_pipeline returned NULL with no error message".to_string()
        } else {
            let m = unsafe { CStr::from_ptr(err) }.to_string_lossy().into_owned();
            unsafe { souxmar_bridge_free_string(err) };
            m
        };
        return FfiOutcome::FfiError(msg);
    }

    let count = unsafe { souxmar_bridge_pipeline_stage_count(handle) };
    let mut stages = Vec::with_capacity(count as usize);
    for i in 0..count {
        let mut out_id:     *const c_char = ptr::null();
        let mut out_plugin: *const c_char = ptr::null();
        let rc = unsafe {
            souxmar_bridge_pipeline_stage_at(
                handle,
                i,
                &mut out_id     as *mut _,
                &mut out_plugin as *mut _,
            )
        };
        if rc != 0 || out_id.is_null() || out_plugin.is_null() {
            unsafe { souxmar_bridge_pipeline_free(handle) };
            return FfiOutcome::FfiError(format!(
                "souxmar-c-bridge: stage_at({}) failed (rc={})", i, rc
            ));
        }
        let id     = unsafe { CStr::from_ptr(out_id)     }.to_string_lossy().into_owned();
        let plugin = unsafe { CStr::from_ptr(out_plugin) }.to_string_lossy().into_owned();
        stages.push(ParsedStage { id, plugin });
    }

    unsafe { souxmar_bridge_pipeline_free(handle) };
    FfiOutcome::FfiOk(stages)
}

#[cfg(not(feature = "real-ffi"))]
pub fn parse_pipeline_stages(_yaml: &str) -> FfiOutcome<Vec<ParsedStage>> {
    FfiOutcome::SkeletonNoFfi
}

/// Returns whether this build is using the real FFI path or the
/// skeleton. Surfaced through `BridgeFeatureSet.pipeline_introspection`
/// + `provider_call`.
pub const fn is_real_ffi_compiled_in() -> bool {
    cfg!(feature = "real-ffi")
}

// ---- Sprint 14 push 4 — provider_call wrappers --------------------

/// Provider the engine resolved for this call. Mirrors the
/// SOUXMAR_BRIDGE_PROVIDER_* C constants.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ChatProvider {
    Unknown,
    Stub,
    Anthropic,
    OpenAI,
    Ollama,
    Managed,
}

impl From<i32> for ChatProvider {
    fn from(v: i32) -> Self {
        match v {
            1 => Self::Stub,
            2 => Self::Anthropic,
            3 => Self::OpenAI,
            4 => Self::Ollama,
            5 => Self::Managed,
            _ => Self::Unknown,
        }
    }
}

/// Typed error returned by the chat-send FFI. Mirrors the
/// SOUXMAR_BRIDGE_PE_* C constants + adds a Skeleton variant for
/// the no-real-ffi case (same shape as FfiOutcome above).
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ChatErrorKind {
    Ok,
    HttpError,
    Timeout,
    InvalidResponse,
    Unauthorized,
    RateLimited,
    QuotaExhausted,
    NotConfigured,
    Internal,
    Unknown(i32),
}

impl From<i32> for ChatErrorKind {
    fn from(v: i32) -> Self {
        match v {
            0 => Self::Ok,
            1 => Self::HttpError,
            2 => Self::Timeout,
            3 => Self::InvalidResponse,
            4 => Self::Unauthorized,
            5 => Self::RateLimited,
            6 => Self::QuotaExhausted,
            7 => Self::NotConfigured,
            8 => Self::Internal,
            n => Self::Unknown(n),
        }
    }
}

#[derive(Debug, Clone)]
pub struct ChatOk {
    pub reply_text: String,
    pub provider:   ChatProvider,
    pub tokens_in:  i64,
    pub tokens_out: i64,
}

#[derive(Debug, Clone)]
pub struct ChatErr {
    pub kind:       ChatErrorKind,
    pub text:       String,
    pub provider:   ChatProvider,
}

#[cfg(feature = "real-ffi")]
pub fn chat_send(request_json: &str, project_id: &str) -> FfiOutcome<Result<ChatOk, ChatErr>> {
    let actual = unsafe { souxmar_bridge_abi_version() };
    if actual != EXPECTED_ABI_VERSION {
        return FfiOutcome::AbiMismatch {
            expected: EXPECTED_ABI_VERSION,
            actual,
        };
    }

    let creq = match CString::new(request_json) {
        Ok(c) => c,
        Err(_) => return FfiOutcome::FfiError("request_json contains an interior NUL byte".into()),
    };
    let cpid = match CString::new(project_id) {
        Ok(c) => c,
        Err(_) => return FfiOutcome::FfiError("project_id contains an interior NUL byte".into()),
    };

    let mut err: *mut c_char = ptr::null_mut();
    let handle = unsafe {
        souxmar_bridge_chat_send(creq.as_ptr(), cpid.as_ptr(), &mut err as *mut _)
    };
    if handle.is_null() {
        let msg = if err.is_null() {
            "souxmar-c-bridge: chat_send returned NULL with no error message".to_string()
        } else {
            let m = unsafe { CStr::from_ptr(err) }.to_string_lossy().into_owned();
            unsafe { souxmar_bridge_free_string(err) };
            m
        };
        return FfiOutcome::FfiError(msg);
    }

    let kind_i  = unsafe { souxmar_bridge_chat_error_kind(handle) };
    let kind    = ChatErrorKind::from(kind_i);
    let provider = ChatProvider::from(unsafe { souxmar_bridge_chat_provider(handle) });

    let result = if kind == ChatErrorKind::Ok {
        let reply = unsafe { CStr::from_ptr(souxmar_bridge_chat_reply_text(handle)) }
            .to_string_lossy().into_owned();
        let tin   = unsafe { souxmar_bridge_chat_tokens_in(handle) };
        let tout  = unsafe { souxmar_bridge_chat_tokens_out(handle) };
        Ok(ChatOk { reply_text: reply, provider, tokens_in: tin, tokens_out: tout })
    } else {
        let txt = unsafe { CStr::from_ptr(souxmar_bridge_chat_error_text(handle)) }
            .to_string_lossy().into_owned();
        Err(ChatErr { kind, text: txt, provider })
    };

    unsafe { souxmar_bridge_chat_response_free(handle) };
    FfiOutcome::FfiOk(result)
}

#[cfg(not(feature = "real-ffi"))]
pub fn chat_send(_request_json: &str, _project_id: &str) -> FfiOutcome<Result<ChatOk, ChatErr>> {
    FfiOutcome::SkeletonNoFfi
}

// ---- Sprint 15 push 4 — auto_updater_menu wrappers ------------------

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum UpdateState {
    Unknown,
    UpToDate,
    Available,
    Staged,
    Refused,
    Corrupted,
    UnknownCode(i32),
}

impl From<i32> for UpdateState {
    fn from(v: i32) -> Self {
        match v {
            0 => Self::Unknown,
            1 => Self::UpToDate,
            2 => Self::Available,
            3 => Self::Staged,
            4 => Self::Refused,
            5 => Self::Corrupted,
            n => Self::UnknownCode(n),
        }
    }
}

#[derive(Debug, Clone)]
pub struct UpdateStatus {
    pub state:             UpdateState,
    pub current_version:   String,
    pub available_version: String,
    pub detail:            String,
}

#[cfg(feature = "real-ffi")]
pub fn read_update_status(target_root: &str) -> FfiOutcome<UpdateStatus> {
    let actual = unsafe { souxmar_bridge_abi_version() };
    if actual != EXPECTED_ABI_VERSION {
        return FfiOutcome::AbiMismatch {
            expected: EXPECTED_ABI_VERSION,
            actual,
        };
    }
    let croot = match CString::new(target_root) {
        Ok(c) => c,
        Err(_) => return FfiOutcome::FfiError("target_root contains an interior NUL byte".into()),
    };
    let mut err: *mut c_char = ptr::null_mut();
    let handle = unsafe {
        souxmar_bridge_update_status_read(croot.as_ptr(), &mut err as *mut _)
    };
    if handle.is_null() {
        let msg = if err.is_null() {
            "souxmar-c-bridge: update_status_read returned NULL with no error message".to_string()
        } else {
            let m = unsafe { CStr::from_ptr(err) }.to_string_lossy().into_owned();
            unsafe { souxmar_bridge_free_string(err) };
            m
        };
        return FfiOutcome::FfiError(msg);
    }
    let state   = UpdateState::from(unsafe { souxmar_bridge_update_state(handle) });
    let current = unsafe { CStr::from_ptr(souxmar_bridge_update_current_version(handle)) }
        .to_string_lossy().into_owned();
    let avail   = unsafe { CStr::from_ptr(souxmar_bridge_update_available_version(handle)) }
        .to_string_lossy().into_owned();
    let detail  = unsafe { CStr::from_ptr(souxmar_bridge_update_detail(handle)) }
        .to_string_lossy().into_owned();
    unsafe { souxmar_bridge_update_status_free(handle) };
    FfiOutcome::FfiOk(UpdateStatus {
        state,
        current_version:   current,
        available_version: avail,
        detail,
    })
}

#[cfg(not(feature = "real-ffi"))]
pub fn read_update_status(_target_root: &str) -> FfiOutcome<UpdateStatus> {
    FfiOutcome::SkeletonNoFfi
}
