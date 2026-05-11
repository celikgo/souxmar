# Desktop Application

souxmar ships as a cross-platform desktop application alongside the CLI and Python library. The desktop app is the primary surface for non-programmer engineers — mechanical, structural, aerospace, architectural — and is the home of the agentic AI chat (see [`AI_INTEGRATION.md`](AI_INTEGRATION.md)).

The desktop app is open-source under the same Apache 2.0 license as the rest of the project. Premium services attached to it (managed AI, cloud sync, compute offload) are described in [`BUSINESS_MODEL.md`](BUSINESS_MODEL.md).

## Platforms

| OS         | Distribution                  | Minimum target              |
| ---------- | ----------------------------- | --------------------------- |
| macOS      | Signed, notarised `.dmg`      | macOS 13 (Ventura), arm64 + x86_64 universal |
| Windows    | Code-signed `.msi` + `.exe`   | Windows 10 22H2 / Server 2022, x86_64 + arm64 |
| Linux      | `.AppImage` + `.deb` + `.rpm` | Ubuntu 22.04 / Fedora 39 / equivalent, x86_64 + arm64 |

Auto-update is built in, signed, opt-in. No telemetry beyond optional crash reports the user must enable on first launch.

## Tech stack

| Layer                       | Choice                              | Rationale                                                                 |
| --------------------------- | ----------------------------------- | ------------------------------------------------------------------------- |
| Shell                       | **Tauri 2.x (Rust)**                | ~10 MB bundles vs Electron's 100+ MB; OS-level webview; strong sandboxing.|
| Frontend                    | **React 18 + TypeScript + Vite**    | Mainstream, large component ecosystem, fast HMR for plugin authors.       |
| UI primitives               | **Radix UI + Tailwind CSS**         | Accessible by default; design tokens drive the dim theme (see UI_DESIGN). |
| 3D viewport                 | **Three.js + VTK.js**               | WebGPU when present, WebGL2 fallback; VTK.js for native VTU/XDMF reading. |
| State                       | **Zustand**                         | Small, no boilerplate; nothing fancier needed.                            |
| Backend bridge              | **Tauri commands (Rust ⇄ C++ FFI)** | Rust shell calls into `libsouxmar-core`/`libsouxmar-pipeline` via FFI.    |
| Native heavy lifting        | **C++20 backend (this repo)**       | Same `libsouxmar-*` libraries the CLI uses. Zero duplication.             |
| Async / IPC                 | **Tauri channels + shared mmap**    | Small messages over channels; large mesh/field buffers via mmap regions.  |

The backend is the same C++ binary the CLI links to. The Tauri shell is a thin Rust adapter that exposes the C++ API as Tauri commands and serves the React frontend. There is **no separate "GUI codebase"** — the desktop app is a presentation layer over the same orchestrator everyone else uses.

## Process and IPC model

```
+-------------------------------------------------------------------+
|  Desktop Process (Tauri shell, Rust)                              |
|                                                                   |
|   +-------------------+        +------------------------------+   |
|   |  WebView          |  IPC   |  Tauri Command Handlers      |   |
|   |  (React UI)       | <----> |  (Rust)                      |   |
|   |                   |        |     |                        |   |
|   |  Chat | Viewport  |        |     v  C FFI                 |   |
|   |  Pipeline editor  |        |  libsouxmar-core/pipeline    |   |
|   |  Inspector panels |        |  (the same C++ libs as CLI)  |   |
|   +-------------------+        +------------------------------+   |
|                                            |                      |
|                                            v                      |
|                                +------------------------------+   |
|                                | Plugin Host                  |   |
|                                | (loads .so / .dylib / .dll)  |   |
|                                +------------------------------+   |
+-------------------------------------------------------------------+
        ^                                ^                  ^
        |                                |                  |
   AI provider (BYOK)            Local file system     Plugin search paths
   Anthropic / OpenAI /          (project workspace)   (per OS)
   Ollama (local)
```

- **All real work runs in-process.** The webview is a renderer; computation happens in the Rust + C++ side. We do not spawn a separate compute server.
- **Long-running work runs on a worker pool**, not on the main thread. The webview gets streaming progress events via a Tauri channel so the UI never blocks.
- **Large buffers cross the boundary by mmap, not JSON.** A 200M-cell mesh is mmapped once; the viewport reads vertex/index buffers directly. JSON is reserved for control messages.
- **Plugins load into the same process.** The plugin host's crash isolation (signal/SEH frame around every plugin call, see [`PLUGIN_SDK.md`](PLUGIN_SDK.md)) protects the desktop app from a misbehaving plugin the same way it protects the CLI.

