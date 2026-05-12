# RFC 0001: Viewport renderer architecture

- **Author:** TBD (Desktop team lead, ratified by Platform)
- **Status:** Draft
- **Tracking issue:** TBD
- **Affects:** ABI, agent tool contract
- **Tier:** 3
- **Date opened:** 2026-05-12
- **Date `final-comment` started:** —
- **Date accepted / rejected:** —

> **Stub.** This RFC is intentionally incomplete at the proposal level: it nails down the boundaries (what's frozen, what's a plugin, what's a one-way door) and enumerates the open questions Sprint 25's first week needs to resolve before the implementation PRs can land. Treat the `## Proposal` section as a contract on **shape**, not byte layout — every `TBD` is a deliberate handoff to the implementation team.

## Summary

Wire a real 3D viewport renderer into the souxmar desktop app, replacing the SVG placeholder shipped in v1.0. The renderer is a **WebGL2-baseline / WebGPU-when-available** Three.js scene in `src/desktop/src/workbench/Viewport.tsx`, fed by a **new opaque surface-stream handle** on the `souxmar-bridge` Rust crate, which in turn reads from a new C ABI surface (`souxmar-c/surface_stream.h`) layered on top of the existing `souxmar_mesh_t`. The ABI surface is additive (no v1 break); the bridge surface is gated behind the `viewport_renderer` feature flag that already exists in `BridgeFeatureSet` and flips to `true` when the new path lands.

## Motivation

The v1.0 workbench has a `viewport_renderer: false` hard-coded in `souxmar_bridge::BridgeFeatureSet::default()` (`src/desktop/src-tauri/souxmar-bridge/src/lib.rs:103`). The Viewport panel renders an SVG placeholder when the flag is off, which is every shipped build. Users opening the cantilever sample see a stylised beam sketch rather than the tetrahedral mesh that was actually generated. Concretely:

- The Sprint 24 dogfood retro logged six independent complaints of the form "I can't see what I made." (Reference: `docs/retros/sprint-24.md` — TBD link.)
- Every external launch-week bug report under the `desktop:viewport` label reduces to the same root cause.
- The agent's eval suite has no test that asserts *the user can perceive* a mesh — only that the bridge call returns a Mesh handle. A renderer closes that observability gap on the user side, not just on the test side.

Now is the right time because the v1 ABI is frozen, so the renderer can be designed against a stable mesh handle without churn risk; and the post-v1.0 block (Sprints 25–32, `docs/SPRINT_PLAN.md`) blocks five subsequent sprints on this surface existing.

## Proposal

Three layers, top-down:

### 1. React side (`src/desktop/src/workbench/Viewport.tsx`)

A Three.js `WebGLRenderer` mounted on a canvas covering the top-left workbench cell. Single `Scene` with a `PerspectiveCamera`, an `OrbitControls`, ambient + directional light, and **one mesh per loaded project**. Geometry data comes from `useSurfaceStream(projectId)`, a new React hook in `src/desktop/src/workbench/useSurfaceStream.ts` that wraps the bridge command.

Render modes (already on the S26 roadmap; the renderer must support the toggle from day 1 even if the UI lands later):

- `shaded` — `MeshStandardMaterial` with vertex normals.
- `shaded_edges` — shaded mesh + a `LineSegments` overlay on the outline edges (folded-edge angle threshold TBD; default 30°).
- `wireframe` — `LineSegments` only.
- `points` — `Points` only.

Picking uses Three.js's `Raycaster` against the mesh, and on a hit returns the `face_id` / `edge_id` / `vertex_id` packed into a 32-bit attribute stored alongside the geometry. (Per-vertex / per-face attribute names: **TBD** — but they're part of the bridge contract below, not just a React detail.)

### 2. Bridge side (`src/desktop/src-tauri/souxmar-bridge`)

Two new commands, additive to the existing `pipeline_summary` / `chat_send` set. Both gated on `feature_set().viewport_renderer == true`.

```rust
// New file: src-tauri/souxmar-bridge/src/surface_stream.rs (sketch)

pub struct SurfaceStreamSummary {
    pub project_id:    String,
    pub vertex_count:  u64,
    pub triangle_count: u64,
    pub bounds_min:    [f64; 3],
    pub bounds_max:    [f64; 3],
    pub stream_token:  String,   // opaque; React passes back on subsequent chunk fetches
}

pub struct SurfaceChunk {
    pub stream_token:  String,
    pub chunk_index:   u32,
    pub chunk_count:   u32,
    // SoA layout to avoid a JS-side decode pass:
    pub positions:     Vec<f32>,   // 3 * vertex_count
    pub normals:       Vec<f32>,   // 3 * vertex_count
    pub indices:       Vec<u32>,   // 3 * triangle_count
    pub face_ids:      Vec<u32>,   // triangle_count
    pub vertex_ids:    Vec<u64>,   // vertex_count (stable across re-meshes; TBD)
}

impl Bridge {
    pub fn surface_stream_open(&self, project_id: &str)
        -> Result<SurfaceStreamSummary, BridgeError>;

    pub fn surface_stream_chunk(&self, token: &str, chunk_index: u32)
        -> Result<SurfaceChunk, BridgeError>;
}
```

