# RFC 0006: Time-series stream protocol + animation export

- **Author:** celikgokhun
- **Status:** Draft
- **Tracking issue:** TBD — file at Sprint 32 kickoff
- **Affects:** ABI, agent tool contract, on-disk format conventions
- **Tier:** 3
- **Date opened:** 2026-05-12
- **Date `final-comment` started:** —
- **Date accepted / rejected:** —

## Summary

Layer a time-series view on top of the existing `souxmar_field_t` (which already carries `num_time_steps`, see `include/souxmar-c/field.h:50`) so the renderer can play back transient solver output frame-by-frame. New C ABI surface `include/souxmar-c/timeseries.h` (additive minor bump v1.8 → v1.9 under the ADR-0008 ratchet — closes the chain of six minor bumps in the post-v1.0 block) layers on `field.h`; the bridge gains `timeseries_open` / `timeseries_frame` / `timeseries_cache_window` commands; the desktop gets playback controls (play / pause / scrub / step / loop). Animation export goes through a new `writer.video` plugin type whose first conforming implementation is `examples/plugins/video-ffmpeg` — a subprocess wrapper around ffmpeg following the ADR-0009 (OpenFOAM) process-isolation pattern. PVD is the on-disk format the reader stack standardises on; numbered-VTU input is accepted via a glob convention.

## Motivation

The Sprint 32 exit criteria require a transient demo that plays in the viewport and exports as MP4. Concretely:

- The Newmark-β reference solver lands the same sprint and emits per-timestep VTU files; without a time-series view, the viewport renders the *last* timestep only — the dynamic behaviour is invisible to the user.
- Post-1.0 themes (per `docs/ROADMAP.md`) call out "Nonlinear and transient solvers" as a near-term direction. The on-disk and IPC conventions set here govern how every future transient solver's output reaches the user.
- "Show your manager the simulation" is a recurring forum request that the static screenshot path cannot answer. MP4 export turns a desktop tool into something users actually share.
- The agent's "run this transient case and play it back" eval case (planned for Sprint 32) presumes a tool surface that does not exist.

Timing: RFC-006 must merge before Sprint 32 starts; it is the last RFC of the post-v1.0 block and gates the v1.3 release.

## Proposal

Four layers, top-down.

### 1. On-disk format: PVD as canonical, numbered VTU as accepted input

PVD (ParaView Data) is a thin XML wrapper that points at a sequence of VTU files plus timestamps:

```xml
<?xml version="1.0"?>
<VTKFile type="Collection" version="0.1">
  <Collection>
    <DataSet timestep="0.000" file="step_0000.vtu"/>
    <DataSet timestep="0.001" file="step_0001.vtu"/>
    <DataSet timestep="0.002" file="step_0002.vtu"/>
    ...
  </Collection>
</VTKFile>
```

Picked because it's the de-facto standard for time-series VTU and Paraview / VisIt / VTK all read it unchanged. The Newmark-β reference solver writes PVD; any future transient solver plugin is expected to emit PVD too (or a directly-readable equivalent). Numbered-VTU input (`output_*.vtu` discovered by glob) is accepted as a convenience — the reader synthesises an in-memory PVD from the glob with uniform timesteps.

### 2. C ABI: `include/souxmar-c/timeseries.h` (new)

`SOUXMAR_ABI_VERSION_MINOR` in `abi.h` bumps from **8** to **9**, with a new history line:

```
 *   v1.9  Sprint 32 push 1 — time-series stream surface
 *                            (souxmar-c/timeseries.h); ADR-0042.
```

Additive minor under the ADR-0008 ratchet. Existing plugins compiled against v1.0–v1.8 link and load unchanged. Function prototypes are bare (no `SOUXMAR_API` decoration), matching the existing `field.h` convention.

