// SPDX-License-Identifier: Apache-2.0
//
// Sprint 11 push 1 — visual-regression coverage for the onboarding
// wizard's four steps. Closes Sprint 10's R-012. Each step gets one
// baseline screenshot; a deliberate token / layout change updates
// the baselines via `npm run update-baselines`.

import { test, expect } from "@playwright/test";
import { tauriInitScript } from "../mocks/tauri";

test.beforeEach(async ({ page }) => {
  // Inject the Tauri mock BEFORE the page navigates so the bridge
  // is in place by the time React's useEffect fires.
  await page.addInitScript(tauriInitScript);
});

test("Welcome step matches baseline", async ({ page }) => {
  await page.goto("/");
  // The wizard starts on Welcome.
  await expect(page.getByRole("heading", { name: /welcome to souxmar/i })).toBeVisible();
  await expect(page).toHaveScreenshot("welcome.png", { fullPage: true });
});

test("BYOK step matches baseline", async ({ page }) => {
  await page.goto("/");
  await page.getByRole("button", { name: /get started/i }).click();
  // 381c09f (2026-08-18) retitled this step from "Configure your AI
  // provider" to "Connect a model". Nothing failed at review time — the
  // heading is only ever named here — so the spec went stale and every
  // run since died on "element(s) not found" before reaching the
  // screenshot, on all three runners at once.
  //
  // The durable fix is a `data-testid="byok-heading"` on the <h1> at
  // src/desktop/src/onboarding/BYOKStep.tsx:131 and a getByTestId here;
  // src/desktop is out of scope for this change, so the selector stays
  // coupled to the rendered copy and will break again the next time the
  // wording moves. Whoever renames it next should land the testid.
  await expect(page.getByRole("heading", { name: /connect a model/i })).toBeVisible();
  await expect(page).toHaveScreenshot("byok.png", { fullPage: true });
});

test("SampleProject step matches baseline", async ({ page }) => {
  await page.goto("/");
  await page.getByRole("button", { name: /get started/i }).click();
  // Skip BYOK (default provider is Anthropic; we don't want to type a
  // key into a screenshot test).
  await page.getByRole("button", { name: /skip for now/i }).click();
  await expect(page.getByRole("heading", { name: /try a sample project/i })).toBeVisible();
  await expect(page).toHaveScreenshot("sample-project.png", { fullPage: true });
});

test("Done step matches baseline", async ({ page }) => {
  await page.goto("/");
  await page.getByRole("button", { name: /get started/i }).click();
  await page.getByRole("button", { name: /skip for now/i }).click();
  await page.getByRole("button", { name: /skip — i'll start blank/i }).click();
  await expect(page.getByRole("heading", { name: /you're set up/i })).toBeVisible();
  await expect(page).toHaveScreenshot("done.png", { fullPage: true });
});
