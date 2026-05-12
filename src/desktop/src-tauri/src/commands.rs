// SPDX-License-Identifier: Apache-2.0
//
// Tauri command surface — every #[tauri::command] function the React
// frontend can invoke. Sprint 10 push 10 ships the onboarding-flow
// commands; subsequent pushes grow this file as the workbench gains
// features (open_project, list_plugins, run_stage, ...).
//
// The function bodies are deliberately small wrappers — anything that
// needs to call into the C++ libsouxmar libraries goes through the
// souxmar-bridge crate (queued for a future push); for now those code
// paths are stubbed with file-system / keyring work the React side
// can actually exercise during `tauri dev`.

use std::fs;
use std::path::PathBuf;

use crate::settings::{Settings, settings_path};

#[tauri::command]
pub fn onboarding_status() -> Result<bool, String> {
    let s = Settings::load(&settings_path()).unwrap_or_default();
    Ok(s.onboarding_completed)
}

#[tauri::command]
pub fn onboarding_complete() -> Result<(), String> {
    let path = settings_path();
    let mut s = Settings::load(&path).unwrap_or_default();
    s.onboarding_completed = true;
    s.save(&path).map_err(|e| e.to_string())
}

#[tauri::command]
pub fn byok_store_key(provider: String, key: String) -> Result<(), String> {
    // Keyring stores per-provider entries under the
    // dev.souxmar.desktop service identifier. The keyring crate maps
    // to Keychain Services on macOS, Credential Manager on Windows,
    // libsecret on Linux. Errors surface to the React side which
    // shows them under the input field.
    let entry = keyring::Entry::new("dev.souxmar.desktop", &format!("byok-{}", provider))
        .map_err(|e| format!("keyring: {}", e))?;
    entry.set_password(&key).map_err(|e| format!("keyring: {}", e))
}

#[tauri::command]
pub fn byok_test_connection(provider: String) -> Result<bool, String> {
    // For Ollama, "test connection" is a no-cost GET /api/tags. The
    // current scaffold returns false (no library probe wired); the
    // production path lands when the souxmar-bridge crate exposes
    // an OllamaProvider::available_models() call. Anthropic / OpenAI
    // are deliberately not tested here — see BYOKStep.tsx for the
    // rationale (avoid charging a "first launch" API call against
    // the user's account).
    let _ = provider;
    Ok(false)
}

#[tauri::command]
pub fn open_sample_project(which: String) -> Result<String, String> {
    let home = dirs::home_dir().ok_or_else(|| "no home directory".to_string())?;
    let dst: PathBuf = home.join("souxmar-projects").join(&which);
    fs::create_dir_all(&dst).map_err(|e| format!("create dir: {}", e))?;

    // In a real build, the bundle ships the examples under
    // /Applications/souxmar.app/Contents/Resources/examples (et al.),
    // and we copy from there. The dev-time path uses the in-tree
    // examples/ relative to the executable's working directory.
    let src = std::env::current_dir()
        .map_err(|e| format!("cwd: {}", e))?
        .join("examples")
        .join(&which);
    if src.is_dir() {
        copy_dir_recursive(&src, &dst).map_err(|e| format!("copy: {}", e))?;
    } else {
        // No source available in this build — leave an empty
        // project directory so the workbench has somewhere to open;
        // surfaces clearly to the user that "the example is missing
        // from this build".
        let placeholder = dst.join("README.md");
        fs::write(
            &placeholder,
            format!("# {} (placeholder)\n\nThe example source is missing from this build.\n", which),
        )
        .map_err(|e| format!("placeholder write: {}", e))?;
    }
    Ok(dst.to_string_lossy().into_owned())
}

/// Sprint 12 push 2 — exposes the BridgeFeatureSet contract to
/// the React side. The workbench panels query this once at
/// startup and gate their "real vs scaffolding" rendering on
/// the flags. ADR-0016 documents the stability contract.
#[tauri::command]
pub fn bridge_feature_set() -> Result<souxmar_bridge::BridgeFeatureSet, String> {
    Ok(souxmar_bridge::Bridge::new().feature_set())
}

/// Sprint 13 push 3 — first real FFI surface from React. Parses
/// the pipeline YAML through libsouxmar-c-bridge and returns the
/// inspector-panel summary. The Bridge wrapper handles both the
/// `real-ffi` path (real parse) and the skeleton fallback; the
/// React side calls this the same way regardless and surfaces the
/// `FeatureNotWired` error as the existing "scaffolding" empty
/// state if the flag is off.
#[tauri::command]
pub fn pipeline_summary(
    project_id:    String,
    pipeline_yaml: String,
) -> Result<souxmar_bridge::PipelineSummary, String> {
    souxmar_bridge::Bridge::new()
        .pipeline_summary(&project_id, &pipeline_yaml)
        .map_err(|e| e.to_string())
}

#[tauri::command]
pub fn chat_send(message: String, project_id: String) -> Result<String, String> {
    // Sprint 11 push 4 stub. The real implementation routes through
    // the souxmar-bridge FFI crate (queued for Sprint 12+) into
    // libsouxmar-ai's Provider abstraction (Sprint 10 push 9). For
    // now we return a deterministic acknowledgement so the chat panel
    // is exercisable in `tauri dev`.
    let suffix = if project_id.is_empty() {
        String::new()
    } else {
        format!(" (project: {})", project_id)
    };
    if message.trim().is_empty() {
        return Err("empty message".into());
    }
    Ok(format!(
        "(scaffolding) I received: \"{}\"{}. The real provider call \
         lands when the souxmar-bridge FFI crate wires this command \
         to libsouxmar-ai (Sprint 12+).",
        message.trim(),
        suffix,
    ))
}

fn copy_dir_recursive(src: &PathBuf, dst: &PathBuf) -> std::io::Result<()> {
    fs::create_dir_all(dst)?;
    for entry in fs::read_dir(src)? {
        let entry = entry?;
        let from = entry.path();
        let to = dst.join(entry.file_name());
        if from.is_dir() {
            copy_dir_recursive(&from, &to)?;
        } else {
            fs::copy(&from, &to)?;
        }
    }
    Ok(())
}
