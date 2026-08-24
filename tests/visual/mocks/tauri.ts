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
//
// The table used to hold five commands while bridge.ts's CommandName
// union declared 28, so every surface past the wizard's first two
// steps hit the throwing default and rendered its error branch into
// the screenshot. The default still throws on purpose — an un-mocked
// command has to fail loudly here rather than resolve to `undefined`
// and quietly seed a baseline of a panel no user will ever see.
//
// The payloads below are typed against bridge.ts's own interfaces so
// that renaming a field on the Rust side (which lands in bridge.ts
// first, per ADR-0016) fails `tsc` in this file instead of drifting
// silently the way `chat_send` did between b9dc123 and today.

import type {
  BridgeFeatureSet,
  ChatSummary,
  FileEntry,
} from "../../../src/desktop/src/tauri/bridge";

// Mirrors `fallbackFeatureSet` (bridge.ts:77) rather than importing it:
// a value import would drag `@tauri-apps/api/core` into this harness's
// otherwise dependency-free node_modules. Keeping the same all-off
// values means mocking `bridge_feature_set` changes nothing on screen —
// the panels render the honestly-scaffolded variant they rendered when
// the command threw, so the baselines stay comparable across the fix.
const featureSet: BridgeFeatureSet = {
  viewport_renderer:       false,
  pipeline_introspection:  false,
  provider_call:           false,
  keychain_write:          true,
  auto_updater_menu:       false,
  bridge_protocol_version: 1,
};

// Chat.tsx's `applyTurn` (chat/Chat.tsx:63) reads `.error`,
// `.tool_calls`, `.reply_text` and `.pending` off whatever chat_send
// resolves to. Omit any of them — as the bare-string mock did — and the
// turn renders as nothing at all: no bubble, no error, just the user's
// own message and a spec that times out waiting for the reply.
const chatSummary: ChatSummary = {
  reply_text: "",          // filled in per call inside the page, below
  provider:   "stub",
  tokens_in:  0,
  tokens_out: 0,
  error:      null,
  tool_calls: [],
  pending:    null,
};

/** The ChatSummary skeleton as a JS object literal. Exported so a spec
 *  that overrides `chat_send` inline (workbench.spec.ts) composes the
 *  same typed shape instead of hand-rolling a second one that can rot
 *  independently. */
export const chatSummaryLiteral = JSON.stringify(chatSummary);

// Root path matches what `open_sample_project` hands back below, because
// Workbench.tsx:80 feeds that string straight into `list_project_files`
// and then strips it as a prefix (Workbench.tsx:367) to build the
// viewer's project-relative path. Two different roots would produce
// absolute paths in the project tree's click handler.
const sampleProjectTree: FileEntry = {
  name:     "cantilever-beam",
  path:     "/home/test/souxmar-projects/cantilever-beam",
  is_dir:   true,
  children: [
    {
      name:     "pipeline.yaml",
      path:     "/home/test/souxmar-projects/cantilever-beam/pipeline.yaml",
      is_dir:   false,
      children: [],
    },
    {
      name:     "README.md",
      path:     "/home/test/souxmar-projects/cantilever-beam/README.md",
      is_dir:   false,
      children: [],
    },
    {
      name:     "geometry",
      path:     "/home/test/souxmar-projects/cantilever-beam/geometry",
      is_dir:   true,
      children: [
        {
          name:     "beam.stl",
          path:     "/home/test/souxmar-projects/cantilever-beam/geometry/beam.stl",
          is_dir:   false,
          children: [],
        },
      ],
    },
  ],
};

// `byok_active` is the one command here with no mirror in bridge.ts —
// it is named in the CommandName union but no React caller has needed
// its payload yet, so there is no interface to type against. This shape
// is transcribed by hand from the Rust `ActiveProvider` struct at
// src/desktop/src-tauri/src/commands.rs:139, and unlike the others it
// will NOT break at tsc time if that struct changes.
interface ActiveProvider {
  provider: string | null;
  model:    string | null;
  has_key:  boolean;
}

const activeProvider: ActiveProvider = {
  provider: "anthropic",
  model:    "claude-sonnet-4-20250514",
  has_key:  true,
};

export const tauriInitScript = `
  (function() {
    window.__TAURI_INTERNALS__ = window.__TAURI_INTERNALS__ || {};
    const fakeState = {
      onboarding_completed: false,
    };
    const featureSet     = ${JSON.stringify(featureSet)};
    const sampleTree     = ${JSON.stringify(sampleProjectTree)};
    const activeProvider = ${JSON.stringify(activeProvider)};
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
        case 'byok_set_active':
          // Writes settings.json and returns unit on the Rust side;
          // BYOKStep.tsx:105 only awaits it, so there is nothing to
          // hand back — but it has to resolve, or the wizard's save
          // path lands in its catch and screenshots the error branch.
          return;
        case 'byok_has_key':
          // BYOKStep.tsx:115 uses this as the "did the key stick?"
          // check for every hosted provider. Answering false would
          // carry validated=false into the Done step.
          return true;
        case 'byok_active':
          return activeProvider;
        case 'byok_test_connection':
          // Ollama: simulate "daemon present and answering".
          return true;
        case 'open_sample_project':
          return '/home/test/souxmar-projects/' + (args && args.which);
        case 'list_project_files':
          return sampleTree;
        case 'bridge_feature_set':
          return featureSet;
        case 'chat_send':
          return Object.assign(${chatSummaryLiteral}, {
            reply_text: '(scaffolding) I received: "' + (args && args.message) + '". '
                      + 'The real provider call lands later.',
          });
        case 'chat_confirm':
          // Declining is an answer, not a cancel (Chat.tsx:86), so both
          // branches resolve to a turn the panel can render.
          return Object.assign(${chatSummaryLiteral}, {
            reply_text: (args && args.allow)
              ? '(scaffolding) allowed; the resumed turn lands with the real bridge.'
              : '(scaffolding) declined; the agent was told and stopped there.',
          });
        default:
          throw new Error('mock: unknown command ' + cmd);
      }
    };
  })();
`;
