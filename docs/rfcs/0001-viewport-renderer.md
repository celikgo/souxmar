# RFC 0001: Viewport renderer architecture

- **Author:** celikgokhun
- **Status:** Draft
- **Tracking issue:** TBD — file at Sprint 25 kickoff
- **Affects:** ABI, agent tool contract
- **Tier:** 3
- **Date opened:** 2026-05-12
- **Date `final-comment` started:** —
- **Date accepted / rejected:** —

## Summary

Wire a real 3D viewport renderer into the souxmar desktop app, replacing the SVG placeholder shipped in v1.0. The renderer is a **WebGL2-baseline / WebGPU-when-available** Three.js scene in `src/desktop/src/workbench/Viewport.tsx`, fed by a new opaque surface-stream handle on the `souxmar-bridge` Rust crate, which in turn reads from a new C ABI surface (`include/souxmar-c/surface_stream.h`) layered on top of the existing `souxmar_mesh_t`. The ABI surface is purely **additive** — minor bump from v1.3 to v1.4 under the ADR-0008 ratchet — and the bridge surface is gated behind the existing `viewport_renderer` flag in `BridgeFeatureSet` (`src/desktop/src-tauri/souxmar-bridge/src/lib.rs:103`), which flips to `true` when the new path lands.

## Motivation

The v1.0 workbench has `viewport_renderer: false` hard-coded in `souxmar_bridge::BridgeFeatureSet::default()` (`src/desktop/src-tauri/souxmar-bridge/src/lib.rs:103`). The Viewport panel renders an SVG placeholder when the flag is off, which is every shipped build today. A user opening the cantilever sample sees a stylised beam sketch rather than the tetrahedral mesh that was actually generated. Concretely:

- The Sprint 24 retro (`docs/retros/sprint-24.md`) names Sprint 28 / `v1.1.0` as the "viewport rendering — Three.js + VTK.js wired through the `viewport_renderer` flag" line item — i.e., closing this gap was already on the schedule. The post-v1.0 plan pulls it forward to S25 because every subsequent visualization sprint is blocked by it.
- The post-v1.0 block (Sprints 25–32, `docs/SPRINT_PLAN.md`) explicitly blocks five subsequent sprints on this surface existing: S26 (mesh viz features), S27 (results fields, ships `v1.1`), S30 (parametric features, ships `v1.2`), S31 (BCs from viewport), S32 (transient + animation, ships `v1.3`).
- The agent's eval suite has no test today that asserts *the user can perceive* a mesh — only that the bridge call returns a Mesh handle. A renderer closes that observability gap on the user side, not just on the test side, and unlocks the `viz.*` agent tool family planned across S27 and beyond.

Now is the right time because the v1 ABI is frozen final (`docs/adr/0008-abi-v1-final-freeze.md`), so the renderer can be designed against a stable mesh handle without churn risk; and the additive-minor ratchet exists precisely for the case where a load-bearing host-side surface needs to layer on top of frozen plugin types.

## Proposal

Three layers, top-down.

### 1. React side (`src/desktop/src/workbench/Viewport.tsx`)

A Three.js `WebGLRenderer` mounted on a canvas covering the workbench viewport cell. Single `Scene` with a `PerspectiveCamera`, an `OrbitControls`, ambient + directional light, and **one mesh per loaded project**. Geometry data comes from `useSurfaceStream(projectId)`, a new React hook in `src/desktop/src/workbench/useSurfaceStream.ts` that wraps the bridge command and exposes `{ summary, chunks, isLoading, error }`.

Render modes (full UI toggle lands in S26; the renderer must support all four from day 1 so S26 is purely UI work):

- `shaded` — `MeshStandardMaterial` with per-vertex normals.
- `shaded_edges` — shaded mesh + a `LineSegments` overlay on outline edges. Default folded-edge angle threshold: **30°** (benchmarked against the `examples/*` corpus during PR 4; bump if a sample looks wrong).
- `wireframe` — `LineSegments` only.
- `points` — `Points` only.

