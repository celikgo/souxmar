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

## Distribution and updates

- Release artefacts are built in CI, signed in CI (Apple notarisation, Windows EV cert, Linux GPG), and published to a static CDN with a Tauri-format updater manifest.
- Updates are opt-in by default in the free tier; Pro defaults to "auto-update on next launch."
- Old versions remain downloadable indefinitely; we do not unilaterally retire releases.

## What this app is not

- Not a fork of FreeCAD, Blender, or Cursor. It links to none of them.
- Not a CAD modeller. Drawing is FreeCAD's, Blender's, or your CAD vendor's job; we import.
- Not a web app. There is no "souxmar.app" hosted version at v1. The Pro tier adds optional cloud services that the desktop app calls into, not a hosted alternative.
- Not a chat client wrapping an LLM. The chat is a control surface for the simulation backend; without the backend, the chat does nothing useful.