Wire format: the existing Tauri `serde_json` invocation path. Chunk size is **TBD** but bounded so a single chunk fits the v8 string size limit comfortably (target ≤ 32 MB JSON ≈ 1 M vertices per chunk after base64 of f32 buffers — actual encoding **TBD**, see Open Questions). For meshes large enough that JSON encoding stalls the UI, we shift to **shared mmap via a sidecar file**; that path is a Sprint 25.5 follow-up, not a precondition for the flag flip.

### 3. C ABI (`include/souxmar-c/surface_stream.h`, new)

The ABI surface is **additive**: no existing header changes. We add one new header at `include/souxmar-c/surface_stream.h`:

```c
/* include/souxmar-c/surface_stream.h — additive, v1.1 of the C ABI.
 *
 * Layers a renderer-friendly surface view on top of an existing
 * souxmar_mesh_t. The view is *derived*: nothing here mutates the
 * underlying mesh; the host computes face normals + an outer-shell
 * extraction on first call and caches the result keyed by mesh handle.
 *
 * Threading: not thread-safe; the renderer thread is expected to
 * marshal calls to the host worker via the existing dispatcher.
 */

#ifndef SOUXMAR_C_SURFACE_STREAM_H
#define SOUXMAR_C_SURFACE_STREAM_H

#include "abi.h"
#include "mesh.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct souxmar_surface_stream_t souxmar_surface_stream_t;

/* Open a derived surface view of `mesh`. NULL on allocation failure.
 * The returned handle is independent of `mesh` after open — freeing
 * the mesh later is safe but invalidates further chunk reads. */
SOUXMAR_API
souxmar_surface_stream_t* souxmar_surface_stream_open(const souxmar_mesh_t* mesh);

SOUXMAR_API
void souxmar_surface_stream_close(souxmar_surface_stream_t* stream);

SOUXMAR_API
size_t souxmar_surface_stream_vertex_count(const souxmar_surface_stream_t* s);

SOUXMAR_API
size_t souxmar_surface_stream_triangle_count(const souxmar_surface_stream_t* s);

SOUXMAR_API
souxmar_status_t souxmar_surface_stream_bounds(const souxmar_surface_stream_t* s,
                                                double out_min[3],
                                                double out_max[3]);

/* SoA reads. Out buffers sized by the count functions above. */
SOUXMAR_API
souxmar_status_t souxmar_surface_stream_positions(const souxmar_surface_stream_t* s,
                                                   float* out, size_t out_capacity);
SOUXMAR_API
souxmar_status_t souxmar_surface_stream_normals(const souxmar_surface_stream_t* s,
                                                 float* out, size_t out_capacity);
SOUXMAR_API
souxmar_status_t souxmar_surface_stream_indices(const souxmar_surface_stream_t* s,
                                                 uint32_t* out, size_t out_capacity);
SOUXMAR_API
souxmar_status_t souxmar_surface_stream_face_ids(const souxmar_surface_stream_t* s,
                                                  uint32_t* out, size_t out_capacity);
SOUXMAR_API
souxmar_status_t souxmar_surface_stream_vertex_ids(const souxmar_surface_stream_t* s,
                                                    uint64_t* out, size_t out_capacity);

#ifdef __cplusplus
}
#endif
#endif /* SOUXMAR_C_SURFACE_STREAM_H */
```

`SOUXMAR_C_API_VERSION` in `abi.h` bumps to `(1, 1, 0)` — additive minor. Existing plugins compiled against `(1, 0, x)` link and load unchanged because nothing in this header is required of plugins; the surface-stream entry points are host-side.

### Agent tool surface

Three additive tool entries in `libsouxmar-ai`, with their JSON Schemas (full schemas: **TBD** during implementation, but the contract names + confirmation policy are part of this RFC):

| Tool                           | Confirmation | Side effect              |
| ------------------------------ | ------------ | ------------------------ |
| `viz.frame_project`            | `auto`       | Camera reset; read-only. |
| `viz.set_render_mode`          | `auto`       | UI state only.           |
| `viz.pick`                     | `auto`       | UI selection state only. |

All three are `auto` per the policy in `docs/AI_INTEGRATION.md` because none of them touches disk or talks to a provider. Adding these tools post-v1 is permitted because adding tools is non-breaking under the v1 agent tool contract (verified in `reviewing-abi-changes` skill).

