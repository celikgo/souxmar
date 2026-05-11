// SPDX-License-Identifier: Apache-2.0
//
// Sprint 10 push 10 — Vite config for the desktop React frontend.
// Tauri ships its own HMR via a custom dev server hook; we keep the
// Vite config minimal and let `npm run tauri dev` orchestrate.

import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

export default defineConfig({
  plugins: [react()],
  // Tauri expects the bundled HTML/JS under `dist/`; this matches its
  // `frontendDist` setting in tauri.conf.json.
  build: {
    outDir: "dist",
    sourcemap: true,
    target: "es2020",
  },
  // The Tauri dev server runs on 1420 by default — keep them in sync.
  server: {
    port: 1420,
    strictPort: true,
  },
  clearScreen: false,
});