Picking uses Three.js's `Raycaster` against the mesh. On a hit, the renderer reads two `BufferAttribute`s carried alongside the position/normal buffers and returns `{ face_id, vertex_id }` (and a derived `edge_id` from the two adjacent face_ids) to the React layer. The attribute names on the `BufferGeometry` are locked:

| Attribute   | Type         | Source field on `SurfaceChunk` |
| ----------- | ------------ | ------------------------------ |
| `position`  | `Float32×3`  | `positions`                    |
| `normal`    | `Float32×3`  | `normals`                      |
| `aFaceId`   | `Uint32×1`   | `face_ids` (per triangle, broadcast to its three vertices on attribute upload) |
| `aVertexId` | `Uint32×2`   | `vertex_ids` split into two uint32s (low / high) since `Uint64Array` is not a BufferAttribute type |

The `aVertexId` packing detail is renderer-side only — the bridge contract still carries `u64` (see below).

### 2. Bridge side (`src/desktop/src-tauri/souxmar-bridge`)

Two new commands, additive to the existing `pipeline_summary` / `chat_send` set. Both gated on `feature_set().viewport_renderer == true`; calling them while the flag is `false` returns `BridgeError::FeatureDisabled` (existing variant; see `lib.rs`).

```rust
// New file: src-tauri/souxmar-bridge/src/surface_stream.rs (sketch)

pub struct SurfaceStreamSummary {
    pub project_id:     String,
    pub vertex_count:   u64,
    pub triangle_count: u64,
    pub bounds_min:     [f64; 3],
    pub bounds_max:     [f64; 3],
    pub chunk_count:    u32,        // pre-computed; React drives the loop
    pub stream_token:   String,     // opaque; React passes back on chunk fetches
}

pub struct SurfaceChunk {
    pub stream_token:   String,
    pub chunk_index:    u32,
    // SoA layout to avoid a JS-side decode pass:
    pub positions:      Vec<f32>,   // 3 * chunk_vertex_count
    pub normals:        Vec<f32>,   // 3 * chunk_vertex_count
    pub indices:        Vec<u32>,   // 3 * chunk_triangle_count
    pub face_ids:       Vec<u32>,   // chunk_triangle_count
    pub vertex_ids:     Vec<u64>,   // chunk_vertex_count
}

impl Bridge {
    pub fn surface_stream_open(&self, project_id: &str)
        -> Result<SurfaceStreamSummary, BridgeError>;

    pub fn surface_stream_chunk(&self, token: &str, chunk_index: u32)
        -> Result<SurfaceChunk, BridgeError>;
}
```

Wire format: the existing Tauri `serde_json` invocation path. Chunk size is the dominant open question (see Open Questions §1); the decision rubric is "keep the cantilever 250k-tri load under 200ms p95 on the integrated-GPU CI runner." For meshes large enough that JSON encoding stalls the UI, we shift to **shared mmap via a sidecar file**; that path is a Sprint 25.5 slip-lane PR, not a precondition for the flag flip on the 250k-tri target.

The `chunk_count` field on the summary is precomputed by the host so the React side knows the loop bound up front; this eliminates a "did the stream end?" sentinel chunk.

### 3. C ABI (`include/souxmar-c/surface_stream.h`, new)

The ABI surface is **additive**: no existing header changes. We add one new header at `include/souxmar-c/surface_stream.h`. Following the convention in the existing host-side headers (`mesh.h`, etc.), function prototypes are bare — there is no `SOUXMAR_API` decoration; only plugin entry points use `SOUXMAR_PLUGIN_EXPORT` from `abi.h`, and surface-stream is host-implemented.

