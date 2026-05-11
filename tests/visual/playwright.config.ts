// SPDX-License-Identifier: Apache-2.0
//
// Sprint 11 push 1 — Playwright config for the desktop visual-regression
// suite. The suite runs against `vite preview` of the React frontend
// (not the full Tauri binary) because:
//
//   1. The wizard's UX is browser-resolvable — the Tauri commands are
//      mocked by a small shim (see `mocks/tauri.ts`). Real Tauri-side
//      keychain writes have no visual signature; their absence in the
//      test environment is invisible.
//   2. A `vite preview` build is ~10x faster to launch than a full
//      `tauri build` + execute cycle, and runs identically on every
//      CI runner without needing a Linux-on-Linux / macOS-on-macOS
//      matrix.
//
// The baselines live alongside the spec files (Playwright's default).
// Update them with `npm run update-baselines` when a deliberate token
// change lands.

import { defineConfig, devices } from "@playwright/test";

export default defineConfig({
  testDir: "./specs",
  fullyParallel: false,           // wizard is single-window; serial is fine
  forbidOnly: !!process.env.CI,
  retries: process.env.CI ? 2 : 0,
  workers: 1,
  reporter: process.env.CI
    ? [["html", { open: "never" }], ["github"]]
    : "list",

  use: {
    baseURL: process.env.SOUXMAR_VISUAL_BASE_URL || "http://localhost:4173",
    trace: "on-first-retry",
    screenshot: "only-on-failure",
    viewport: { width: 1280, height: 800 },
  },

  webServer: process.env.SOUXMAR_VISUAL_BASE_URL
    ? undefined  // external server (e.g. tauri dev); caller is responsible
    : {
        // Build + preview the desktop React frontend from src/desktop/.
        // We override `tauri dev` because we don't need (or want) the
        // Tauri runtime here.
        command: "cd ../../src/desktop && npm run build && npx vite preview --port 4173",
        port: 4173,
        timeout: 120_000,
        reuseExistingServer: !process.env.CI,
      },

  expect: {
    // Loose pixel comparison — anti-aliasing differences across
    // platforms eat a few thousand pixels easily. Tightening this
    // is a Sprint-12+ concern; for the initial baselines, "the
    // shape is right" is the bar.
    toHaveScreenshot: {
      maxDiffPixels: 1500,
    },
  },

  projects: [
    {
      name: "chromium-dim",
      use: { ...devices["Desktop Chrome"] },
    },
  ],
});
