# RFC 0002: Field-stream protocol on the bridge

- **Author:** TBD (Platform team lead, ratified by Desktop)
- **Status:** Draft
- **Tracking issue:** TBD
- **Affects:** ABI, agent tool contract
- **Tier:** 3
- **Date opened:** 2026-05-12
- **Date `final-comment` started:** —
- **Date accepted / rejected:** —

> **Stub.** Pairs with RFC-001 (viewport renderer). Establishes how scalar/vector/tensor fields from a solver run reach the renderer — the contract is set; chunk encoding + colormap details are deliberate `TBD`s for Sprint 27 week 1. Time-series playback is **out of scope** here; it lives in RFC-006.

## Summary

Layer a field-stream surface on top of the existing `souxmar_field_t` handle so the workbench can render scalar / vector / tensor fields produced by any solver plugin, alongside the mesh streamed via RFC-001. The C ABI surface is additive (new `include/souxmar-c/field_stream.h`, ABI minor `(1, 2, 0)`); the bridge gains `field_stream_open` + `field_stream_chunk` Rust commands; the desktop wires those into Three.js vertex/face colour buffers and arrow glyphs. A reference `reader.vtu` plugin lands in the same sprint so the cantilever `cantilever.vtu` output renders without per-build special-casing.

## Motivation

After RFC-001 lands at end of Sprint 25, the workbench renders meshes but cannot render results. The cantilever sample writes `cantilever.vtu` via the `writer.vtu` plugin — that file is on disk after every pipeline run, but the user has no way to *see* the stress field. Concretely:

- Sprint 26 dogfood (planned) will surface the same complaint pattern as Sprint 24 ("I ran the solver — what did it produce?") unless field rendering ships with the v1.1 release.
- The agent already has `pipeline_summary` (read pipeline stages) but no equivalent for "summarise the results field" — the chat cannot answer "what's the peak stress?" without this surface.
- The on-disk VTU is the canonical format, but reading it from JavaScript adds a 1.4 MB parser dependency and duplicates code that the in-core `reader.vtu` plugin should own anyway.

Time pressure: RFC-002 must be merged before Sprint 27 starts; the dependency chain (S25 → S26 → S27 v1.1) is on the critical path of the post-v1.0 block.

## Proposal

Three layers, top-down, mirroring RFC-001.

### 1. React side

A new `useFieldStream(projectId, fieldName)` hook in `src/desktop/src/workbench/useFieldStream.ts` that fetches the field arrays and pushes them into the Three.js geometry as vertex colors (scalar / magnitude of vector) or instanced arrow geometry (vector glyphs). Colormap selection in the Viewport toolbar: `viridis | coolwarm | grayscale | plasma`. A legend component renders the colormap + min/max + units (units string carried in the field metadata; **TBD** in the C ABI — see Open questions).

Render modes added on top of RFC-001's four:

- `color_by_field` — vertex/face colors driven by a scalar (or vector magnitude).
- `glyphs` — instanced arrows at every Nth vertex (N is a UI slider; default = ceil(vertex_count / 5000)).
- `deformed_shape` — apply a displacement vector field to vertex positions with a scale slider (1× / 10× / 100×).
- `threshold` — hide elements where the bound field is outside `[min, max]`.

Multiple field overlays compose: `color_by_field(σ_vm)` + `deformed_shape(u)` is the canonical structural visualisation and must compose without an extra round-trip.

### 2. Bridge side

