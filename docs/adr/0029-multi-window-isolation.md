# ADR-0029: Multi-window desktop + per-project AI provider isolation

- **Status:** Accepted
- **Date:** 2026-05-16 (Sprint 18 push 2)
- **Author:** souxmar desktop team
- **Deciders:** desktop, AI, security review
- **Tier:** 1 (architecture — names the multi-window model + the
  data-isolation invariant between concurrent project windows)
- **Affects:** `src/desktop/src-tauri/src/main.rs` (Tauri window
  manager); `src/desktop/src/store/` (per-window store
  isolation); the bridge's `Bridge::chat_send()` + ADR-0020's
  project.ai.toml lookup (per-window project-id).

## Context

Sprint 11 push 4's workbench shell assumed one project per
desktop window. Sprint 18's theme per SPRINT_PLAN.md is
"multi-window / multi-project polish; project-level isolation
of AI providers." Two related decisions worth recording.

## Decision

### Window model — one project per window

Each desktop window owns:

- One project id (path to project directory).
- One BridgeFeatureSet snapshot (queried at window open).
- One Zustand store instance (independent state tree).
- One Chat panel session.

Opening a second project opens a new OS window. Within one
window the user navigates panels (inspector / viewport / chat)
but the project never changes — the project picker reopens
into a fresh window.

Tauri's WindowManager API exposes this via `Window::new(label,
url)` per project; we use the project id as the label so the
OS task-switcher shows meaningful titles.

### Data-isolation invariant

**No state leaks between concurrent project windows.**
Specifically:

1. The Zustand store is window-local — created on
   `useEffect(window.id)` mount; destroyed on window close.
2. The Tauri command handlers (`commands.rs`) receive the
   project id as an argument on every call. The handler
   never reads "the current project" from process-global
   state.
3. The agent's BYOK keychain key namespace is per-project:
   `souxmar.byok.<provider>.<project-id>` (extends Sprint 10
   push 10's keychain pattern). A user who configures
   different BYOK keys for two projects gets two different
   upstream calls without manually switching.
4. Project.ai.toml (ADR-0020) is looked up per-call against
   the call's project-id — the bridge already does this since
   Sprint 15 push 2; no change required.
5. The session budget (Sprint 6 push 6) is window-local.

### What's still shared across windows

- The OS keychain (one per user; namespaces are per-project,
  but the keychain is one).
- The plugin host's registry cache (one per process; plugin
  loads cost wall time, share is the right call).
- The auto-updater state (one per install layout; not
  per-project).
- The C++ engine handles (one libsouxmar per process; shared
  but the FFI wrappers serialise per-window).

### Reasons against alternatives

- **One project per process.** Wasteful — every new project
  spawns a new desktop process, loses the plugin cache, takes
  several hundred ms. Rejected.
- **One project but switchable.** Sprint 11 push 4's model.
  Per-project AI key switching becomes a settings-panel
  affair; concurrent comparison of two projects' chat
  sessions is impossible. Rejected as a forwards-looking
  shape.

## Consequences

- `src/desktop/src-tauri/src/main.rs` gains
  `commands::open_project_in_new_window(project_path)`.
- `src/desktop/src/store/features.ts` keys its hook off
  `window.label` instead of process-global.
- The Chat panel's Zustand store gets a window-local prefix.
- Sprint 19 push 1's geometry edits inherit the per-window
  pattern.
- Sprint 22 public beta exercises the multi-window flow + the
  isolation invariant via Playwright tests against two
  concurrent windows.

## Risks

- **R-037 (cross-window keychain leakage).** The OS keychain
  is one per user. A bug in the per-project namespacing
  could let project A read project B's keys.
  **Mitigation:** keychain reads are gated by the
  per-project name pattern in the bridge wrapper; Sprint 21
  pen-test exercises the pattern.
- **R-038 (memory growth on many concurrent windows).**
  Three concurrent project windows could push memory past
  what mid-range laptops handle. **Mitigation:** Sprint 19's
  heap accountant (Sprint 9 push 9) already tracks per-FFI
  allocation; per-window snapshots in Sprint 22 beta.

— Sprint 18 push 2.
