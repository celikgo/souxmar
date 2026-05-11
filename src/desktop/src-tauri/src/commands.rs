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
