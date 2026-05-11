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
            commands::open_sample_project,
            commands::chat_send,
            commands::bridge_feature_set,
        ])
        .run(tauri::generate_context!())
        .expect("error launching souxmar-desktop");
}
