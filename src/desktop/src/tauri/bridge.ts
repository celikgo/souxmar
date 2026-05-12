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
  | "open_sample_project"
  // Sprint 11 push 4 — workbench chat.
  | "chat_send"
  // Sprint 12 push 2 — FFI bridge feature-set query.
  | "bridge_feature_set"
  // Sprint 13 push 3 — first real FFI: pipeline introspection.
  | "pipeline_summary"
  // Sprint 15 push 4 — third real FFI: auto-updater menu.
  | "update_menu_status";

// Sprint 12 push 2 — BridgeFeatureSet mirror of the Rust struct.
// Renaming or removing fields here without a matching change to
// src-tauri/souxmar-bridge/src/lib.rs is a breaking change per
// ADR-0016. Adding a field is non-breaking — the React side will
// receive `undefined` until both sides ship the new flag.
export interface BridgeFeatureSet {
  viewport_renderer:       boolean;
  pipeline_introspection:  boolean;
  provider_call:           boolean;
  keychain_write:          boolean;
  auto_updater_menu:       boolean;
  bridge_protocol_version: number;
}

export async function invokeCommand<T>(
  name: CommandName,
  args?: Record<string, unknown>,
): Promise<T> {
  // During Vite dev (no Tauri runtime), `invoke` throws. We let the
  // caller handle that — the App component falls back to the
  // first-run path so the wizard is reachable in `vite preview`.
  return invoke<T>(name, args ?? {});
}

// Default feature set the React side falls back to when the Tauri
// runtime is absent (vite preview / Playwright). Everything off
// except keychain_write (consistent with the Rust-side default).
export const fallbackFeatureSet: BridgeFeatureSet = {
  viewport_renderer:       false,
  pipeline_introspection:  false,
  provider_call:           false,
  keychain_write:          true,
  auto_updater_menu:       false,
  bridge_protocol_version: 1,
};

// Sprint 13 push 3 — pipeline_summary command shapes. Mirrors
// the souxmar_bridge::PipelineSummary + PipelineStageSummary
// Rust structs. Adding a field is non-breaking per ADR-0016.
export interface PipelineStageSummary {
  id:     string;
  plugin: string;
  status: string;   // pending | running | ok | failed | cached
}

export interface PipelineSummary {
  project_id:  string;
  stage_count: number;
  stages:      PipelineStageSummary[];
}

// Sprint 14 push 4 — chat_send return shape. Mirrors
// souxmar_bridge::ChatSummary. The `error` field is populated
// when the upstream provider returned a typed ProviderError;
// the FFI wrapper itself succeeded.
export interface ChatErrorSummary {
  kind: string;
  text: string;
}

export interface ChatSummary {
  reply_text: string;
  provider:   string;   // "stub" | "anthropic" | "openai" | "ollama" | "managed" | "unknown"
  tokens_in:  number;
  tokens_out: number;
  error:      ChatErrorSummary | null;
}

// Sprint 15 push 4 — auto-updater menu status. Mirrors
// souxmar_bridge::UpdateMenuStatus.
export interface UpdateMenuStatus {
  state:             string;  // "unknown" | "up_to_date" | "available" | "staged" | "refused" | "corrupted"
  current_version:   string;
  available_version: string;
  detail:            string;
}