```c
/* SPDX-License-Identifier: Apache-2.0
 *
 * include/souxmar-c/timeseries.h — additive, v1.9 of the C ABI.
 *
 * A time-series view over a sequence of souxmar_field_t handles. The
 * sequence is keyed by an opaque handle that the reader plugin
 * produces (typically by parsing a PVD); the host owns frame caching.
 *
 * Memory model: a cache window holds at most N frames in RAM; frames
 * outside the window are evicted (LRU) and re-read on demand. Window
 * size is host-controlled — the renderer asks for a window large
 * enough to keep playback at the requested framerate, but never asks
 * for the whole series unless the user explicitly pins it.
 *
 * Threading: a series is single-threaded for write (frame eviction
 * happens on the host worker), single-threaded for read (the renderer
 * thread).
 */

#ifndef SOUXMAR_C_TIMESERIES_H
#define SOUXMAR_C_TIMESERIES_H

#include "abi.h"
#include "field.h"
#include "status.h"

SOUXMAR_C_BEGIN

typedef struct souxmar_timeseries_t souxmar_timeseries_t;

/* ---- Lifecycle ---- */

/* Open a PVD or PVD-equivalent file. The reader is selected via the
 * existing reader.* plugin dispatch on the file extension. */
souxmar_timeseries_t* souxmar_timeseries_open(const char* path);

void souxmar_timeseries_close(souxmar_timeseries_t* series);

/* ---- Series metadata ---- */

size_t souxmar_timeseries_frame_count(const souxmar_timeseries_t* series);

souxmar_status_t souxmar_timeseries_time(const souxmar_timeseries_t* series,
                                          size_t                       frame_index,
                                          double*                      out_time);

size_t souxmar_timeseries_field_count(const souxmar_timeseries_t* series);

const char* souxmar_timeseries_field_name(const souxmar_timeseries_t* series,
                                           size_t                       field_index);

/* ---- Frame access ----
 *
 * Returns a field handle valid until the next call that mutates the
 * cache (any souxmar_timeseries_frame or souxmar_timeseries_cache_*
 * call). The renderer thread must consume the field before it next
 * calls the API. The host promises the returned field is in the
 * cache window. */
const souxmar_field_t* souxmar_timeseries_frame(souxmar_timeseries_t* series,
                                                  size_t                 frame_index,
                                                  const char*            field_name);

/* ---- Cache control ----
 *
 * window_size: how many frames the host may keep resident. Defaults
 * to 16. Setting 0 disables caching (every frame request hits disk).
 * Setting SIZE_MAX pins the whole series (caller's risk). */
souxmar_status_t souxmar_timeseries_cache_window(souxmar_timeseries_t* series,
                                                  size_t                 window_size);

/* Pre-warm the cache so playback can start without disk stalls. */
souxmar_status_t souxmar_timeseries_cache_preload(souxmar_timeseries_t* series,
                                                   size_t                 start_frame,
                                                   size_t                 count);

SOUXMAR_C_END
#endif /* SOUXMAR_C_TIMESERIES_H */
```

The existing `souxmar_field_t` carries `num_time_steps`; that path is preserved for solvers that produce a single multi-timestep field in one allocation. The `souxmar_timeseries_t` path is for the disk-backed case where loading all timesteps at once would blow out memory.

### 3. New plugin type: `writer.video`

Distinct from `writer.*` (which writes a single mesh / field snapshot) — `writer.video` consumes a `souxmar_timeseries_t` plus rendered-frame PNGs from the renderer and produces a video file. First implementation: `examples/plugins/video-ffmpeg/`, capability `writer.video.mp4` (and `writer.video.gif`).

Following ADR-0009 (the OpenFOAM precedent), the plugin uses **subprocess isolation**: it invokes the `ffmpeg` binary as a child process via `souxmar::plugin::run_subprocess` rather than linking libavcodec / libavformat. Reasons:

- ffmpeg's licensing landscape (LGPL/GPL builds, GPL-only codecs, patent-encumbered codecs) is complex; keeping ffmpeg as an external binary means downstream distros can swap in their preferred build without souxmar relinking.
- The single-binary install of souxmar does not need to ship ffmpeg; we tell the installer "ffmpeg on PATH or set `SOUXMAR_FFMPEG_PATH`" and surface a clean error if absent.
- The subprocess isolation contains ffmpeg crashes / OOMs to a child process; the desktop doesn't take the hit.