```rust
// New file: src-tauri/souxmar-bridge/src/field_stream.rs (sketch)

pub struct FieldSummary {
    pub project_id:    String,
    pub field_name:    String,
    pub location:      u8,         // 0 = vertex, 1 = cell  (matches SOUXMAR_FL_*)
    pub kind:          u8,         // 0 = scalar, 1 = vector, 2 = symmetric tensor
    pub components:    u8,         // 1 / 3 / 6
    pub count:         u64,        // vertex or cell count
    pub min:           Vec<f64>,   // length = components
    pub max:           Vec<f64>,
    pub units:         String,     // free-form; "Pa", "m", "K", "" if absent
    pub stream_token:  String,
}

pub struct FieldChunk {
    pub stream_token:  String,
    pub chunk_index:   u32,
    pub chunk_count:   u32,
    pub values:        Vec<f32>,   // SoA, length = chunk_size * components
}

impl Bridge {
    pub fn field_list(&self, project_id: &str)
        -> Result<Vec<String>, BridgeError>;

    pub fn field_stream_open(&self, project_id: &str, field_name: &str)
        -> Result<FieldSummary, BridgeError>;

    pub fn field_stream_chunk(&self, token: &str, chunk_index: u32)
        -> Result<FieldChunk, BridgeError>;
}
```

Encoding follows whatever RFC-001 chose for chunk transport (open question 1 there); we deliberately re-use the decision rather than re-litigate it.

### 3. C ABI (`include/souxmar-c/field_stream.h`, new)

`SOUXMAR_C_API_VERSION` bumps `(1, 1, 0)` → `(1, 2, 0)` — additive minor. Existing plugins continue to load.

```c
/* include/souxmar-c/field_stream.h — additive, v1.2.
 *
 * Renderer-friendly view over an existing souxmar_field_t. The view
 * is *derived* and read-only; the host computes per-component min/max
 * on first open and caches alongside the field. Threading: not
 * thread-safe; marshal via the existing host dispatcher.
 */

#ifndef SOUXMAR_C_FIELD_STREAM_H
#define SOUXMAR_C_FIELD_STREAM_H

#include "abi.h"
#include "field.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct souxmar_field_stream_t souxmar_field_stream_t;

SOUXMAR_API
souxmar_field_stream_t* souxmar_field_stream_open(const souxmar_field_t* field);

SOUXMAR_API
void souxmar_field_stream_close(souxmar_field_stream_t* stream);

SOUXMAR_API
size_t souxmar_field_stream_count(const souxmar_field_stream_t* s);

SOUXMAR_API
uint8_t souxmar_field_stream_components(const souxmar_field_stream_t* s);

SOUXMAR_API
souxmar_status_t souxmar_field_stream_range(const souxmar_field_stream_t* s,
                                             double* out_min, /* len == components */
                                             double* out_max);

SOUXMAR_API
const char* souxmar_field_stream_units(const souxmar_field_stream_t* s);

/* SoA read. out_capacity must be >= count * components. */
SOUXMAR_API
souxmar_status_t souxmar_field_stream_values(const souxmar_field_stream_t* s,
                                              float* out, size_t out_capacity);

#ifdef __cplusplus
}
#endif
#endif /* SOUXMAR_C_FIELD_STREAM_H */
```

### Reader plugin: `reader.vtu`