```c
/* SPDX-License-Identifier: Apache-2.0
 *
 * include/souxmar-c/surface_stream.h — additive, v1.4 of the C ABI.
 *
 * Layers a renderer-friendly surface view on top of an existing
 * souxmar_mesh_t. The view is *derived*: nothing here mutates the
 * underlying mesh; the host computes face normals + an outer-shell
 * extraction on first open and caches the result keyed by mesh handle.
 *
 * Threading: not thread-safe; the renderer thread is expected to
 * marshal calls to the host worker via the existing dispatcher.
 */

#ifndef SOUXMAR_C_SURFACE_STREAM_H
#define SOUXMAR_C_SURFACE_STREAM_H

#include "abi.h"
#include "mesh.h"
#include "status.h"

SOUXMAR_C_BEGIN

typedef struct souxmar_surface_stream_t souxmar_surface_stream_t;

/* Open a derived surface view of `mesh`. Returns NULL on allocation
 * failure. The returned handle holds an internal reference to `mesh`;
 * freeing the mesh while the stream is open is undefined behavior.
 * Close with souxmar_surface_stream_close. */
souxmar_surface_stream_t* souxmar_surface_stream_open(const souxmar_mesh_t* mesh);

void souxmar_surface_stream_close(souxmar_surface_stream_t* stream);

size_t souxmar_surface_stream_vertex_count(const souxmar_surface_stream_t* s);
size_t souxmar_surface_stream_triangle_count(const souxmar_surface_stream_t* s);

souxmar_status_t souxmar_surface_stream_bounds(const souxmar_surface_stream_t* s,
                                                double out_min[3],
                                                double out_max[3]);

/* SoA reads. Out buffers are sized by the count functions above; passing
 * an undersized capacity returns SOUXMAR_E_INVALID_ARGUMENT. */
souxmar_status_t souxmar_surface_stream_positions(const souxmar_surface_stream_t* s,
                                                   float* out, size_t out_capacity);
souxmar_status_t souxmar_surface_stream_normals(const souxmar_surface_stream_t* s,
                                                 float* out, size_t out_capacity);
souxmar_status_t souxmar_surface_stream_indices(const souxmar_surface_stream_t* s,
                                                 uint32_t* out, size_t out_capacity);
souxmar_status_t souxmar_surface_stream_face_ids(const souxmar_surface_stream_t* s,
                                                  uint32_t* out, size_t out_capacity);
souxmar_status_t souxmar_surface_stream_vertex_ids(const souxmar_surface_stream_t* s,
                                                    uint64_t* out, size_t out_capacity);

SOUXMAR_C_END
#endif /* SOUXMAR_C_SURFACE_STREAM_H */
```

**ABI version bump.** `SOUXMAR_ABI_VERSION_MINOR` in `abi.h` advances from **3** to **4** with a new history line:

```
 *   v1.4  Sprint 25 push 1 — surface-stream renderer surface
 *                            (souxmar-c/surface_stream.h); ADR-0037.
```

Existing plugins compiled against v1.0–v1.3 link and load unchanged because nothing in this header is required of plugins; the surface-stream entry points are host-side only. Per the abi.h ratchet rules (and `docs/adr/0008-abi-v1-final-freeze.md`), the minor bump requires an accompanying ADR (ADR-0037, listed in the implementation plan below) and the `Ratchet: additive minor surface (ADR-0008)` commit marker on PR 1.

### Agent tool surface

Three additive tool entries in `libsouxmar-ai`. Full JSON Schemas are produced as part of PR 4; the contract names and confirmation policy are part of this RFC and frozen here:

| Tool                   | Confirmation | Side effect              |
| ---------------------- | ------------ | ------------------------ |
| `viz.frame_project`    | `auto`       | Camera reset; read-only. |
| `viz.set_render_mode`  | `auto`       | UI state only.           |
| `viz.pick`             | `auto`       | UI selection state only. |

All three are `auto` per the policy in `docs/AI_INTEGRATION.md` because none of them touches disk or talks to a provider. Adding tools post-v1 is permitted under the v1 agent-tool contract (verified by the `reviewing-abi-changes` skill); only **removing or changing existing tools** breaks the contract.

## Alternatives considered

