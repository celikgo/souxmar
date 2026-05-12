# ADR-0042: Time-series C ABI ratchet (v1.8 → v1.9)

- **Status:** Proposed
- **Date:** 2026-05-12 (Sprint 32 push 1)
- **Author:** celikgokhun
- **Deciders:** core, plugin-host, adapters, desktop, AI, platform, DX
- **Tier:** 3 (heavy / requires RFC — frozen-header ratchet + new plugin type)
- **Affects:** `include/souxmar-c/timeseries.h` (new), `include/souxmar-c/abi.h`
  (`SOUXMAR_ABI_VERSION_MINOR` bump 8 → 9 + history line), `docs/PLUGIN_SDK.md`
  (new `writer.video.*` plugin type), conformance suite (PVD round-trip,
  cache-window LRU correctness), `examples/plugins/video-ffmpeg` (subprocess
  plugin, ADR-0009 pattern), `examples/plugins/solver-dyn` (Newmark-β reference
  transient solver), on-disk format conventions (PVD canonical, numbered VTU
  accepted).

## Context

Sprint 32 closes the post-v1.0 block with the transient + animation surface. The reference Newmark-β linear-elastic dynamics solver lands the same sprint and emits per-timestep VTU files; without a time-series view, the viewport renders the last timestep only — the dynamic behaviour is invisible.

The existing `souxmar_field_t` carries `num_time_steps` (see `include/souxmar-c/field.h:50`) — but that path is for solver-internal time-stepping where the solver controls memory. A long transient run (10,000 frames × 250k vertices × 6 stress components × f64) is 30 GB; we cannot materialize it in one field handle. The time-series surface introduces **bounded-memory streaming** with an LRU cache window.

A new plugin type `writer.video.*` joins the surface for animation export, following ADR-0009's subprocess-isolation pattern (ffmpeg as a child process, not a linked library — for licensing reasons + crash containment).

RFC-006 carries the architectural rationale (PVD format choice, cache-window UX, codec choices); this ADR carries the binding declaration under ADR-0008. This is the **final ratchet of the post-v1.0 block**.

## Decision

The C ABI gains **one new header** (`include/souxmar-c/timeseries.h`), **one new opaque handle type** (`souxmar_timeseries_t`), and **nine new function declarations** covering lifecycle, series metadata, frame access, and cache control. A new plugin type `writer.video.*` is introduced for animation export. `SOUXMAR_ABI_VERSION_MINOR` bumps from **8** to **9** with a new history line in `abi.h`. No existing declaration moves; strict additive minor ratchet per ADR-0008. Sits on top of ADRs 0037–0041.

### New surface

See RFC-006 §2 (C ABI) for the verbatim header. Nine entry points: `_open` / `_close`, `_frame_count` / `_time`, `_field_count` / `_field_name`, `_frame`, `_cache_window` / `_cache_preload`.

### `abi.h` history entry

```
 *   v1.9  Sprint 32 push 1 — time-series stream surface
 *                            (souxmar-c/timeseries.h); ADR-0042.
```

`#define SOUXMAR_ABI_VERSION_MINOR 8` becomes `#define SOUXMAR_ABI_VERSION_MINOR 9`.

### Host-side implementation contract