## Alternatives considered

### Alternative A: VTK.js native viewer in an `<iframe>`

Embed VTK.js's `WebViewer` component and feed it the on-disk `cantilever.vtu` directly, no FFI streaming. VTK.js already implements the readers we need.

Rejected because: (a) it forces a write-to-disk roundtrip for every render — the in-memory `souxmar_mesh_t` would have to be serialised to a temp VTU before VTK.js could read it, which doubles the memory peak on large meshes; (b) VTK.js's bundle size adds ~3.2 MB minified to the app, vs. ~600 KB for the Three.js subset we actually use; (c) picking IDs in VTK.js round-trip through its own scene-graph IDs, which gives us a second ID system to keep in sync with the souxmar IDs that the agent's selection tools already use. The bundle-size and ID-system arguments dominate; the readers argument flips the other way for Sprint 27 (results fields), and we may pull VTK.js in *just* for the reader layer at that point — a decision for RFC-002, not this one.

### Alternative B: WebGPU-only renderer, drop WebGL2

WebGPU has materially better perf headroom for the 1M-triangle target. Could ship WebGPU-only and require a recent Chrome/Edge/Safari.

Rejected because: WebGPU on Linux + Tauri's wry webview is not stable enough as of 2026-Q2 (Mesa driver coverage for the `BC*` formats we'd want is uneven; Tauri 2's macOS WKWebView path supports WebGPU only on macOS 14+). We'd silently exclude a chunk of users on Linux and pre-14 macOS. WebGL2 baseline + WebGPU upgrade is the cheap path; the renderer interface inside Three.js abstracts both.

### Alternative C: Render server-side, stream pixels (Pixel Streaming / WebRTC)

Run the renderer on a CPU/GPU in the host process (e.g., VTK with OSMesa), stream H.264 to a `<video>` tag in the React side.