The current in-tree `examples/plugins/vtu-writer` writes; no reader exists. Sprint 27 lands `examples/plugins/vtu-reader`, capability `reader.vtu`, single-threaded (matches the writer's threading model). It produces a `souxmar_geometry_t` + `souxmar_mesh_t` + one or more `souxmar_field_t` per file. The conformance suite (`souxmar-conformance`) gains VTU round-trip tests on the cantilever output.

This RFC does **not** change the reader-plugin vtable — it's a new plugin, not a new plugin type.

### Agent tool surface

Four additive tools in `libsouxmar-ai`, all read-only / UI-state-only, all `auto` confirmation:

| Tool                          | Purpose                                                       |
| ----------------------------- | ------------------------------------------------------------- |
| `viz.list_fields`             | Returns field-name list for the loaded project.               |
| `viz.set_color_field`         | Sets the scalar field driving vertex/face colour.             |
| `viz.set_deformation_scale`   | Sets the displacement multiplier (1× / 10× / 100× / custom).   |
| `viz.set_threshold`           | Sets `(field_name, min, max)` for the threshold filter.        |

Adding tools post-v1 is non-breaking (verified against `reviewing-abi-changes` skill). Each ships with at least one eval case in `tests/agent-eval/`.

## Alternatives considered

### Alternative A: Render directly from `cantilever.vtu` in JavaScript

Bundle `vtk.js`'s VTU reader; bypass the bridge for results. The Sprint 26 dogfood could have results visualisation by week 2 with no ABI work.

Rejected because: (a) doubles the bundle size (vtk.js's VTU reader subset is ~800 KB minified); (b) results files larger than ~200 MB stall the renderer thread because the reader is synchronous; (c) the agent's "what's the peak stress?" question still needs an in-core code path, so the work is unavoidable — we'd just have done it twice. The C ABI surface is small enough that doing it once correctly is cheaper.

### Alternative B: HTTP server in the host process, fetch field arrays as Arrow over `127.0.0.1`

Run a tiny Arrow-format HTTP server in the host; React side fetches arrays via `fetch()`. Arrow's zero-copy semantics are appealing for big fields.

Rejected because: (a) introduces a third IPC mechanism (Tauri commands + plain TCP + the existing chat WebSocket would all coexist); (b) Arrow's bundle cost on the JS side (~600 KB) matches the entire renderer's; (c) authentication on a local TCP port is a security item we'd have to ADR — the bridge already inherits Tauri's process-level isolation. Worth revisiting only if Tauri's IPC turns out to be the perf bottleneck after RFC-001 closes its open question (1).

### Alternative C: VTU streaming over the bridge instead of field streaming

Stream the raw VTU bytes through the bridge; parse in JS. Avoids a new ABI surface.

Rejected because: the renderer needs SoA float buffers, not a parsed object graph; we'd pay the parse cost on the JS side every reload. Also keeps the field-summary computation (min/max/units) in JS, which the agent's `viz.list_fields` tool then has to re-implement.

### (Considered and rejected: do nothing)

Shipping v1.1 with renderer but no results visualisation gives users a 3D viewer over an empty result. Confusing; worse than the SVG placeholder it replaces.

## Drawbacks

- **Second additive ABI surface in one block** (`surface_stream.h` from RFC-001, `field_stream.h` here). Both are minor bumps; the v1 contract is unchanged; but the surface area to maintain post-v1 grows.
- **Float32 lossy on the wire.** Solver output is f64; we downcast to f32 for transport. Adequate for visualisation (8-bit-per-channel display is the real floor), but the agent's `viz.list_fields` summary must compute min/max in f64 *before* the downcast or the displayed range loses precision on stiff problems. Spelled out in the C header comment.
- **Reader plugin coupling.** `reader.vtu` becomes a load-bearing dependency of the v1.1 release; a bug there blocks results visualisation entirely. Mitigation: conformance tests on the cantilever golden VTU; the conformance gate already exists.
- **Colormap politics.** Picking four colormaps creates a small but real maintenance commitment (legend, accessibility, agent tool defaults). Documented in `docs/UI_DESIGN.md`.

## Migration plan

- **Existing in-tree plugins:** unaffected (additive ABI).
- **Out-of-tree plugins:** unaffected.
- **Existing pipeline files on disk:** unaffected.
- **Saved chat sessions:** the four `viz.*` tools are additive — sessions referencing only v1.0 tools replay identically.
- **Documentation:** update `docs/DESKTOP_APP.md` (results section), `docs/AI_INTEGRATION.md` (four new tools), `docs/PLUGIN_SDK.md` (note the in-tree `reader.vtu`). Tutorial "Inspecting results" is a new page in `docs/tutorials/`.

## Pre-mortem

It is 2027-05-12. RFC-002 went badly. What happened:

The float32 downcast turned out to be the wrong choice on a thermal problem where the user cared about temperature deltas of 0.001 K over a 300 K baseline — the f32 quantisation made the colour bands obviously stripey, and several users on the forum read "stripes mean discretisation error" when actually they meant "your renderer is lossy." We added a "high-precision mode" later that streams f64, but the colormap legend story was already entrenched and the migration was painful. Separately, the `reader.vtu` plugin had a corner-case bug on VTU files with mixed cell types that the conformance suite missed because all golden VTUs were single-cell-type. Two months of bug reports later we expanded the conformance corpus.

Leading indicators to watch in the first six months:

- Any user-reported "stripey colours" issue with a `<= 0.5%` field range relative to baseline.
- Conformance corpus VTU files all of the same cell type.
- `viz.list_fields` p95 latency exceeds 100ms on the cantilever — implies the in-core min/max computation isn't cached.
- More than one VTU-reader bug in the first three months — implies test coverage gap.

## Open questions

1. **Units string source.** The existing `souxmar_field_t` has no units accessor. Add one to `field.h` (a small ABI bump in addition to `field_stream.h`)? Or carry units in field-name conventions (`stress_Pa` / `displacement_m`)? Convention is brittle; lean toward adding the accessor.
2. **Tensor display.** Symmetric stress tensors have six components — show as nine scalar derivatives (σ_xx, σ_yy, …, σ_vm, σ_max, σ_min)? Hardcode the derivations in the reader, or expose tensor → scalar choosers in the React side? Lean toward the reader so the agent's `viz.set_color_field` tool has a flat name space.
3. **Colormap accessibility.** Viridis is colour-blind-safe; coolwarm is not. Default to viridis. Plasma is for pure aesthetic. Grayscale for accessibility / print. Lock the four; add more via plugin later if needed.
4. **Glyph density.** Auto-sampling at every Nth vertex misses anisotropic meshes (clusters of glyphs in refined regions). Better default is uniform 3D grid sampling — but that needs a kd-tree the renderer doesn't currently build. Defer kd-tree to S31, ship the Nth-vertex sampling for v1.1.
5. **Cell-located fields rendering.** Need per-face colours from per-cell data — flat shading vs. interpolated. Pick flat for cell-located, interpolated for vertex-located; document in the UI.
6. **Deformation scale persistence.** Should the scale slider value persist per project, per session, or be reset every load? Lean per-project, stored in `~/.souxmar/projects/<id>/viz.toml`.

## Implementation plan

Four PRs in Sprint 27, plus the reader plugin.

- [ ] **PR 1 — ABI add.** `include/souxmar-c/field_stream.h`; in-core backing impl; `SOUXMAR_C_API_VERSION` bump to `(1, 2, 0)`; conformance test add. Reviewer: ABI gate.
- [ ] **PR 2 — Bridge surface.** Rust commands; Tauri registration; unit tests against the bridge skeleton path. Closes open question (6) via the persistence file path.
- [ ] **PR 3 — Reader plugin.** `examples/plugins/vtu-reader/`; manifest, build, conformance round-trip on the cantilever VTU + on a multi-cell-type fixture.
- [ ] **PR 4 — Renderer wiring.** Three.js vertex/face colour pipeline; glyph instancing; deformed-shape pass; threshold filter; colormap legend component. Lands the four agent tools.
- [ ] Documentation update in `docs/DESKTOP_APP.md`, `docs/AI_INTEGRATION.md`, `docs/PLUGIN_SDK.md`.
- [ ] ADR filed at `docs/adr/NNNN-field-units-accessor.md` (Open question 1 — depends on its resolution).
- [ ] Tutorial: `docs/tutorials/inspecting-results.md`.

## References

- `docs/SPRINT_PLAN.md` — Post-v1.0 plan, Sprint 27 row (v1.1 release).
- `docs/rfcs/0001-viewport-renderer.md` — Sister RFC; chunk-encoding decision is shared.
- `docs/rfcs/0006-time-series.md` — Time-series playback (Sprint 32); this RFC does *not* address transient.
- `include/souxmar-c/field.h` — Existing field handle this RFC layers on.
- `examples/plugins/vtu-writer/` — Sibling writer; the new reader follows the same plugin pattern.
- `docs/AI_INTEGRATION.md` — Tool-surface contract.
