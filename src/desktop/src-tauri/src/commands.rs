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

use serde::{Deserialize, Serialize};

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

const PROJECT_README: &str = r#"# {name}

A souxmar project. The pipeline that runs when you press *Run* is defined
in `pipeline.yaml`; imported geometry lives under `geometry/`.
"#;

const PROJECT_PIPELINE_YAML: &str = r#"# souxmar pipeline — generated by the workbench.
# Edit stages below; the workbench Run button dispatches this file.

name: {name}
stages:
  - id: mesh
    plugin: mesher.tetra.hello
  - id: solve
    plugin: solver.elasticity.linear
  - id: write
    plugin: writer.vtu
"#;

/// Boundary-condition spec received from the React workbench. `face` is
/// one of the named bbox faces (+x/-x/+y/-y/+z/-z); `vector` is only
/// meaningful when kind=force.
#[derive(Deserialize, Debug)]
#[serde(rename_all = "lowercase")]
pub enum LoadKind {
    Force,
    Fixed,
}

#[derive(Deserialize, Debug)]
pub struct LoadSpec {
    pub face:   String,
    pub kind:   LoadKind,
    pub vector: Option<[f64; 3]>,
}

/// Write the given loads into `<project>/pipeline.yaml` under the first
/// `solver.*` stage as a `loads:` array. Existing loads block is replaced
/// whole — partial merges would surprise the user. Returns a short summary.
#[tauri::command]
pub fn apply_loads_to_pipeline(
    project_path: String,
    loads:        Vec<LoadSpec>,
) -> Result<String, String> {
    let project = PathBuf::from(project_path.trim());
    let yaml_path = project.join("pipeline.yaml");
    if !yaml_path.is_file() {
        return Err(format!("{} has no pipeline.yaml", project.to_string_lossy()));
    }
    let yaml = fs::read_to_string(&yaml_path)
        .map_err(|e| format!("read pipeline.yaml: {}", e))?;

    // Build the new `loads:` block string (6-space indent matching the
    // surrounding `input:` body in the scaffold).
    let mut loads_block = String::from("      loads:\n");
    for load in &loads {
        match load.kind {
            LoadKind::Force => {
                let v = load.vector.unwrap_or([0.0, 0.0, 0.0]);
                loads_block.push_str(&format!(
                    "        - {{face: '{}', kind: force, vector: [{}, {}, {}]}}\n",
                    load.face, v[0], v[1], v[2],
                ));
            }
            LoadKind::Fixed => {
                loads_block.push_str(&format!(
                    "        - {{face: '{}', kind: fixed}}\n",
                    load.face,
                ));
            }
        }
    }

    // Find the first `solver.` stage; insert/replace its loads block.
    let solver_off = yaml
        .find("plugin: solver.")
        .ok_or_else(|| "no solver.* stage in pipeline.yaml".to_string())?;

    // Find the end of that stage = either the next `  - id:` line or EOF.
    let after_solver = &yaml[solver_off..];
    let stage_end_rel = after_solver
        .match_indices("\n  - id:")
        .next()
        .map(|(i, _)| i + 1)
        .unwrap_or(after_solver.len());
    let stage_end = solver_off + stage_end_rel;
    let stage_text = &yaml[solver_off..stage_end];

    // If the stage already has an `input:` line, replace any existing
    // `loads:` block under it; otherwise append `input:\n<loads>` to the stage.
    let new_stage = if stage_text.contains("    input:") {
        // Strip any existing `loads:` block from the stage text.
        let stripped = strip_loads_block(stage_text);
        // Append our new loads block right after the `    input:` line.
        if let Some(input_at) = stripped.find("    input:") {
            let nl = stripped[input_at..].find('\n').unwrap_or(stripped.len() - input_at);
            let mut s = String::with_capacity(stripped.len() + loads_block.len());
            s.push_str(&stripped[..input_at + nl + 1]);
            s.push_str(&loads_block);
            s.push_str(&stripped[input_at + nl + 1..]);
            s
        } else {
            stripped
        }
    } else {
        // No input: block — append one (use 4-space indent matching stage body).
        let trimmed = stage_text.trim_end_matches('\n');
        format!("{}\n    input:\n{}", trimmed, loads_block)
    };

    let mut out = String::with_capacity(yaml.len() + new_stage.len());
    out.push_str(&yaml[..solver_off]);
    out.push_str(&new_stage);
    if !new_stage.ends_with('\n') {
        out.push('\n');
    }
    out.push_str(&yaml[stage_end..]);
    fs::write(&yaml_path, out).map_err(|e| format!("write pipeline.yaml: {}", e))?;

    Ok(format!("wrote {} load(s) under the solver stage", loads.len()))
}

