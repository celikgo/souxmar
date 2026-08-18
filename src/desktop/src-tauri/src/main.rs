// SPDX-License-Identifier: Apache-2.0
//
// Tauri shell entry point. Sprint 10 push 10 scaffolding.

#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod commands;
mod settings;

fn main() {
    tauri::Builder::default()
        .plugin(tauri_plugin_shell::init())
        .invoke_handler(tauri::generate_handler![
            commands::onboarding_status,
            commands::onboarding_complete,
            commands::byok_store_key,
            commands::byok_test_connection,
            // Reading the saved key back — without these the keychain
            // entry was write-only and BYOK never reached the engine.
            commands::byok_has_key,
            commands::byok_clear_key,
            commands::byok_set_active,
            commands::byok_active,
            commands::open_sample_project,
            commands::chat_send,
            commands::chat_confirm,
            commands::bridge_feature_set,
            // Sprint 13 push 3 — first real FFI command.
            commands::pipeline_summary,
            // Sprint 15 push 4 — third real FFI command.
            commands::update_menu_status,
            // Workbench file-management surface.
            commands::create_project,
            commands::open_project,
            commands::import_model,
            commands::list_project_files,
            commands::pick_directory,
            commands::pick_file,
            commands::read_geometry_bytes,
            commands::apply_loads_to_pipeline,
            commands::simplify_mesh,
            commands::write_text_file,
            commands::list_solver_capabilities,
            commands::list_mesher_capabilities,
            commands::list_capabilities,
            // Pipeline execution — shells out to the souxmar CLI per ADR-0022.
            commands::run_pipeline,
        ])
        .run(tauri::generate_context!())
        .expect("error launching souxmar-desktop");
}
