// SPDX-License-Identifier: Apache-2.0
//
// Sprint 10 push 10 — the *only* place the React frontend talks to
// the Rust shell. Centralising the boundary lets us mock Tauri during
// unit tests + makes the rust-side command list a single grep.
//
// The Rust shell's command surface (defined in
// src-tauri/src/commands.rs) is the contract. Each entry below has a
// matching #[tauri::command] handler.

import { invoke } from "@tauri-apps/api/core";

export type CommandName =
  | "onboarding_status"
  | "onboarding_complete"
  | "byok_store_key"
  | "byok_test_connection"
  | "open_sample_project";

export async function invokeCommand<T>(
  name: CommandName,
  args?: Record<string, unknown>,
): Promise<T> {
  // During Vite dev (no Tauri runtime), `invoke` throws. We let the
  // caller handle that — the App component falls back to the
  // first-run path so the wizard is reachable in `vite preview`.
  return invoke<T>(name, args ?? {});
}