/// Remove any existing `      loads:` block (six-space indent) from a stage
/// snippet. The block runs from the `loads:` line up to the next line that
/// starts at the four-space (stage-body) indent level or shallower.
fn strip_loads_block(stage_text: &str) -> String {
    let needle = "      loads:";
    let Some(start) = stage_text.find(needle) else {
        return stage_text.to_string();
    };
    // Find the end: scan lines after the loads: line, stop at first line
    // that isn't more deeply indented than 6 spaces.
    let after = &stage_text[start..];
    let mut idx = after.find('\n').map(|i| i + 1).unwrap_or(after.len());
    while idx < after.len() {
        let nl = after[idx..].find('\n').map(|i| idx + i + 1).unwrap_or(after.len());
        let line = &after[idx..nl];
        let leading = line.chars().take_while(|c| *c == ' ').count();
        if leading < 8 && !line.trim().is_empty() {
            break;
        }
        idx = nl;
    }
    let end = start + idx;
    let mut out = String::with_capacity(stage_text.len());
    out.push_str(&stage_text[..start]);
    out.push_str(&stage_text[end..]);
    out
}

/// Read a geometry file off disk as raw bytes. The frontend three.js
/// loaders parse this directly (OBJLoader from a UTF-8 string, STLLoader
/// from an ArrayBuffer). Path is restricted to a project's `geometry/`
/// subdirectory to keep this from being an unbounded filesystem read.
#[tauri::command]
pub fn read_geometry_bytes(project_path: String, rel_path: String) -> Result<Vec<u8>, String> {
    let project = PathBuf::from(project_path.trim());
    if !project.is_dir() {
        return Err(format!("project {} is not a directory", project.to_string_lossy()));
    }
    // Reject any `..` traversal — rel_path must stay inside the project.
    let rel = PathBuf::from(rel_path.trim());
    for c in rel.components() {
        if matches!(c, std::path::Component::ParentDir | std::path::Component::RootDir) {
            return Err("rel_path must be inside the project".into());
        }
    }
    let abs = project.join(&rel);
    if !abs.is_file() {
        return Err(format!("{} is not a file", abs.to_string_lossy()));
    }
    fs::read(&abs).map_err(|e| format!("read {}: {}", abs.to_string_lossy(), e))
}

/// Write text content (e.g. a simplified OBJ produced by the frontend
/// three.js decimator) to a file inside the project. Path is restricted
/// the same way as `read_geometry_bytes`: no traversal, must land under
/// the project root.
#[tauri::command]
pub fn simplify_mesh(
    project_path: String,
    rel_path:     String,
    content:      String,
) -> Result<String, String> {
    let project = PathBuf::from(project_path.trim());
    if !project.is_dir() {
        return Err(format!("project {} is not a directory", project.to_string_lossy()));
    }
    let rel = PathBuf::from(rel_path.trim());
    for c in rel.components() {
        if matches!(c, std::path::Component::ParentDir | std::path::Component::RootDir) {
            return Err("rel_path must be inside the project".into());
        }
    }
    let abs = project.join(&rel);
    if let Some(parent) = abs.parent() {
        fs::create_dir_all(parent).map_err(|e| format!("create dir: {}", e))?;
    }
    fs::write(&abs, content).map_err(|e| format!("write {}: {}", abs.to_string_lossy(), e))?;
    Ok(abs.to_string_lossy().into_owned())
}

/// Open a native folder-picker dialog and return the chosen directory.
/// Returns `Ok(None)` if the user cancelled. The `start_dir` parameter is
/// the directory the picker opens at; an empty string means "platform
/// default" (typically the user's home).
#[tauri::command]
pub fn pick_directory(start_dir: String) -> Result<Option<String>, String> {
    let mut d = rfd::FileDialog::new().set_title("Choose a folder");
    let start = start_dir.trim();
    if !start.is_empty() && PathBuf::from(start).is_dir() {
        d = d.set_directory(start);
    }
    Ok(d.pick_folder().map(|p| p.to_string_lossy().into_owned()))
}

/// Open a native file-picker dialog and return the chosen file path.
/// Returns `Ok(None)` if the user cancelled. `extensions` is a list of
/// bare extensions like `["stl", "obj"]`; pass an empty list to accept
/// any file.
#[tauri::command]
pub fn pick_file(
    start_dir:  String,
    extensions: Vec<String>,
) -> Result<Option<String>, String> {
    let mut d = rfd::FileDialog::new().set_title("Choose a file");
    let start = start_dir.trim();
    if !start.is_empty() && PathBuf::from(start).is_dir() {
        d = d.set_directory(start);
    }
    if !extensions.is_empty() {
        let ext_refs: Vec<&str> = extensions.iter().map(|s| s.as_str()).collect();
        d = d.add_filter("Model files", &ext_refs);
    }
    Ok(d.pick_file().map(|p| p.to_string_lossy().into_owned()))
}

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

#[derive(Serialize)]
pub struct ImportResult {
    /// Absolute path of the imported file inside `<project>/geometry/`.
    pub dst_path:        String,
    /// Path of the file *relative to the project root* (e.g. `geometry/cube.stl`).
    pub rel_path:        String,
    /// Human-readable summary of any pipeline.yaml mutation; `None` if the
    /// pipeline was left untouched.
    pub pipeline_change: Option<String>,
}