Rejected because: latency budget on a 60fps interaction is 16ms; encode + transport + decode on a local socket eats 8–12ms even with hardware H.264. Picking would need a separate IPC round-trip. Burns the determinism story (each OS's encoder picks different macroblocks). And it's just more moving parts than rendering directly in the webview, for no win on the local-only desktop path. This pattern *does* become interesting for the future managed-compute offload (`docs/adr/0027-compute-offload.md`) — flag the architecture there, do not invest here.

### (Considered and rejected: do nothing)

Shipping v1.x with the SVG placeholder is not viable; it's the top external complaint and blocks five subsequent sprints (S26 mesh viz, S27 fields, S30 modeling, S31 BCs, S32 transient all assume a working viewport).

## Drawbacks

- **Bundle size:** adding Three.js + its loaders adds ~600 KB minified + brotli to the desktop bundle. Tracked against the perf budget in `docs/ENGINEERING_PRACTICES.md`; acceptable but a real number.
- **Maintenance surface:** Three.js's major versions are not strictly semver-safe; we will pin to a specific minor and bump deliberately. Pin candidate: r170 (TBD; pick the highest stable r-release at Sprint 25 day 1).
- **Determinism story:** the renderer introduces a screenshot-level golden test (`viz-golden`) that is **not byte-exact** across OSes; only numeric solver output stays under the exact-byte determinism gate (R-015 in the new post-v1.0 risk register).
- **New ABI surface to support forever:** `surface_stream.h` joins the frozen v1 ABI as a v1.1 add. Once shipped, the function signatures cannot change without an RFC + a major bump.
- **WebGPU drift:** WebGPU adoption rate may overtake our WebGL2 baseline within 18 months, at which point we carry two render paths longer than we'd like. Mitigation: a yearly "drop WebGL2?" review starting 2027-Q2.

## Migration plan

- **Existing in-tree plugins:** unaffected. The new ABI is additive and host-side.
- **Out-of-tree plugins:** unaffected. They do not call `souxmar_surface_stream_*`.
- **Existing pipeline files on disk:** unaffected. The renderer reads from the in-memory `souxmar_mesh_t` produced by the existing pipeline, not a new file format.
- **Saved chat sessions / agent histories:** the three new `viz.*` tools are additive; saved sessions referencing only v1.0 tools replay identically.
- **Documentation that references the old behaviour:** update `docs/DESKTOP_APP.md` ("Viewport" section), `docs/UI_DESIGN.md` (workbench layout), and remove the SVG-placeholder caveat in `src/desktop/src/workbench/Viewport.tsx`'s comment block. Tutorials currently saying "the viewport is a placeholder" become outdated; rewrite list **TBD** during the implementation PR series.

No shim layer is needed — this is purely additive surface.

## Pre-mortem

It is 2027-05-12. RFC-001 has gone badly. What happened:

We shipped the renderer on schedule, but the 1M-triangle perf target was missed by ~3× on integrated GPUs. Worse, the Three.js pin we chose (r170) hit a regression in `OrbitControls` momentum handling that surfaced only on Linux Wayland; we patched around it in our fork, then forgot, then bumped Three and the patch silently no-op'd. Users on a single popular Linux distro experienced a stuttering camera for three months before someone connected the dots. The JSON-over-IPC chunk encoding turned out to be the dominant cost on large meshes — bigger than the render itself — and the "shared mmap follow-up" we deferred from S25.5 never landed because the team got pulled onto the modeling block.

Leading indicators to watch in the first six months:

- `viz-golden` frame-time regressions on the integrated-GPU CI runner exceed budget more than once.
- Three.js version bump PRs that don't include an explicit "verified Wayland camera behaviour" checkbox.
- The "shared mmap follow-up" item slips past its original target sprint by more than one sprint.
- Picking latency p95 on the cantilever exceeds 32ms in any release build.

## Open questions

The implementation team needs to close these before the merge of PR 2 below. Listed as **TBD** in `## Proposal` above:

1. **Chunk encoding.** Plain JSON arrays vs. base64'd `ArrayBuffer` vs. a Tauri-native binary IPC path (`tauri::ipc::Response::new(Vec<u8>)`). Decision rubric: pick the option that keeps the cantilever 250k-tri load under 200ms p95 on the integrated-GPU CI runner.
2. **Vertex ID stability across re-meshes.** Do we promise that `vertex_id` for a given geometric location stays stable when the mesh is regenerated? Probably no, for now (no point until BCs reference vertices, which is S31). Document the non-promise.
3. **Edge extraction angle threshold for `shaded_edges`.** Default 30° feels right; benchmark across `examples/*` before locking.
4. **Three.js version pin.** Pick the r-release on Sprint 25 day 1; ratify in a one-paragraph ADR.
5. **WebGPU feature detection.** Auto-upgrade silently, or surface a "GPU acceleration: WebGPU/WebGL2" indicator in the settings panel? Lean toward surfacing — debuggability beats magic.
6. **Wayland-specific camera test.** What's the cheapest way to keep one in CI? (No Linux Wayland runner exists today.) Possibly a manual gate on the release-candidate checklist for now; deferred to a follow-up ADR.

## Implementation plan

Five PRs, sized to fit in Sprint 25 (with a slip lane into 25.5 for the shared-mmap follow-up). Owner column **TBD** at sprint-planning kickoff.

- [ ] **PR 1 — ABI add.** New header `include/souxmar-c/surface_stream.h`; in-core implementation backing the entry points; `SOUXMAR_C_API_VERSION` bump to `(1, 1, 0)`; conformance test additions. Reviewer load: ABI gate (`reviewing-abi-changes` skill).
- [ ] **PR 2 — Bridge surface.** `souxmar-bridge` `surface_stream_open` + `surface_stream_chunk` Rust commands; Tauri command registration in `src/desktop/src-tauri/src/commands.rs`; flip `viewport_renderer` to `true` when `real-ffi` is on; unit tests against the bridge skeleton path. Closes open question (1).
- [ ] **PR 3 — Three.js scene + hook.** `useSurfaceStream` hook; Three.js scene mount; orbit controls; resize + HiDPI; perf budget gate in CI (`viz-golden`). No render-mode toggle yet.
- [ ] **PR 4 — Render modes + picking.** Four render modes; raycaster picking; `viz.*` agent tools wired through the dispatcher with eval cases.
- [ ] **PR 5 (S25.5 slip lane) — Shared mmap path.** Falls in if PR 2 misses the 200ms p95 target on the 1M-tri stress test.
- [ ] Documentation update in `docs/DESKTOP_APP.md` (Viewport section rewrite) and `docs/AI_INTEGRATION.md` (three new tools).
- [ ] ADR filed at `docs/adr/NNNN-three-js-pin.md` (Open question 4).
- [ ] Tutorial update in `docs/tutorials/` ("Viewing a mesh") — slips to S26 if S25 is tight.

## References

- `docs/SPRINT_PLAN.md` — Post-v1.0 plan, Sprint 25 row.
- `docs/ROADMAP.md` — Post-1.0 themes, "GPU back-end" mention (separate concern; the renderer is not the solver).
- `docs/AI_INTEGRATION.md` — Agent tool contract; confirmation policy.
- `docs/ENGINEERING_PRACTICES.md` — Perf budget format.
- `docs/adr/0027-compute-offload.md` — Where Alternative C (server-side render) gets re-evaluated for the managed-compute path.
- `include/souxmar-c/mesh.h` — The existing handle this RFC layers on top of.
- `src/desktop/src-tauri/souxmar-bridge/src/lib.rs` — `BridgeFeatureSet` and the existing flag.
- Three.js r170 release notes — TBD link at Sprint 25 day 1.
