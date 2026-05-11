// SPDX-License-Identifier: Apache-2.0
//
// Sprint 11 push 1 — minimal mock for the Tauri bridge so the visual
// harness can run inside a plain browser. Injected as a `<script>`
// tag via Playwright's `addInitScript` before each spec navigates.
//
// The shim replaces `window.__TAURI_INTERNALS__.invoke` (the
// undocumented-but-stable hook the `@tauri-apps/api` package calls
// into) with a small fakes table. Behaviour mirrors what each Tauri
// command would do in production but with side effects elided.

export const tauriInitScript = `
  (function() {
    window.__TAURI_INTERNALS__ = window.__TAURI_INTERNALS__ || {};
    const fakeState = {
      onboarding_completed: false,
    };
    window.__TAURI_INTERNALS__.invoke = async (cmd, args) => {
      switch (cmd) {
        case 'onboarding_status':
          return fakeState.onboarding_completed;
        case 'onboarding_complete':
          fakeState.onboarding_completed = true;
          return;
        case 'byok_store_key':
          // Pretend the key landed in the OS keychain.
          return;
        case 'byok_test_connection':
          // Ollama: simulate "daemon present and answering".
          return true;
        case 'open_sample_project':
          return '/home/test/souxmar-projects/' + (args && args.which);
        default:
          throw new Error('mock: unknown command ' + cmd);
      }
    };
  })();
`;