#[tauri::command]
pub fn import_model(project_path: String, source_file: String) -> Result<ImportResult, String> {
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

    // Build the project-relative path for the pipeline reference. We assume
    // dst is always under `<project>/geometry/`, which we just created above.
    let rel_path = dst
        .strip_prefix(&project)
        .map(|p| p.to_string_lossy().into_owned())
        .unwrap_or_else(|_| format!("geometry/{}", dst.file_name().unwrap_or_default().to_string_lossy()));

    // Pick the right reader plugin for this extension. Maps to the
    // `provides:` IDs of the in-tree reader plugins. Unknown / no
    // extension means we leave pipeline.yaml alone.
    let pipeline_change = match reader_plugin_for_extension(dst.extension().and_then(|e| e.to_str())) {
        Some(plugin_id) => auto_wire_reader_pipeline(&project, &rel_path, plugin_id)?,
        None => None,
    };

    Ok(ImportResult {
        dst_path: dst.to_string_lossy().into_owned(),
        rel_path,
        pipeline_change,
    })
}

/// Map a file extension (lowercase, no leading dot) to the in-tree reader
/// plugin id that handles it. Returns `None` for extensions we don't ship
/// a reader for.
fn reader_plugin_for_extension(ext: Option<&str>) -> Option<&'static str> {
    match ext.map(|s| s.to_ascii_lowercase()).as_deref() {
        Some("stl")          => Some("reader.stl"),
        Some("obj")          => Some("reader.obj"),
        Some("step" | "stp") => Some("reader.step"),
        Some("iges" | "igs") => Some("reader.iges"),
        Some("blend")        => Some("reader.blend"),
        _ => None,
    }
}

/// Mutate `<project>/pipeline.yaml` so the named reader plugin points at
/// the just-imported file. Three cases, in order:
///   1. The pipeline already has a `plugin: <plugin_id>` stage — rewrite
///      its `path:` line.
///   2. The pipeline has the scaffold's `plugin: mesher.tetra.hello`
///      placeholder — replace that line with the reader block.
///   3. Otherwise prepend a fresh `read` stage at the top of `stages:`.
///
/// String manipulation (not YAML parsing) so user comments and formatting
/// outside the touched lines survive. Returns a human-readable summary of
/// what changed.
fn auto_wire_reader_pipeline(
    project:   &Path,
    rel_path:  &str,
    plugin_id: &str,
) -> Result<Option<String>, String> {
    let yaml_path = project.join("pipeline.yaml");
    if !yaml_path.is_file() {
        return Ok(None);
    }
    let yaml = fs::read_to_string(&yaml_path)
        .map_err(|e| format!("read pipeline.yaml: {}", e))?;

    let needle = format!("plugin: {}", plugin_id);

    // Case 1: existing reader stage of this plugin — rewrite the next `path:` line.
    if let Some(stage_idx) = yaml.find(&needle) {
        let tail = &yaml[stage_idx..];
        if let Some(path_off) = tail.find("path:") {
            let abs_path = stage_idx + path_off;
            let line_end = yaml[abs_path..].find('\n').map(|i| abs_path + i).unwrap_or(yaml.len());
            let mut out = String::with_capacity(yaml.len() + rel_path.len());
            out.push_str(&yaml[..abs_path]);
            out.push_str(&format!("path: {}", rel_path));
            out.push_str(&yaml[line_end..]);
            fs::write(&yaml_path, out).map_err(|e| format!("write pipeline.yaml: {}", e))?;
            return Ok(Some(format!("updated {} path → {}", plugin_id, rel_path)));
        }
        // Stage without a path: line — fall through to insert.
    }

    // Case 2: scaffold's mesher.tetra.hello placeholder — replace it.
    let placeholder = "    plugin: mesher.tetra.hello";
    if yaml.contains(placeholder) {
        let replacement = format!(
            "    plugin: {}\n    input:\n      path: {}",
            plugin_id, rel_path
        );
        let out = yaml.replacen(placeholder, &replacement, 1);
        fs::write(&yaml_path, out).map_err(|e| format!("write pipeline.yaml: {}", e))?;
        return Ok(Some(format!(
            "replaced mesher.tetra.hello placeholder with {} → {}",
            plugin_id, rel_path
        )));
    }

    // Case 3: prepend a fresh `read` stage under `stages:`.
    if let Some(stages_off) = yaml.find("stages:") {
        let after = &yaml[stages_off..];
        if let Some(nl) = after.find('\n') {
            let insert_at = stages_off + nl + 1;
            let block = format!(
                "  - id: read\n    plugin: {}\n    input:\n      path: {}\n",
                plugin_id, rel_path
            );
            let mut out = String::with_capacity(yaml.len() + block.len());
            out.push_str(&yaml[..insert_at]);
            out.push_str(&block);
            out.push_str(&yaml[insert_at..]);
            fs::write(&yaml_path, out).map_err(|e| format!("write pipeline.yaml: {}", e))?;
            return Ok(Some(format!("added {} stage → {}", plugin_id, rel_path)));
        }
    }

    Ok(None)
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