```toml
# examples/plugins/video-ffmpeg/souxmar-plugin.toml (sketch)

[plugin]
id            = "dev.souxmar.examples.video-ffmpeg"
name          = "ffmpeg Video Writer"
version       = "0.1.0"
abi           = 1
min_souxmar_abi_minor = 9
license       = "Apache-2.0"

[plugin.capabilities]
provides      = ["writer.video.mp4", "writer.video.gif"]

[plugin.threading]
# Subprocess isolation; plugin host marshals frame writes.
model         = "single-threaded"

[plugin.subprocess]
# souxmar's subprocess harness invokes this, captures stderr.
binary        = "ffmpeg"
binary_env    = "SOUXMAR_FFMPEG_PATH"
```

### 4. Bridge + React side

```rust
// src-tauri/souxmar-bridge/src/timeseries.rs (sketch)

pub struct TimeSeriesSummary {
    pub project_id:   String,
    pub series_id:    String,
    pub frame_count:  u32,
    pub time_min:     f64,
    pub time_max:     f64,
    pub field_names:  Vec<String>,
}

impl Bridge {
    pub fn timeseries_open(&self, project_id: &str, path: &str)
        -> Result<TimeSeriesSummary, BridgeError>;

    pub fn timeseries_frame(&self, series_id: &str, frame_index: u32, field_name: &str)
        -> Result<FieldChunk, BridgeError>;     // reuses FieldChunk from RFC-002

    pub fn timeseries_cache_window(&self, series_id: &str, size: u32)
        -> Result<(), BridgeError>;

    pub fn animation_export(&self, project_id: &str, series_id: &str,
                            output_path: &str, fps: u32, resolution: [u32; 2])
        -> Result<AnimationExportSummary, BridgeError>;
}
```

The React side adds a playback strip below the viewport: play / pause / scrub / step (←/→) / loop toggle / frame counter / time display. Framerate is throttled to the requested FPS (default 24); the host pre-warms a 16-frame cache window before play starts. The user adjusts cache window in Settings → Performance — most users never touch it, but the knob exists for users on RAM-constrained machines.

The deformed-shape pass from RFC-002 applies per-frame: at frame `i`, the renderer reads the `displacement` field at `i` and applies it to the mesh vertices. Color-by-field and threshold also re-evaluate per-frame. Composing the three (deformed shape + colored stress + threshold filter) at 24 fps on 250k tris is the perf target.

### Agent tool surface (5 tools)

| Tool                       | Confirmation    | Purpose                                                |
| -------------------------- | --------------- | ------------------------------------------------------ |
| `viz.timeseries_open`      | `confirm-once`  | Disk read; load a PVD into the active project.          |
| `viz.play`                 | `auto`          | Start playback at current frame.                        |
| `viz.pause`                | `auto`          | Pause playback.                                         |
| `viz.scrub`                | `auto`          | Jump to a specific frame or time.                       |
| `viz.export_animation`     | `confirm-once`  | Write an MP4 / GIF; subprocess plugin call.             |

Each ships with at least one eval case. `viz.export_animation` is `confirm-once` because it produces a new file at a user-named path; consistent with every other "write a file" tool.

## Alternatives considered

### Alternative A: Single-frame view; user manually loads each timestep

Keep RFC-002's field-stream surface as-is; let the user (or agent) call `field_stream_open` once per timestep.

Rejected because: (a) playback at 24 fps means 24 round-trips per second through the bridge — JSON encoding cost dominates at this rate; (b) the renderer cannot pre-warm a cache window; first-frame latency on every play action is unacceptable; (c) puts memory management on the user — they choose between "load everything" (OOM on long runs) or "load one frame" (stutter on every step).

### Alternative B: Load entire series into one `souxmar_field_t` with `num_time_steps > 0`

The existing field handle already supports this — keep using it.

Rejected for the long-running case: a 10,000-frame transient simulation on a 250k-vertex mesh with 6 components per stress tensor is 30 GB at f64. We cannot hold that in RAM. The handle path is fine for solver-internal time-stepping (where the solver controls memory anyway); it is wrong for disk-backed playback.

### Alternative C: WebCodecs API in the renderer for MP4 encoding

Encode MP4 in-browser using WebCodecs (a recent Chromium API).

Rejected because: (a) WebCodecs is not yet available in Tauri's wry webview across all three OSes (Safari/WKWebView coverage is partial as of 2026-Q2); (b) hardware encoder availability varies — silent quality differences across user machines; (c) we still need a subprocess plugin for GIF export and high-quality codec choices, so a WebCodecs path is a *second* exporter to maintain, not a simpler one. Revisit if WebCodecs stabilises across the three webview implementations and we drop the GIF requirement.