## Layout

```
+---------------------------------------------------------------+
| Title bar / project switcher                                  |
+----------+--------------------------------------+-------------+
|          |                                      |             |
|  Chat    |          3D Viewport                 |  Inspector  |
|  (left)  |   (geometry / mesh / fields)         |  (right)    |
|          |                                      |             |
|  - history|                                     |  Selected:  |
|  - input  |       drag, rotate, slice           |  - entity   |
|  - tools  |       overlays: BCs, materials      |  - props    |
|  invoked  |       streamlines, contour          |  - field    |
|  by AI    |                                     |    values   |
|          |                                      |             |
+----------+--------------------------------------+-------------+
|          Pipeline timeline / console / errors                 |
+---------------------------------------------------------------+
|     Status bar: backend, plugin count, AI provider, cache     |
+---------------------------------------------------------------+
```

- **Chat panel (left)** — agentic AI, BYOK or managed. Renders tool invocations as inline cards (the user sees what the AI ran). Detailed in [`AI_INTEGRATION.md`](AI_INTEGRATION.md).
- **Viewport (centre)** — Three.js scene mirrors the active pipeline state. Boundary conditions, materials, and result fields render as overlays. Camera and selection state are part of the AI's context.
- **Inspector (right)** — properties of the selected entity: a face's tag and area; a mesh cell's quality metrics; a field's min/max/mean.
- **Timeline (bottom)** — pipeline DAG as a horizontal strip. Each stage is a node; clicking shows logs and intermediate previews. Cache hits are visually distinct from re-runs.
- **Status bar** — backend health, plugin count, current AI provider, cache size, sync state.

The layout is panelled and dockable (Radix `Resizable`). All panels can be closed; the viewport never can.

## Visual language

See [`UI_DESIGN.md`](UI_DESIGN.md) for the full design system. Highlights:

- **Theme:** dark by default, using the Twitter "dim" palette as the base. A pure-black ("lights out") variant is available for OLED screens. A light theme exists for screenshots and is not the default.
- **Typography:** Inter for UI, JetBrains Mono for code/console, monospace tabular figures for numerical inspector values.
- **Density:** information-dense by default — engineers comparing five mesh metrics do not want airy whitespace. A "comfortable" density toggle exists.
- **Motion:** sparing. 150 ms ease-out for panel transitions; no decorative animation.
- **Iconography:** Lucide icon set, 16 px UI / 20 px header.

## File and project model

A souxmar **project** is a directory containing:

```
my-project/
  project.souxmar.toml      # name, plugin dependencies, AI settings
  pipelines/
    cantilever.souxmar.yaml
  geometry/                 # imported CAD
  results/                  # solver output, cached
  .souxmar/
    cache/                  # content-addressed intermediates
    chat/                   # AI conversation history (local)
    state.json              # UI state: open panels, viewport pose
```

Projects are git-friendly. The `.souxmar/` subdirectory is `.gitignore`-able by convention — it is reproducible from `pipelines/` and `geometry/`. Project files are intentionally human-readable: a senior engineer can audit a project without opening the app.

## Multi-window

Single project per window; multiple windows allowed. Useful for comparing two pipelines side by side, or for a teaching context where the instructor and student each want their own view of the same project (in which case both windows share the same backend instance).

## Performance budgets

Hard targets that PRs must not regress:

| Operation                                          | Budget          |
| -------------------------------------------------- | --------------- |
| Cold app launch to interactive                     | < 1.5 s         |
| Open a 1M-cell mesh in the viewport                | < 2.0 s         |
| First chat token after submit (BYOK direct)        | < 800 ms        |
| Pipeline stage cache hit (no recomputation)        | < 50 ms         |
| Frame time during interactive viewport rotate      | < 16.7 ms (60fps) on a 5M-cell mesh, M2 / Ryzen 7 |

Benchmarks live in `benchmarks/desktop/` and run on every PR.

## Accessibility

