// SPDX-License-Identifier: Apache-2.0
//
// Sprint 11 push 4 — visual coverage for the workbench shell.
// Companion to onboarding.spec.ts. The mock Tauri bridge from
// onboarding.spec.ts is reused; we drive the post-onboarding path
// by setting the persisted onboarding bit and reloading.

import { test, expect } from "@playwright/test";
import { tauriInitScript } from "../mocks/tauri";

const skipOnboardingScript = `
  // Override onboarding_status to return true so the workbench
  // renders without going through the wizard.
  (function() {
    const prev = window.__TAURI_INTERNALS__.invoke;
    window.__TAURI_INTERNALS__.invoke = async (cmd, args) => {
      if (cmd === 'onboarding_status') return true;
      if (cmd === 'chat_send') {
        return '(scaffolding) I received: "' + (args && args.message) + '". '
             + 'The real provider call lands later.';
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
  await expect(page.getByText(/no project loaded/i).first()).toBeVisible();
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
