// SPDX-License-Identifier: Apache-2.0
//
// Sprint 11 push 1 — Playwright config for the desktop visual-regression
// suite. Sprint 14 push 1 — per-platform snapshot directories.
//
// The suite runs against `vite preview` of the React frontend
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
// The baselines live alongside the spec files under per-platform
// snapshot directories (`*.spec.ts-snapshots-{linux,darwin,win32}/`)
// so font-rendering variance between OSes doesn't false-positive.
// Update them with `npm run update-baselines` when a deliberate token
// change lands; see `BASELINES.md` for the policy.

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

  // Sprint 14 push 1 — per-platform snapshot template. Playwright
  // substitutes `{platform}` with `linux`, `darwin`, or `win32`.
  // Each OS keeps its own baseline directory; cross-OS font /
  // antialiasing variance does not false-positive.
  snapshotPathTemplate:
    "{testDir}/{testFilePath}-snapshots-{platform}/{arg}{ext}",

  expect: {
    // Loose pixel comparison — anti-aliasing differences across
    // platforms eat a few thousand pixels easily. The per-platform
    // snapshot template + this budget together give the suite
    // enough slack to survive runner-to-runner variance without
    // hiding real regressions.
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
