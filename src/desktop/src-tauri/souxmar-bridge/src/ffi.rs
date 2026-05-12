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
}

/// Bridge ABI version this Rust crate was built against. Compared
/// against `souxmar_bridge_abi_version()` at startup to refuse a
/// mismatched library. Aligned with
/// `BridgeFeatureSet::bridge_protocol_version` so that one number
/// names the bridge surface.
pub const EXPECTED_ABI_VERSION: u32 = 1;

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
/// skeleton. Surfaced through `BridgeFeatureSet.pipeline_introspection`.
pub const fn is_real_ffi_compiled_in() -> bool {
    cfg!(feature = "real-ffi")
}