- **The series is opened by a reader plugin.** `souxmar_timeseries_open(path)` dispatches to the existing `reader.*` plugin family on the file extension (`.pvd` → PVD reader, numbered VTU → synthetic in-memory PVD). New file formats join via additional `reader.*` plugins; no ABI change needed.
- **Cache window is bounded.** Default 16 frames. Setting `0` disables caching. Setting `SIZE_MAX` pins the entire series in RAM (caller's risk; surfaced only via Settings → Performance, never via default UI).
- **Frame handle is borrowed.** `souxmar_timeseries_frame(series, i, name)` returns a `const souxmar_field_t*` valid until the next mutating call on the series. The renderer must consume the field before the next frame request.
- **LRU eviction.** The host evicts least-recently-used frames when the cache window is full and a new frame is requested.
- **Threading: single-writer (host worker) + single-reader (renderer).** Multiple readers on one series are undefined.
- **Time values are f64.** PVD `timestep` values can be arbitrary precision; we preserve f64 for the API surface even though the renderer typically only needs ~3 significant figures.
- **`writer.video.*` plugin contract.** A new plugin type whose vtable accepts a `souxmar_timeseries_t*` + a target path + (fps, resolution) parameters. The reference `video-ffmpeg` plugin invokes the `ffmpeg` binary as a child process via the existing `souxmar::plugin::run_subprocess` harness; subprocess isolation contains ffmpeg crashes to a child process.

### What stays out of scope

- **Sub-timestep interpolation.** Solvers emit at fixed timesteps; smooth playback between them would require interpolation that's correct for displacement but wrong for stress maxima. Punt — no interpolation in v1.9.
- **Adaptive cache-window auto-tune.** RFC-006 Open Q1 — defer to v1.9.1 if launch metrics warrant it.
- **Frame-level binary diff streaming.** Sending only the delta between frames would cut JSON encode cost dramatically; out of scope for v1.9 because it requires a much more complex protocol on the bridge.
- **Full-workbench animation export.** v1.9 ships viewport-only export; "export the whole workbench" is a v2.x ask.
- **Multi-series composition.** Playing two transient runs side by side is a v1.9.x feature, not v1.9.

## Alternatives considered

### Use existing `souxmar_field_t` with `num_time_steps > 0` for everything

The existing field handle already supports time-step counts. Pro: zero ABI churn. Con: holds the whole series in RAM, which doesn't fit for long runs (30 GB on a 10k-frame transient). Acceptable for solver-internal time-stepping (where the solver owns memory anyway); wrong for disk-backed playback.

### Implement video export with WebCodecs in the renderer

Per RFC-006 §Alternative C — rejected. WebCodecs is not yet stable across the three Tauri webviews; hardware encoder coverage is uneven; we still need a subprocess plugin for GIF, so WebCodecs would be a *second* exporter to maintain.

### Link libavcodec directly instead of a subprocess plugin

Per RFC-006 §Alternative D — rejected. ffmpeg's GPL builds force a relicense; the LGPL subset of codecs is too narrow; libavcodec ABI churn is painful; crashes take down the host. ADR-0009's subprocess-isolation pattern is exactly the right boundary.

### Streaming-cursor C API (`*_next_frame()` instead of indexed `_frame(i)`)

Pro: matches a "playback" mental model. Con: random access (scrub) becomes awkward; the renderer wants frame `i` directly, not "advance N times." Indexed access with a host-side cache is the right shape; the playback strip advances `i` deterministically.

### Skip the ratchet; make playback a desktop-only feature

Pro: zero ABI surface. Con: Python bindings and the CLI would have no access to time-series; "run a transient and export an animation from a script" becomes desktop-only. Out of step with the "CLI / Python / desktop / agent are peers" architecture.

## Consequences

### Positive

- **Sprint 32 exit criterion has a stable target.** The dynamic-beam example plays + exports through this surface.
- **Future transient solvers join cleanly.** Any solver plugin that emits PVD (or a directly-readable equivalent) gets playback for free.
- **Subprocess isolation is documented precedent.** ADR-0009 established the pattern; ADR-0042 extends it to a second concrete plugin (`video-ffmpeg`). Future "third-party binary as a plugin" cases (e.g., ParaView for a complex render path) inherit the pattern.
- **Cache-window contract is explicit.** Users on memory-constrained machines have a knob; the default handles 95% of cases.

### Negative

- **PVD as canonical lock-in.** Switching to a different time-series format later is a v2 ABI break. PVD's 15-year longevity in ParaView mitigates this; still a long-lived commitment.
- **ffmpeg-on-PATH dependency.** First-launch users on a fresh Windows install hit "ffmpeg not found" on export. Installer story documented; release notes call it out.
- **Bridge JSON encode at 24 fps is on the perf edge.** RFC-001's shared-mmap follow-up (PR 5 in its plan) becomes effectively mandatory for time-series workflows; the dependency is explicit in RFC-006's drawbacks.
- **Final ratchet of the block — six minor bumps in eight sprints.** The post-v1.0 block moves the v1 ABI from MINOR 3 to MINOR 9. Each ratchet is reviewable in isolation, but the cumulative surface area added is substantial and worth flagging in the v1.3 release notes.

### Risks

- **Risk:** Cache-window default of 16 stutters on integrated-GPU laptops with slow SSDs. **Mitigation:** RFC-006 pre-mortem; auto-tune deferred to v1.9.1 if metrics warrant.
- **Risk:** Path-with-spaces bug in the ffmpeg subprocess call recurs (we fixed it once for OpenFOAM in ADR-0009 era). **Mitigation:** the subprocess harness gained path-quoting back then; the `video-ffmpeg` plugin reuses the harness, so the fix should be inherited automatically. PR 5's tests include path-with-spaces fixtures.
- **Risk:** Non-uniform timesteps (adaptive-timestep solvers) break playback assumptions. **Mitigation:** the API returns the real `time` per frame, not just the index; the playback strip respects real time. Documented.
- **Risk:** PVD parser bug on a non-trivial corpus surfaces post-launch. **Mitigation:** conformance corpus includes multi-cell-type PVD fixtures and adaptive-timestep fixtures from day 1.
- **Risk:** Plugin C004 skew on v1.9. **Mitigation:** same gate as ADR-0037 — C004 covers the matrix.

## Pre-mortem (one year from today)

It is 2027-05-12 and the time-series ratchet went badly. Most likely failure mode (per RFC-006 pre-mortem): cache-window default of 16 frames produces visible stutter at 24 fps on a popular integrated-GPU laptop SKU; launch-week reviews pick it up. We patch the default to 32 in v1.3.1 and add an auto-tune path. The fix is small but the launch perception sticks.

Less-likely failure mode: the `video-ffmpeg` subprocess plugin hits a path-with-spaces bug on Windows that we'd fixed for OpenFOAM but didn't port into the shared subprocess harness correctly. Users hit the same GitHub issue template they would have hit two years earlier.

Leading indicators:

- "Playback stutters" reports at default cache window on documented-supported hardware.
- Animation-export failures correlating with paths containing spaces or non-ASCII characters.
- ffmpeg-not-found rate > 5% of attempted exports — implies installer flow needs a redesign.
- `viz.export_animation` p95 latency > 2× real-time playback duration — implies the frame-render-to-disk path is slower than budget.

## References

- ADR-0008 — ABI v1 final freeze + ratchet rules.
- ADR-0037 / 0038 / 0039 / 0040 / 0041 — preceding ratchets of the post-v1.0 block.
- ADR-0009 — OpenFOAM process-isolation pattern; the precedent for `video-ffmpeg`.
- RFC-006 (`docs/rfcs/0006-time-series.md`) — the gating RFC.
- RFC-001 — shared-mmap follow-up dependency.
- RFC-002 — single-frame field-stream that this surface extends.
- `include/souxmar-c/abi.h` — file under ratchet.
- `include/souxmar-c/field.h` — existing field handle this surface layers on.
- `scripts/check-frozen-headers.sh` — CI gate.

## History

- 2026-05-11 (Sprint 6 push 4): first ratchet — `reader.*` (v1.0 → v1.1).
- 2026-05-11 (Sprint 7 push 3): second — mmap-backed buffer (v1.1 → v1.2).
- 2026-05-11 (Sprint 9 push 2): third — per-face-tag, ADR-0012 (v1.2 → v1.3).
- 2026-05-12 (Sprint 25 push 1): fourth — surface-stream, ADR-0037 (v1.3 → v1.4).
- 2026-05-12 (Sprint 27 push 1): fifth — field-stream, ADR-0038 (v1.4 → v1.5).
- 2026-05-12 (Sprint 28 push 1): sixth — BREP session, ADR-0039 (v1.5 → v1.6).
- 2026-05-12 (Sprint 29 push 1): seventh — 2D sketch, ADR-0040 (v1.6 → v1.7).
- 2026-05-12 (Sprint 30 push 1): eighth — BREP feature ops, ADR-0041 (v1.7 → v1.8).
- 2026-05-12 (Sprint 32 push 1): **ninth ratchet — time-series stream + writer.video.* plugin type, this ADR** (v1.8 → v1.9). Proposed; gates on RFC-006 acceptance. Closes the post-v1.0 ratchet chain.