- Full keyboard navigability — every command surfaces in the command palette (`⌘K` / `Ctrl+K`).
- Screen-reader labels on every interactive element via Radix.
- Configurable contrast: the dim theme passes WCAG AA contrast ratios; an optional high-contrast theme passes AAA.
- Reduced-motion preference honoured (`prefers-reduced-motion`).
- Configurable font size; viewport text scales independently from UI text.

## First-run experience (Sprint 10 push 10)

The onboarding wizard runs once, on the first launch after a fresh
install. It is the only thing between download and a working analysis
— per `SPRINT_PLAN.md` § "Sprint 10 exit criteria." Implementation:
`src/desktop/src/onboarding/`.

### Step sequence

| # | Step          | Purpose                                                                 | Skippable |
| - | ------------- | ----------------------------------------------------------------------- | --------- |
| 1 | Welcome       | One-screen "what souxmar is"; sets expectations (BYOK, plugin model).   | No        |
| 2 | BYOK          | Pick provider (Anthropic / OpenAI / Ollama); paste key; store in OS keychain. | Yes — defers to Settings → AI providers. |
| 3 | Sample project | Copy `examples/cantilever-beam` to `~/souxmar-projects/cantilever` and open it. | Yes — user starts blank.   |
| 4 | Done          | Recap; one button into the workbench shell.                             | No        |

The wizard owns its own state (Zustand store); each step is a
small focused React component. Tauri commands at each step:

* Step 2 → `byok_store_key(provider, key)`: writes to the platform
  keychain via the `keyring` Rust crate. For Ollama, also calls
  `byok_test_connection` which does a no-cost `GET /api/tags`
  against `localhost:11434`. Anthropic / OpenAI are *not* tested
  here — a "first launch" 1-token call would bill the user.
* Step 3 → `open_sample_project(which)`: copies the example tree
  to `~/souxmar-projects/<which>` and returns the destination
  path. Missing-source case (the example wasn't bundled with this
  build) leaves a placeholder README so the workbench has
  somewhere to open and the user sees a clear explanation.
* Step 4 → `onboarding_complete()`: flips
  `settings.json::onboarding_completed = true` so the next launch
  goes straight to the workbench.

### Why this shape

* **Welcome is one screen, not three.** A bored-in-30-seconds user
  cannot be re-engaged. We say what souxmar is, what BYOK means,
  what plugins are, and get out of the way.
* **BYOK is step 2, not step 1.** A user who launches the app to
  poke around shouldn't have to make a billing decision before
  seeing what they downloaded. The skippable path is the soft
  default.
* **The sample project copies into the user's home, not a tmp
  directory.** First-run state must persist if the user quits and
  comes back; tmp directories don't.
* **No telemetry checkbox.** souxmar collects no telemetry beyond
  optional crash reports (which the user enables under Settings →
  Privacy, *after* onboarding, when they have context for what the
  trade-off is).

### Visual regression

The wizard's screens are tracked in the screenshot suite under
`tests/visual/onboarding/`. Any token change that lands in
`src/desktop/src/ui/tokens.css` re-runs the suite (per
`.claude/skills/updating-design-tokens`). Visual diffs surface in
the PR; a token bump that visibly changes the wizard's appearance
is a design-system review, not a code-review.

### What's still scaffolding

The Tauri commands at step 2 (key validation), step 3 (real
example bundle resolution), and the FFI layer that lets the
workbench call into `libsouxmar-*` are stubbed in push 10's
scaffolding. The full wiring lands progressively across Sprint 11
(dogfood week — every engineer launches this and the rough edges
get filed). The wizard's UX shape is locked here so subsequent
pushes have a concrete target.

## Distribution and updates

- Release artefacts are built in CI, signed in CI (Apple notarisation, Windows EV cert, Linux GPG), and published to a static CDN with a Tauri-format updater manifest.
- Updates are opt-in by default in the free tier; Pro defaults to "auto-update on next launch."
- Old versions remain downloadable indefinitely; we do not unilaterally retire releases.

## What this app is not

- Not a fork of FreeCAD, Blender, or Cursor. It links to none of them.
- Not a CAD modeller. Drawing is FreeCAD's, Blender's, or your CAD vendor's job; we import.
- Not a web app. There is no "souxmar.app" hosted version at v1. The Pro tier adds optional cloud services that the desktop app calls into, not a hosted alternative.
- Not a chat client wrapping an LLM. The chat is a control surface for the simulation backend; without the backend, the chat does nothing useful.