### Alternative D: link libavcodec directly

Skip the subprocess; link libavcodec and use its API directly.

Rejected because: (a) licensing — ffmpeg's GPL builds would force a relicense; the LGPL-only build subset of codecs is narrow enough to anger users who want H.265; (b) ABI churn on libavcodec is frequent and painful; (c) crashes in libavcodec take down the host process. Subprocess isolation is exactly the boundary that lets us not care about these. The ADR-0009 precedent is on point.

### (Considered and rejected: do nothing)

Without a time-series view, the v1.3 release ships a transient solver whose output can't be played back. Reduces the v1.3 visible story to "we shipped a solver and a static result picture" — does not justify a minor version.

## Drawbacks

- **PVD as canonical format is a lock-in.** Once shipped, switching to a different time-series format is a v2 ABI break. Mitigated by PVD's longevity (15+ years in ParaView), but it's still a long-lived commitment.
- **ffmpeg-on-PATH dependency.** First-launch users on a fresh Windows install will see "ffmpeg not found" when they try to export. Installer story needs a recommended download link; documented in `docs/RELEASE_NOTES_TEMPLATE.md`.
- **Cache window as a knob users will get wrong.** Setting `window_size = SIZE_MAX` because "I have 64 GB RAM" makes the renderer unresponsive when the series doesn't fit. The default (16) handles 95% of cases; we don't ship a "load all" button in the UI even though the ABI supports it.
- **Bridge call overhead at 24 fps.** Tauri's JSON-encoded IPC at 24 fps on a frame full of field arrays is on the perf budget edge. RFC-001's shared-mmap follow-up (PR 5 in its implementation plan) becomes effectively mandatory for time-series workflows; we should land it before v1.3 GA.
- **The 5 agent tools cross-reference RFCs 1 and 2.** `viz.timeseries_open` invalidates the static `viz.set_color_field` selection; the tool surface needs a consistent "what changes when you open a series" story documented in `docs/AI_INTEGRATION.md`.

## Migration plan

- **Existing in-tree / out-of-tree plugins:** unaffected (additive ABI).
- **Existing pipeline files:** unaffected.
- **Existing `examples/cantilever-beam`:** unaffected — still static. A *new* `examples/dynamic-beam` (the Newmark-β demo case) lands the same sprint.
- **Saved chat sessions:** the 5 `viz.*` tools are additive; sessions referencing only v1.0–v1.2 tools replay identically.
- **Settings file:** new `[performance.timeseries]` section in `~/.souxmar/settings.toml` for cache-window override; absent → use the default (16). Additive.
- **Documentation:** new `docs/tutorials/a-dynamic-beam.md`; updates to `docs/DESKTOP_APP.md` (playback section), `docs/AI_INTEGRATION.md` (5 new tools), `docs/PLUGIN_SDK.md` (new `writer.video.*` plugin type); ffmpeg-install line in `docs/INFRA_STATUS.md`.

## Pre-mortem

It is 2027-05-12. RFC-006 went badly. What happened:

We shipped v1.3 with the cache window default at 16 frames and discovered three weeks later that on a popular configuration (4-core integrated GPU laptops) the disk-read cost for the next-batch of frames exceeded the playback interval, causing visible stutter at 24 fps. We patched the default to 32 in v1.3.1 and added an "auto-tune" path, but the launch-week reviews picked up the stutter and the user perception of v1.3 was "slow." Separately, the ffmpeg subprocess path on Windows surfaced a recurring "path with spaces in user home directory" bug that we'd fixed in the OpenFOAM subprocess plugin years earlier but hadn't ported into the shared subprocess harness. Users hit it; the GitHub issue templated the same fix twice.

Leading indicators to watch in the first six months:

- Any user-reported "playback stutters" issue at the default cache-window setting on a documented-supported machine.
- Animation-export failures correlating with paths containing spaces, unicode, or non-ASCII.
- ffmpeg-not-found rate above 5% of attempted exports — implies the installer flow needs a redesign.
- `viz.export_animation` p95 latency above 2× real-time playback duration — implies the frame-render-to-disk path is slower than budget.

## Open questions

