// SPDX-License-Identifier: Apache-2.0
//
// Sprint 11 push 4 — visual coverage for the workbench shell.
// Companion to onboarding.spec.ts. The mock Tauri bridge from
// onboarding.spec.ts is reused; we drive the post-onboarding path
// by setting the persisted onboarding bit and reloading.

import { test, expect } from "@playwright/test";
import { chatSummaryLiteral, tauriInitScript } from "../mocks/tauri";

const skipOnboardingScript = `
  // Override onboarding_status to return true so the workbench
  // renders without going through the wizard.
  (function() {
    const prev = window.__TAURI_INTERNALS__.invoke;
    window.__TAURI_INTERNALS__.invoke = async (cmd, args) => {
      if (cmd === 'onboarding_status') return true;
      if (cmd === 'chat_send') {
        // b9dc123 (2026-05-12) changed chat_send's return from a bare
        // string to a typed ChatSummary (bridge.ts:127). This mock kept
        // returning the string, so Chat.tsx's applyTurn read undefined
        // for both .reply_text and .tool_calls, appended no bubble at
        // all, and the reply assertion below timed out on every run.
        // The skeleton comes from mocks/tauri.ts so there is one typed
        // definition of the shape, not two.
        return Object.assign(${chatSummaryLiteral}, {
          reply_text: '(scaffolding) I received: "' + (args && args.message) + '". '
                    + 'The real provider call lands later.',
        });
      }
      return prev ? prev(cmd, args) : undefined;
    };
  })();
`;

test.beforeEach(async ({ page }) => {
  await page.addInitScript(tauriInitScript);
  await page.addInitScript(skipOnboardingScript);
});

test("Workbench empty state matches baseline", async ({ page }) => {
  await page.goto("/");
  // ca6e807 (2026-05-12) redesigned the no-project state: the viewport
  // area routes to <Welcome> whenever projectId is empty
  // (Workbench.tsx:237), which leaves Viewport.tsx:19's "No project
  // loaded" branch dead from the workbench's side — the assertion this
  // replaces could never resolve. Anchor on the ProjectTree's empty
  // copy instead (src/desktop/src/workbench/ProjectTree.tsx:88). The
  // only other place that string can surface is the terminal log line
  // at Workbench.tsx:115, which needs a Run click this spec never
  // makes; the onboarding wizard has no copy matching it at all, so a
  // run that somehow landed on the wizard fails here rather than
  // blessing it as the workbench baseline.
  await expect(page.getByText(/no project open/i).first()).toBeVisible();
  await expect(page).toHaveScreenshot("workbench-empty.png", {
    fullPage: true,
  });
});

test("Workbench chat after one message matches baseline", async ({ page }) => {
  await page.goto("/");
  // Type a short message and send via Cmd+Enter.
  const ta = page.getByPlaceholder(/ask the agent/i);
  await ta.fill("list the available plugins");
  await ta.press("Meta+Enter");
  await expect(page.getByText(/i received: "list the available plugins"/i))
      .toBeVisible();
  await expect(page).toHaveScreenshot("workbench-chat.png", {
    fullPage: true,
  });
});