### Alternative A: VTK.js native viewer in an `<iframe>`

Embed VTK.js's `WebViewer` component and feed it the on-disk `cantilever.vtu` directly, no FFI streaming. VTK.js already implements the readers we need.

Rejected because: (a) it forces a write-to-disk roundtrip for every render — the in-memory `souxmar_mesh_t` would have to be serialised to a temp VTU before VTK.js could read it, which doubles the memory peak on large meshes; (b) VTK.js's bundle size adds ~3.2 MB minified to the app, vs. ~600 KB for the Three.js subset we actually use; (c) picking IDs in VTK.js round-trip through its own scene-graph IDs, which gives us a second ID system to keep in sync with the souxmar IDs that the agent's selection tools already use. The bundle-size and ID-system arguments dominate. The readers argument flips the other way for Sprint 27 (results fields), and we may pull VTK.js in *just* for the reader layer at that point — a decision for RFC-002, not this one.

### Alternative B: WebGPU-only renderer, drop WebGL2

WebGPU has materially better perf headroom for the 1M-triangle target. Could ship WebGPU-only and require a recent Chrome/Edge/Safari.

Rejected because: WebGPU on Linux + Tauri's wry webview is not stable enough as of 2026-Q2 (Mesa driver coverage for the `BC*` formats we'd want is uneven; Tauri 2's macOS WKWebView path supports WebGPU only on macOS 14+). We'd silently exclude a chunk of users on Linux and pre-14 macOS. WebGL2 baseline + WebGPU upgrade is the cheap path; the renderer interface inside Three.js abstracts both.

### Alternative C: Render server-side, stream pixels (Pixel Streaming / WebRTC)

Run the renderer on a CPU/GPU in the host process (e.g., VTK with OSMesa), stream H.264 to a `<video>` tag in the React side.

