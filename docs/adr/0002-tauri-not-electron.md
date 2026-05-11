# ADR-0002: Tauri 2 as the desktop shell, not Electron, not Qt

- **Status:** Accepted
- **Date:** 2026-05-11
- **Author:** Founders
- **Deciders:** Desktop team, Founders
- **Tier:** 3
- **Affects:** desktop, platform, security

## Context

souxmar ships a cross-platform desktop application as the primary surface for engineers. The shell technology choice constrains:

- Bundle size (matters for enterprise download budgets and air-gapped installs).
- Memory footprint at idle (engineers run multiple analysis tools at once).
- Security posture (auditability matters for regulated industries).
- Update story (cross-platform code-signed delta updates).
- Frontend skill availability (we want to hire web engineers, not GUI specialists).
- Native interop with the C++ core libraries.

The choice is one of three credible options as of 2026: Tauri 2.x, Electron, or Qt (with QML or Widgets).

## Decision

souxmar's desktop shell is **Tauri 2.x** (Rust shell + system WebView frontend). The frontend is React + TypeScript; the Rust shell exposes Tauri commands that bridge into `libsouxmar-core` / `libsouxmar-pipeline` over C FFI.

## Alternatives considered

### Electron

The mainstream choice for cross-platform desktop apps with a web frontend. Used by VS Code, Cursor, Slack, Discord. **Rejected** primarily on bundle size (~100 MB vs Tauri's ~10 MB) and memory baseline (~150 MB at idle vs Tauri's ~30 MB). Engineers running souxmar alongside CAD, ParaView, a chat client, and a browser cannot afford an Electron-shaped resident memory tax.

Secondary reasons: Electron ships its own Chromium per-app (a security update treadmill we would inherit); the security sandbox model is weaker than Tauri's allow-list IPC; binary signing pipelines are more complex.

The mitigation we considered — shared Electron runtime — does not exist as a portable distribution mechanism in 2026.

### Qt (Widgets or QML)

The traditional choice for engineering desktop apps (FreeCAD, ParaView, MeshLab). **Rejected** on three counts:

1. **Frontend skill pool.** Hiring React/TypeScript engineers is dramatically easier than hiring QML/Qt-Widgets engineers in 2026. The talent gap costs us months on hiring and then continuous bus-factor risk.
2. **Modern UI velocity.** Iterating on the dim design system, animations, and responsive layouts is faster in CSS + React than in QML + Qt's styling system. Twitter-dim parity (per `UI_DESIGN.md`) is essentially free in CSS; in QML it would be weeks of styling work.
3. **Licensing complexity.** Qt's commercial-vs-LGPL split adds review burden for paid-tier features. Tauri is MIT/Apache; no such conversation.

Qt's strengths (mature 3D widget integration, native OS look & feel) are real but not load-bearing for our use case. Three.js + VTK.js cover 3D; native look-and-feel is not what an engineering tool needs (information density beats it).

### Native per-platform (SwiftUI + WinUI + GTK)

Maximum native fit. **Rejected** as 3× the engineering cost for cosmetic gain. We would maintain three frontends; the design system would diverge; the shared logic would still need a common core. This is the approach 1Password tried and walked back from. We start where they ended.

### Web-only (PWA with WASM core)

Skip the desktop shell entirely; ship as a web app. **Rejected** for v1 — see the discussion in the README and `DESKTOP_APP.md`. Heavy native deps, regulated-industry air-gap requirement, and the size of the workloads make a browser tab the wrong shape. A read-only web *viewer* is a separate, post-1.0 question.

## Consequences

### Positive

- ~10 MB installer; ~30 MB memory at idle; ~150 ms cold launch — all materially under the budgets in `DESKTOP_APP.md`.
- React/TypeScript hiring pool is large; design-system work is cheap.
- Tauri's allow-list IPC model is auditable; every command the frontend can invoke is enumerable.
- Per-platform code-signing is well-supported (Tauri ships pipelines for Apple notarisation, Windows EV signing, Linux GPG).
- The frontend is reusable for the post-1.0 web-viewer ambition with minimal porting.

### Negative

- **Tauri 2 is younger than Electron.** Some edge cases (auto-update on Linux specifically, advanced WebView2 quirks on Windows) have weaker community knowledge.
- **System WebView variance.** macOS uses WKWebView, Windows uses WebView2, Linux uses WebKitGTK. Cross-OS rendering parity testing is mandatory; visual regression CI exists for this.
- **Rust skill required for shell work.** One senior engineer (Desktop senior) has Rust depth; mid engineer can grow into it. Hiring constraint we are aware of.
- **No "hot-reload of native code" story.** Editing C++ requires a rebuild + relaunch. Acceptable; iteration is on the React side, where Vite HMR is fast.

### Risks

- **R-002:** Tauri 2 stability issue blocks a release. Mitigation: maintain a 2-week-effort Electron-port POC as a documented bail-out; track Tauri stability monthly; never depend on a Tauri feature still marked unstable.
- **System WebView CVE forces emergency update.** Mitigation: WebView is OS-managed (not bundled), so the OS update channel handles it — we are *less* exposed than Electron, not more.
- **Hiring pool for Tauri/Rust shell work narrows growth.** Mitigation: 80 % of frontend work is React/TypeScript; only the IPC bridge needs Rust. We can hire mostly web engineers.

## Pre-mortem

*One year from today.* Tauri 2 hit a regression in WebView2 integration on Windows that broke our auto-updater silently. We shipped a release that left 30 % of users stuck on the previous version. The fix required upstream Tauri changes that took 6 weeks to land. We learned that we should have been running a daily smoke test against the latest Tauri release-candidate, not just stable, and that the Electron POC needs to be more than a stub. Leading indicator to watch: number of open Tauri issues we are subscribed to, growing relative to the rate of resolution.

## References

- `docs/DESKTOP_APP.md` — full architecture for the desktop app.
- `docs/UI_DESIGN.md` — design system the frontend implements.
- Tauri 2 release notes and stability tracking.

## History

- 2026-05-11: Proposed and accepted with the Phase-0 design.
