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
use std::path::{Path, PathBuf};

use serde::Serialize;

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
    // and we copy from there. For `tauri dev` the binary's cwd is
    // src-tauri/, with the in-tree examples three levels up at the
    // repo root — so we try a handful of candidate paths before
    // falling back to the placeholder.
    let mut candidates: Vec<PathBuf> = Vec::new();
    if let Ok(cwd) = std::env::current_dir() {
        candidates.push(cwd.join("examples").join(&which));
        candidates.push(cwd.join("../examples").join(&which));
        candidates.push(cwd.join("../../examples").join(&which));
        candidates.push(cwd.join("../../../examples").join(&which));
    }
    // CARGO_MANIFEST_DIR is the absolute src-tauri/ path baked in
    // at compile time; the repo root sits three levels up.
    candidates.push(
        PathBuf::from(env!("CARGO_MANIFEST_DIR"))
            .join("../../../examples")
            .join(&which),
    );

    let src = candidates.into_iter().find(|p| p.is_dir());
    if let Some(src) = src {
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

/// Sprint 15 push 4 — read the auto-updater's menu status for
/// the given install layout. Read-only; the desktop's "Apply
/// update" click shells out to `souxmar update apply` (per the
/// updater.h header rationale — don't double-implement the
/// state-machine surface ADR-0014 depends on).
#[tauri::command]
pub fn update_menu_status(
    target_root: String,
) -> Result<souxmar_bridge::UpdateMenuStatus, String> {
    souxmar_bridge::Bridge::new()
        .update_status(&target_root)
        .map_err(|e| e.to_string())
}

#[tauri::command]
pub fn chat_send(
    message:    String,
    project_id: String,
) -> Result<souxmar_bridge::ChatSummary, String> {
    // Sprint 14 push 4 — chat_send routes through the C bridge.
    // The Sprint 11 stub becomes the no-real-ffi fallback path:
    // when the bridge crate was built without `real-ffi`, the
    // Bridge wrapper returns FeatureNotWired which we surface as
    // a typed error to the React side. With real-ffi on, the
    // call goes through to the engine's StubProvider today;
    // Sprint 15 push 1 swaps in the configured per-project
    // provider.
    if message.trim().is_empty() {
        return Err("empty message".into());
    }

    // Render the one-message request to the proxy's openapi.yaml
    // ChatRequest shape. The bridge's regex-based extractor reads
    // model + messages; sampling knobs default. Sprint 15 push 1
    // replaces the hand-rolled JSON with a serde_json::to_string
    // once the openapi-generator wires up.
    let request_json = format!(
        r#"{{"model":"stub-model","messages":[{{"role":"user","content":"{}"}}]}}"#,
        message.trim().replace('"', "\\\"")
    );

    souxmar_bridge::Bridge::new()
        .chat_send(&request_json, &project_id)
        .map_err(|e| e.to_string())
}

// ---------------------------------------------------------------------------
// Workbench file-management commands.
//
// The "project" is a directory on disk that contains `pipeline.yaml` and an
// optional `geometry/` subdirectory for imported CAD/mesh files. These
// commands let the React side create a new project, open an existing one,
// import a model file, and walk the project tree for the sidebar.
// ---------------------------------------------------------------------------

const PROJECT_README: &str = "# {name}\n\
\n\
A souxmar project. The pipeline that runs when you press *Run* is defined\n\
in `pipeline.yaml`; imported geometry lives under `geometry/`.\n";

const PROJECT_PIPELINE_YAML: &str = "# souxmar pipeline — generated by the workbench.\n\
# Edit stages below; the workbench Run button dispatches this file.\n\
\n\
name: {name}\n\
stages:\n\
  - id: mesh\n\
    plugin: mesher.tetra.hello\n\
  - id: solve\n\
    plugin: solver.elasticity.linear\n\
  - id: write\n\
    plugin: writer.vtu\n";

#[tauri::command]
pub fn create_project(name: String, parent_dir: String) -> Result<String, String> {
    let trimmed = name.trim();
    if trimmed.is_empty() {
        return Err("project name is empty".into());
    }
    if trimmed.contains('/') || trimmed.contains('\\') {
        return Err("project name must not contain path separators".into());
    }

    let parent = if parent_dir.trim().is_empty() {
        dirs::home_dir()
            .ok_or_else(|| "no home directory".to_string())?
            .join("souxmar-projects")
    } else {
        PathBuf::from(parent_dir)
    };

    let dst = parent.join(trimmed);
    if dst.exists() {
        return Err(format!("{} already exists", dst.to_string_lossy()));
    }
    fs::create_dir_all(&dst).map_err(|e| format!("create dir: {}", e))?;
    fs::create_dir_all(dst.join("geometry")).map_err(|e| format!("create geometry: {}", e))?;
    fs::create_dir_all(dst.join("outputs")).map_err(|e| format!("create outputs: {}", e))?;

    fs::write(dst.join("pipeline.yaml"), PROJECT_PIPELINE_YAML.replace("{name}", trimmed))
        .map_err(|e| format!("write pipeline.yaml: {}", e))?;
    fs::write(dst.join("README.md"), PROJECT_README.replace("{name}", trimmed))
        .map_err(|e| format!("write README.md: {}", e))?;

    Ok(dst.to_string_lossy().into_owned())
}

#[tauri::command]
pub fn open_project(path: String) -> Result<String, String> {
    let trimmed = path.trim();
    if trimmed.is_empty() {
        return Err("path is empty".into());
    }
    let p = PathBuf::from(trimmed);
    if !p.is_dir() {
        return Err(format!("{} is not a directory", trimmed));
    }
    if !p.join("pipeline.yaml").is_file() {
        return Err(format!("{} has no pipeline.yaml", trimmed));
    }
    let canonical = fs::canonicalize(&p).map_err(|e| format!("canonicalize: {}", e))?;
    Ok(canonical.to_string_lossy().into_owned())
}

#[tauri::command]
pub fn import_model(project_path: String, source_file: String) -> Result<String, String> {
    let project = PathBuf::from(project_path.trim());
    if !project.is_dir() {
        return Err(format!("project {} is not a directory", project.to_string_lossy()));
    }
    let src = PathBuf::from(source_file.trim());
    if !src.is_file() {
        return Err(format!("{} is not a file", src.to_string_lossy()));
    }
    let file_name = src
        .file_name()
        .ok_or_else(|| "source has no file name".to_string())?;

    let geometry = project.join("geometry");
    fs::create_dir_all(&geometry).map_err(|e| format!("create geometry: {}", e))?;

    let mut dst = geometry.join(file_name);
    // If the file already exists, add a numeric suffix so we never overwrite.
    if dst.exists() {
        let stem = src.file_stem().map(|s| s.to_string_lossy().into_owned()).unwrap_or_default();
        let ext = src.extension().map(|s| s.to_string_lossy().into_owned()).unwrap_or_default();
        for i in 1..1000 {
            let candidate = if ext.is_empty() {
                geometry.join(format!("{}-{}", stem, i))
            } else {
                geometry.join(format!("{}-{}.{}", stem, i, ext))
            };
            if !candidate.exists() {
                dst = candidate;
                break;
            }
        }
    }
    fs::copy(&src, &dst).map_err(|e| format!("copy: {}", e))?;
    Ok(dst.to_string_lossy().into_owned())
}

#[derive(Serialize)]
pub struct FileEntry {
    pub name:     String,
    pub path:     String,
    pub is_dir:   bool,
    pub children: Vec<FileEntry>,
}

fn read_dir_recursive(dir: &Path, depth: u32) -> Vec<FileEntry> {
    if depth == 0 {
        return Vec::new();
    }
    let mut out = Vec::new();
    let entries = match fs::read_dir(dir) {
        Ok(e) => e,
        Err(_) => return out,
    };
    let mut collected: Vec<_> = entries.flatten().collect();
    // Stable order: directories first, then files, both alphabetical.
    collected.sort_by(|a, b| {
        let ad = a.path().is_dir();
        let bd = b.path().is_dir();
        match (ad, bd) {
            (true, false) => std::cmp::Ordering::Less,
            (false, true) => std::cmp::Ordering::Greater,
            _ => a.file_name().to_string_lossy().to_lowercase()
                .cmp(&b.file_name().to_string_lossy().to_lowercase()),
        }
    });
    for entry in collected {
        let path = entry.path();
        let name = entry.file_name().to_string_lossy().into_owned();
        // Hide dot-files and the .git tree by default — the workbench
        // doesn't surface them anywhere useful yet.
        if name.starts_with('.') {
            continue;
        }
        let is_dir = path.is_dir();
        let children = if is_dir { read_dir_recursive(&path, depth - 1) } else { Vec::new() };
        out.push(FileEntry {
            name,
            path: path.to_string_lossy().into_owned(),
            is_dir,
            children,
        });
    }
    out
}

#[tauri::command]
pub fn list_project_files(project_path: String) -> Result<FileEntry, String> {
    let root = PathBuf::from(project_path.trim());
    if !root.is_dir() {
        return Err(format!("{} is not a directory", root.to_string_lossy()));
    }
    let name = root
        .file_name()
        .map(|s| s.to_string_lossy().into_owned())
        .unwrap_or_else(|| root.to_string_lossy().into_owned());
    // Cap the walk at 4 levels — the workbench only renders a sidebar
    // tree; anything deeper is opt-in via a future "reveal in file
    // manager" action.
    let children = read_dir_recursive(&root, 4);
    Ok(FileEntry {
        name,
        path: root.to_string_lossy().into_owned(),
        is_dir: true,
        children,
    })
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