Rejected because: latency budget on a 60fps interaction is 16ms; encode + transport + decode on a local socket eats 8–12ms even with hardware H.264. Picking would need a separate IPC round-trip. Burns the determinism story (each OS's encoder picks different macroblocks). And it's just more moving parts than rendering directly in the webview, for no win on the local-only desktop path. This pattern *does* become interesting for the future managed-compute offload (`docs/adr/0027-compute-offload.md`) — flag the architecture there, do not invest here.

### (Considered and rejected: do nothing)

Shipping v1.x with the SVG placeholder is not viable; it blocks five subsequent sprints (S26 mesh viz, S27 fields, S30 modeling, S31 BCs, S32 transient all assume a working viewport) and three planned releases (`v1.1`, `v1.2`, `v1.3`).

## Drawbacks

- **Bundle size:** adding Three.js + its loaders adds ~600 KB minified + brotli to the desktop bundle. Tracked against the perf budget in `docs/ENGINEERING_PRACTICES.md`; acceptable but a real number.
- **Maintenance surface:** Three.js's major versions are not strictly semver-safe; we will pin to a specific minor and bump deliberately. The exact r-release is picked during PR 3 against the highest stable release available at Sprint 25 day 1, and ratified in ADR-0043 (see Implementation plan).
- **Determinism story:** the renderer introduces a screenshot-level golden test (`viz-golden`) that is **not byte-exact** across OSes; only numeric solver output stays under the exact-byte determinism gate (R-015 in the post-v1.0 risk register, `docs/SPRINT_PLAN.md`).
- **New ABI surface to support forever:** `surface_stream.h` joins the frozen v1 ABI as a v1.4 add. Once shipped, the function signatures cannot change without an RFC + a major bump.
- **WebGPU drift:** WebGPU adoption rate may overtake our WebGL2 baseline within 18 months, at which point we carry two render paths longer than we'd like. Mitigation: a yearly "drop WebGL2?" review starting 2027-Q2.

## Migration plan

- **Existing in-tree plugins:** unaffected. The new ABI is additive and host-side.
- **Out-of-tree plugins:** unaffected. They do not call `souxmar_surface_stream_*`.
- **Existing pipeline files on disk:** unaffected. The renderer reads from the in-memory `souxmar_mesh_t` produced by the existing pipeline, not a new file format.
- **Saved chat sessions / agent histories:** the three new `viz.*` tools are additive; saved sessions referencing only v1.0 tools replay identically.
- **ABI minor consumers:** plugin authors who want to advertise that they were built against v1.4 set their `abi_version_minor` floor accordingly; the host conformance check C004 already covers the version-skew matrix.
- **Documentation that references the old behaviour:** update `docs/DESKTOP_APP.md` ("Viewport" section), `docs/UI_DESIGN.md` (workbench layout), and remove the SVG-placeholder caveat in `src/desktop/src/workbench/Viewport.tsx`'s comment block. Tutorials currently saying "the viewport is a placeholder" become outdated; rewrite list compiled during PR 3 review.

No shim layer is needed — this is purely additive surface.

## Pre-mortem

It is 2027-05-12. RFC-001 has gone badly. What happened:

We shipped the renderer on schedule, but the 1M-triangle perf target was missed by ~3× on integrated GPUs. Worse, the Three.js pin we chose hit a regression in `OrbitControls` momentum handling that surfaced only on Linux Wayland; we patched around it in our fork, then forgot, then bumped Three and the patch silently no-op'd. Users on a single popular Linux distro experienced a stuttering camera for three months before someone connected the dots. The JSON-over-IPC chunk encoding turned out to be the dominant cost on large meshes — bigger than the render itself — and the "shared mmap follow-up" we deferred from S25.5 never landed because the team got pulled onto the modeling block.

Leading indicators to watch in the first six months:

- `viz-golden` frame-time regressions on the integrated-GPU CI runner exceed budget more than once.
- Three.js version bump PRs that don't include an explicit "verified Wayland camera behaviour" checkbox.
- The "shared mmap follow-up" item slips past its original target sprint by more than one sprint.
- Picking latency p95 on the cantilever exceeds 32ms in any release build.

## Open questions

The implementation team needs to close these before the merge of PR 2 (chunk encoding) or PR 3 (Three.js pin). Items already resolved by this RFC are not listed here.

1. **Chunk encoding.** Plain JSON arrays vs. base64'd `ArrayBuffer` vs. a Tauri-native binary IPC path (`tauri::ipc::Response::new(Vec<u8>)`). Decision rubric: pick the option that keeps the cantilever 250k-tri load under 200ms p95 on the integrated-GPU CI runner. **Closes in PR 2.**
2. **Three.js version pin.** Pick the r-release on Sprint 25 day 1; ratify in ADR-0043. **Closes in PR 3.**
3. **WebGPU feature detection UX.** Auto-upgrade silently, or surface a "GPU acceleration: WebGPU / WebGL2" indicator in the settings panel? Lean toward surfacing — debuggability beats magic — but this is a UX call. **Closes in PR 3 review.**
4. **Wayland-specific camera test.** What's the cheapest way to keep one in CI? (No Linux Wayland runner exists today.) For S25, the gate is a manual checkbox on the release-candidate checklist; full CI coverage deferred to a follow-up ADR after Sprint 26.

### Resolved by this RFC (recorded so reviewers don't re-litigate)

- **Vertex ID stability across re-meshes.** Not promised in v1.4. The `vertex_ids` array carries the IDs that exist for the *current* mesh; consumers must re-fetch after a regeneration. Stability becomes a real concern only when BCs reference vertices in S31 — at that point, a separate "stable selection IDs" RFC introduces the guarantee, with its own ABI surface.
- **Edge extraction angle threshold for `shaded_edges`.** Default 30°. Benchmark across `examples/*` during PR 4; if a sample looks wrong, bump and re-snapshot `viz-golden`.
- **Render-mode coverage from day 1.** All four modes (`shaded`, `shaded_edges`, `wireframe`, `points`) ship in the S25 renderer even though the UI toggle lands in S26. This avoids a "renderer refactor for an extra mode" pattern.
- **`SOUXMAR_API` decoration.** Does not exist in the ABI today; the new header uses bare declarations, matching the existing `mesh.h` convention.

## Implementation plan

Five PRs, sized to fit in Sprint 25 (with a slip lane into S25.5 for the shared-mmap follow-up). Owners assigned at sprint-planning kickoff.

- [ ] **PR 1 — ABI add.** New header `include/souxmar-c/surface_stream.h`; in-core implementation backing the entry points; `SOUXMAR_ABI_VERSION_MINOR` bump 3 → 4 with the new history line; conformance-suite additions covering `souxmar_surface_stream_*` round-trips. Commit message carries the `Ratchet: additive minor surface (ADR-0008)` marker. Reviewer load: ABI gate (`reviewing-abi-changes` skill).
- [ ] **PR 2 — Bridge surface.** `souxmar-bridge` `surface_stream_open` + `surface_stream_chunk` Rust commands; Tauri command registration in `src/desktop/src-tauri/src/commands.rs`; flip `viewport_renderer` to `true` when the `real-ffi` feature is on; unit tests against the bridge skeleton path. **Closes Open Question 1 (chunk encoding).**
- [ ] **PR 3 — Three.js scene + hook.** `useSurfaceStream` hook; Three.js scene mount; orbit controls; resize + HiDPI; perf budget gate in CI (`viz-golden`). No render-mode toggle UI yet. **Closes Open Questions 2 and 3 (Three.js pin + WebGPU UX); ADR-0043 filed for the pin.**
- [ ] **PR 4 — Render modes + picking.** All four render modes wired into the canvas (UI toggle lands in S26); raycaster picking; `viz.*` agent tools wired through the dispatcher with eval cases in `tests/agent-eval/`.
- [ ] **PR 5 (S25.5 slip lane) — Shared mmap path.** Falls in if PR 2 misses the 200ms p95 target on the 1M-tri stress test.
- [ ] ADR-0037 filed at `docs/adr/0037-abi-v1-4-surface-stream-ratchet.md` — records the v1.4 minor bump under the ADR-0008 ratchet. Filed with PR 1.
- [ ] ADR-0043 filed at `docs/adr/0043-three-js-pin.md` — records the Three.js r-release pin chosen in PR 3.
- [ ] Documentation update in `docs/DESKTOP_APP.md` (Viewport section rewrite) and `docs/AI_INTEGRATION.md` (three new `viz.*` tools). Filed alongside PR 4.
- [ ] Tutorial update in `docs/tutorials/` ("Viewing a mesh") — slips to S26 if S25 is tight; S26 lists this as a Desktop story already.

## References

- `docs/SPRINT_PLAN.md` — Post-v1.0 plan, Sprint 25 row and R-011/R-015 risk register.
- `docs/ROADMAP.md` — Post-1.0 themes, "GPU back-end" mention (separate concern; the renderer is not the solver).
- `docs/AI_INTEGRATION.md` — Agent tool contract; confirmation policy.
- `docs/ENGINEERING_PRACTICES.md` — Perf budget format.
- `docs/GOVERNANCE.md` — RFC process; Tier definitions.
- `docs/adr/0008-abi-v1-final-freeze.md` — The freeze + ratchet rules this RFC operates under.
- `docs/adr/0016-bridge-feature-set-contract.md` — `BridgeFeatureSet` contract that this RFC flips a flag in.
- `docs/adr/0027-compute-offload.md` — Where Alternative C (server-side render) gets re-evaluated for the managed-compute path.
- `include/souxmar-c/abi.h` — Version macros + ratchet history this RFC extends.
- `include/souxmar-c/mesh.h` — The existing handle this RFC layers on top of.
- `src/desktop/src-tauri/souxmar-bridge/src/lib.rs` — `BridgeFeatureSet` and the existing `viewport_renderer` flag (line 103).