1. **Cache-window auto-tune.** Could the host measure disk read latency for the current series and pick the cache window automatically. Probably yes; deferred to a v1.3.1 follow-up if launch metrics warrant it.
2. **Frame interpolation for sub-timestep playback.** Solvers emit at fixed timesteps; the user might want smooth playback between them. Linear interpolation of displacement is straightforward; for colormap on stress, interpolation is wrong (max stress at a moment between two solver steps is not the linear mean of the two). Punt — no interpolation in v1.3.
3. **Animation export resolution.** Match the viewport, or a user-set resolution? Lean user-set with viewport as default; 1080p / 4K presets.
4. **Loop mode semantics.** Play → end → restart vs. ping-pong? Default "restart"; ping-pong as a toggle.
5. **Series with non-uniform timesteps.** PVD supports arbitrary timestep values; ensure playback respects real time (not just frame index) when the timesteps are non-uniform. Document this — users on adaptive-timestep solvers will hit it.
6. **`writer.video` for *just* the renderer view vs. the full panel layout.** First v1.3 ships viewport-only export; "export the whole workbench" is a v1.4 ask if anyone wants it.
7. **MP4 codec choice.** H.264 default for broad compatibility; H.265 / AV1 as alternatives. ffmpeg arg surface details TBD; pick at Sprint 32 day 1.

## Implementation plan

Six PRs in Sprint 32.

- [ ] **PR 1 — ABI add.** `include/souxmar-c/timeseries.h`; in-core stubs; `SOUXMAR_ABI_VERSION_MINOR` bump 8 → 9 with the new history line; conformance scaffold for `writer.video.*`. Commit marker `Ratchet: additive minor surface (ADR-0008)`. Reviewer: ABI gate.
- [ ] **PR 2 — PVD reader extension.** Extend the Sprint 27 `reader.vtu` plugin (RFC-002 PR 3) to also accept `*.pvd` and produce a `souxmar_timeseries_t`; cache-window backing impl in libcore.
- [ ] **PR 3 — Bridge surface + cache.** Rust commands; Tauri registration; LRU cache; pre-warm path; React `useTimeSeries(series_id)` hook.
- [ ] **PR 4 — Playback UI.** Playback strip below viewport; per-frame deformed-shape / color-by-field / threshold re-evaluation; performance gate (24 fps on 250k tris on the CI integrated-GPU runner).
- [ ] **PR 5 — `video-ffmpeg` plugin.** Subprocess-driven; conformance round-trip ("export a 24-frame test series, decode and compare frame hashes within tolerance"); error UX for ffmpeg-not-found.
- [ ] **PR 6 — Agent tools + reference solver + example.** Five `viz.*` tools; `examples/plugins/solver-dyn` (Newmark-β linear-elastic dynamics); `examples/dynamic-beam` example case; "A dynamic beam" tutorial; v1.3 release notes.
- [ ] ADR-0042 filed at `docs/adr/0042-abi-v1-9-timeseries-ratchet.md` — records the v1.9 minor bump under the ADR-0008 ratchet. Filed with PR 1.
- [ ] ADR filed at `docs/adr/NNNN-pvd-as-canonical-timeseries.md`.
- [ ] Documentation: tutorial; release notes; `docs/INFRA_STATUS.md` ffmpeg dependency note.

## References

- `docs/SPRINT_PLAN.md` — Post-v1.0 plan, Sprint 32 row (v1.3 release).
- `docs/rfcs/0002-field-stream-protocol.md` — Single-frame field stream this RFC extends.
- `docs/rfcs/0001-viewport-renderer.md` — Renderer this RFC drives; shared-mmap follow-up becomes mandatory.
- `docs/adr/0009-openfoam-process-isolation.md` — Subprocess-plugin precedent the `writer.video` plugin follows.
- `docs/AI_INTEGRATION.md` — Tool-surface contract; the `viz.timeseries_open` state-change story lives here.
- `docs/PLUGIN_SDK.md` — Plugin-type taxonomy; gains `writer.video.*`.
- `docs/ROADMAP.md` — "Nonlinear and transient solvers" post-1.0 theme this RFC enables.
- `include/souxmar-c/field.h` — Existing field handle with `num_time_steps`.
- ParaView PVD format reference — TBD link at Sprint 32 day 1.
- ffmpeg CLI documentation — TBD link.
