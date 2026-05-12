# RFC 0003: CAD kernel choice + `cad.*` plugin contract

- **Author:** celikgokhun
- **Status:** Draft
- **Tracking issue:** TBD — file at Sprint 28 kickoff
- **Affects:** ABI, governance (new plugin type), agent tool contract
- **Tier:** 3
- **Date opened:** 2026-05-12
- **Date `final-comment` started:** —
- **Date accepted / rejected:** —

## Summary

Adopt OCCT (LGPL-2.1 with linking exception, dynamic-link only) as the in-tree CAD kernel. Introduce a new plugin type `cad.*` distinct from `reader.*` — readers produce a one-shot `souxmar_geometry_t` snapshot today, whereas a `cad.*` plugin owns a *live* kernel session keyed by an opaque `souxmar_brep_session_t*` handle that supports edits across calls. The new ABI surface lands at `include/souxmar-c/brep.h` (additive minor bump v1.5 → v1.6 under the ADR-0008 ratchet — assumes RFC-001 and RFC-002 have already landed their v1.4 and v1.5 surfaces). The first conforming plugin is `examples/plugins/cad-occt`, replacing the existing `examples/plugins/occt-reader` (which becomes a thin wrapper that opens a session, exports the snapshot, and closes — preserving the v1 capability for plugins consuming `reader.step`).

The agent gains `cad.*` tools that operate on the BREP session — initially `import_step`, `list_bodies`, `get_body_metadata`. The parametric feature operations (extrude, revolve, fillet, boolean) ship in RFC-005's `feature.*` tools on top of this surface.

## Motivation

Sprint 28 is the unblocker for Sprints 29–30 (sketcher + parametric features). To author parametric geometry the host needs a kernel that understands BREP topology, analytic surfaces, and constructive history — not the discrete vertex/edge/face/solid tags exposed by `souxmar_geometry_t`. Concretely:

- The current `souxmar_geometry_t` (see `include/souxmar-c/geometry.h`) tracks entities only by integer index and tag; it has no notion of "this face is a cylinder of radius 5 mm" — information the mesher needs for curvature-adaptive refinement and the renderer needs for accurate edge extraction.
- The existing `examples/plugins/occt-reader` already proves OCCT links and ships across our three OSes, but it loses BREP topology at the reader boundary. We are paying the OCCT linking cost without getting the BREP benefit.
- Without a kernel session, "edit a dimension and watch the model regenerate" (a Sprint 30 exit criterion) has no place to land.

The kernel choice is the load-bearing decision because it determines licensing posture, distribution model, perf envelope, and what the agent can promise users for the next several years. Picking wrong is recoverable but expensive — the kernel touches every CAD-adjacent plugin.

## Proposal

### Kernel choice: OCCT 7.8 LTS (or current LTS at Sprint 28 day 1)

OCCT is the only mature open-source kernel that supports the surface set (NURBS, analytic primitives, B-rep with adjacency, STEP/IGES/BRep persistence) the sketcher / feature tree need. Already proven in the existing reader plugin. LGPL-2.1 with the "OCCT linking exception" is compatible with our Apache-2.0 distribution under **dynamic linking only** — we ship OCCT as a separate shared library, never statically linked into souxmar binaries. The exception text and our compliance posture are documented in a new top-level `LICENSING.md` (a deliverable of this RFC).

### New plugin type: `cad.*`

```toml
# examples/plugins/cad-occt/souxmar-plugin.toml (sketch)

[plugin]
id            = "dev.souxmar.examples.cad-occt"
name          = "OpenCASCADE CAD Kernel"
version       = "0.1.0"
abi           = 1
license       = "Apache-2.0"
min_souxmar_abi_minor = 6

[plugin.capabilities]
provides      = ["cad.occt", "reader.step", "reader.iges"]
# `reader.*` capabilities are preserved by delegating to the cad
# session: open, export snapshot via souxmar_brep_to_geometry, close.

[plugin.threading]
# OCCT's kernel state is per-session; one session per thread.
model         = "single-threaded-per-session"

[plugin.licensing]
# Spelled out because of the LGPL relink rights.
includes_dynamic_dep = ["OpenCASCADE >= 7.8 LTS"]
relink_rights_url    = "https://souxmar.dev/licensing/occt-relink"
```

The conformance suite gains a `cad.*` plugin type with tests for: open / close a session, import STEP, list bodies, export geometry snapshot (must equal the legacy `occt-reader` output bit-for-bit on the same input).

### C ABI: `include/souxmar-c/brep.h` (new)

`SOUXMAR_ABI_VERSION_MINOR` in `abi.h` bumps from **5** to **6**, with a new history line:

```
 *   v1.6  Sprint 28 push 1 — BREP session surface + cad.* plugin type
 *                            (souxmar-c/brep.h); ADR-0039.
```

Additive minor under the ADR-0008 ratchet. Existing plugins compiled against v1.0–v1.5 link and load unchanged. Function prototypes are bare (no `SOUXMAR_API` decoration), matching the existing `geometry.h` / `mesh.h` convention.

```c
/* SPDX-License-Identifier: Apache-2.0
 *
 * include/souxmar-c/brep.h — additive, v1.6 of the C ABI.
 *
 * A live BREP session owned by a cad.* plugin. The session is the
 * unit of mutation: feature operations (RFC-005) take a session +
 * arguments and either extend or modify the session's body graph.
 *
 * Bodies inside a session are identified by stable u64 IDs — stable
 * across edits within the same session, NOT promised across sessions
 * (re-importing a STEP file gives fresh IDs).
 *
 * Threading: a session is single-threaded. Multiple sessions in one
 * process are allowed; calls into one session from multiple threads
 * are not.
 */

#ifndef SOUXMAR_C_BREP_H
#define SOUXMAR_C_BREP_H

#include "abi.h"
#include "geometry.h"
#include "mesh.h"
#include "status.h"

SOUXMAR_C_BEGIN

typedef struct souxmar_brep_session_t souxmar_brep_session_t;

/* ---- Lifecycle ---- */

souxmar_brep_session_t* souxmar_brep_session_open(void);
void                     souxmar_brep_session_close(souxmar_brep_session_t* session);

/* ---- Import / export ---- */

souxmar_status_t souxmar_brep_import_step(souxmar_brep_session_t* session,
                                          const char*              path);

souxmar_status_t souxmar_brep_import_iges(souxmar_brep_session_t* session,
                                          const char*              path);

/* Snapshot the session's body graph into a souxmar_geometry_t. The
 * session retains ownership of its BREP; the geometry is a discrete
 * read-only view consumable by mesher plugins. Calling this is the
 * only legal way for a non-cad plugin to see CAD output. */
souxmar_geometry_t* souxmar_brep_to_geometry(const souxmar_brep_session_t* session);

/* ---- Body queries ---- */

size_t souxmar_brep_num_bodies(const souxmar_brep_session_t* session);

souxmar_status_t souxmar_brep_body_id(const souxmar_brep_session_t* session,
                                       size_t                         index,
                                       uint64_t*                      out_body_id);

souxmar_status_t souxmar_brep_body_bounds(const souxmar_brep_session_t* session,
                                           uint64_t                       body_id,
                                           double                         out_min[3],
                                           double                         out_max[3]);

const char* souxmar_brep_body_name(const souxmar_brep_session_t* session,
                                    uint64_t                       body_id);

/* ---- Tessellation (for the renderer; deflection in model units) ---- */

souxmar_status_t souxmar_brep_tessellate(souxmar_brep_session_t* session,
                                          uint64_t                 body_id,
                                          double                   deflection,
                                          souxmar_mesh_t**         out_surface_mesh);

SOUXMAR_C_END
#endif /* SOUXMAR_C_BREP_H */
```

Feature operations (`souxmar_brep_extrude`, `souxmar_brep_fillet`, etc.) are **not in this RFC** — they land in RFC-005 once the sketcher (RFC-004) gives them inputs. Keeping them out keeps RFC-003 reviewable in one pass.

### Mesher plugins: optional `cad_handle` input

Existing mesher plugins (e.g., `examples/plugins/gmsh-mesher`) accept a `souxmar_geometry_t` today. We extend the mesher vtable's input value-bag to optionally accept a `souxmar_brep_session_t*` + `body_id` pair under the keys `cad_session` / `cad_body_id`. Mesher plugins that ignore the pair fall back to the geometry snapshot — fully backward-compatible. Mesher plugins that consume the pair gain the ability to refine on the analytic surface (a Gmsh capability we are currently leaving on the table).

This is **not** a vtable signature change. The input value-bag is an existing extension point; we are only documenting two new well-known keys.

### Agent tool surface (this RFC, minimal)

Three additive tools, all in the typed surface, with confirmation policy per `docs/AI_INTEGRATION.md`:

| Tool                      | Confirmation    | Side effect                            |
| ------------------------- | --------------- | -------------------------------------- |
| `cad.import_step`         | `confirm-once`  | Disk read; mutates session body graph. |
| `cad.list_bodies`         | `auto`          | Read-only.                              |
| `cad.get_body_metadata`   | `auto`          | Read-only.                              |

`confirm-once` for `import_step` because path traversal is a real concern — the file path is user-controlled and we shouldn't silently open files the user didn't intend.

Feature-creation tools (`feature.extrude`, etc.) are RFC-005's surface and inherit `confirm-once` by default.

## Alternatives considered

### Alternative A: CGAL

CGAL (GPL or commercial dual-license; some packages LGPL) has excellent computational geometry primitives and is already mature.

Rejected because: (a) the BREP surface (`Polyhedron_3`, `Surface_mesh`) is discrete-only — CGAL is a great mesh/geometry library, not a CAD kernel. We'd be back to needing a *second* tool for parametric features. (b) The GPL parts would force us to either avoid them (limiting capability) or relicense; the dual-license commercial path is incompatible with Apache-2.0 distribution. Use CGAL for *specific computational geometry plugins* (boolean robustness, mesh repair) — not as the kernel.

### Alternative B: Manifold (https://github.com/elalish/manifold)

Apache-2.0 licensed, fast, robust boolean operations.

Rejected because: Manifold is a triangle-mesh boolean library, not a BREP kernel. No analytic surfaces, no STEP/IGES import, no parametric history. Excellent for what it does — boolean ops on tessellated input — and we *will* depend on it later for robust mesh booleans in a `feature.boolean_mesh` path. But not the kernel.

### Alternative C: Build a souxmar-native kernel

Write our own BREP kernel.

Rejected because: BREP kernel development is a five-engineer-year commitment minimum. The post-v1.0 block is sixteen weeks. Not an option for this block; revisit only if OCCT's license posture or upstream health degrades materially (R-016 in the risk register below).

### Alternative D: Use OCCT but statically link

Static linking would simplify packaging.

Rejected because: OCCT's LGPL-2.1 linking exception specifically preserves the right of downstream users to *relink* against a modified OCCT. Static linking removes that right de facto and we'd be in a licensing grey zone. Dynamic linking is the well-trodden path; the packaging overhead is acceptable (we already ship dynamic deps).

### (Considered and rejected: do nothing)

Without a kernel, Sprints 29–30 cannot ship. The parametric modeler is the load-bearing feature of v1.2; deferring this RFC is deferring the entire visible value of the post-v1.0 block.

## Drawbacks

- **OCCT distribution adds ~80 MB to the installer** across the three OSes. Mitigated by tree-shaking unused OCCT modules at build time; rough target is ~40 MB after pruning. Tracked against the existing installer-size budget.
- **OCCT API is large and idiomatic C++** — wrappers shelter plugin authors from the API surface, but the `cad-occt` plugin itself becomes a non-trivial codebase that needs maintainers. Hiring implication: one engineer on Adapters team needs to be OCCT-fluent or willing to become so.
- **LGPL relink-rights obligation** means we must publish OCCT build instructions + a reproducible build of our OCCT binary alongside every release. New release-process step; documented in `docs/LICENSING.md` and the `releasing-souxmar` skill.
- **One more dependency to track for CVEs.** OCCT has had a handful of vulnerabilities historically — folded into the existing security gate process.
- **The `cad.*` plugin type doubles as a kernel-shim type.** A future RFC may want a separate "kernel-binding" plugin type; we are deliberately collapsing it for now to keep the surface small.

## Migration plan

- **`examples/plugins/occt-reader`** becomes a thin wrapper around `cad-occt`: open session, import, snapshot to geometry, close. The plugin keeps its capability ID (`reader.step`, `reader.iges`) so any pipeline that names it continues to work. Move scheduled for the same PR series.
- **In-tree plugins consuming `reader.step`:** unaffected — the capability is preserved.
- **Out-of-tree plugins:** unaffected (additive ABI; no removed entry points).
- **Existing pipeline files on disk:** unaffected; pipelines reference plugins by capability, not by plugin ID.
- **Existing `examples/cantilever-beam`:** unaffected (no CAD step in the pipeline).
- **Saved chat sessions:** the three `cad.*` tools are additive; sessions referencing only v1.0 tools replay identically.
- **Documentation:** new `docs/LICENSING.md` for the OCCT distribution story; updates to `docs/PLUGIN_SDK.md` (new `cad.*` plugin type), `docs/AI_INTEGRATION.md` (three new tools), `docs/RELEASE_NOTES_TEMPLATE.md` (relink-rights line item).

## Pre-mortem

It is 2027-05-12. RFC-003 went badly. What happened:

OCCT 7.8's STEP importer had a regression on assembly files produced by a popular mid-market CAD tool. We discovered it when a customer tried to import their actual production model on day one of v1.2 GA and got an empty body graph with no error. Upstream patched in OCCT 7.9 three months later, but our LTS pin lagged the patch by six months because we hadn't established a "bump OCCT" cadence. Separately, the "snapshot to geometry" path turned out to be the dominant cost for large assemblies — re-snapshotting on every viewport refresh stalled the renderer. We bolted on an incremental-snapshot path in v1.2.1 that should have been in this RFC.

Leading indicators to watch in the first six months:

- Any user-reported "empty body graph after import" issue — implies upstream regression.
- Snapshot latency p95 on a 100-body assembly exceeds 500ms — implies the full-snapshot cost is real.
- No "bump OCCT" PR opened within 30 days of an OCCT upstream release.
- The conformance corpus contains only single-body STEP files.

## Open questions

1. **Body-ID stability across import re-runs.** Promising stable IDs across `import_step(path)` calls on the same path means we need a deterministic numbering scheme tied to STEP entity IDs. Probably yes; the sketcher (RFC-004) wants to reference faces stably. Document the promise; pick the hashing scheme during PR 1.
2. **Tessellation deflection default.** Per-body or session-wide? OCCT's `BRepMesh_IncrementalMesh` takes a linear + angular deflection. Defaults: linear = bbox-diagonal × 0.01, angular = 0.5 rad. Validate on `examples/pipe-bend`.
3. **Session-on-disk format.** Sessions need to persist for save/load. Store `*.brep` (OCCT-native) inside the project directory under `cad/<session_id>.brep`, indexed by `design.yaml` (RFC-005's territory)? Probably yes; nail in RFC-005.
4. **Memory caps.** Large STEP files can blow out memory. Add `souxmar_brep_session_set_memory_cap()` and return a typed error on overflow? Probably yes; details TBD.
5. **Threading on the host side.** "Single-threaded per session" is the rule, but the host dispatcher may want to spread *sessions* across threads. Document the contract; nothing for the ABI to do.
6. **OCCT version pin.** 7.8 LTS today; pick the highest stable LTS at Sprint 28 day 1; ratify in a one-paragraph ADR.

## Implementation plan

Four PRs in Sprint 28.

- [ ] **PR 1 — ABI add.** `include/souxmar-c/brep.h`; in-core stub backing (returns `SOUXMAR_E_NOT_IMPLEMENTED` until PR 2 supplies a real impl); `SOUXMAR_ABI_VERSION_MINOR` bump 5 → 6 with the new history line; conformance test scaffold for the `cad.*` plugin type. Commit marker `Ratchet: additive minor surface (ADR-0008)`. Reviewer: ABI gate.
- [ ] **PR 2 — `examples/plugins/cad-occt`.** Real OCCT session backing; STEP/IGES import; tessellation; conformance suite passes on the existing STEP corpus.
- [ ] **PR 3 — Reader rewrite + agent tools.** Convert `examples/plugins/occt-reader` to a thin wrapper; land `cad.import_step`, `cad.list_bodies`, `cad.get_body_metadata` in `libsouxmar-ai`; eval cases for each.
- [ ] **PR 4 — Mesher input.** Document the `cad_session` / `cad_body_id` value-bag keys; `examples/plugins/gmsh-mesher` consumes them when present and refines on the BREP analytic surface.
- [ ] New doc: `docs/LICENSING.md` (OCCT relink-rights compliance story).
- [ ] `releasing-souxmar` skill: add the OCCT-binary-publish step.
- [ ] ADR-0039 filed at `docs/adr/0039-abi-v1-6-brep-ratchet.md` — records the v1.6 minor bump under the ADR-0008 ratchet. Filed with PR 1.
- [ ] ADR filed at `docs/adr/NNNN-occt-version-pin.md` (Open question 6).
- [ ] `docs/PLUGIN_SDK.md` updated: new `cad.*` plugin type section.

## References

- `docs/SPRINT_PLAN.md` — Post-v1.0 plan, Sprint 28 row.
- `docs/rfcs/0004-2d-sketcher.md` — Builds on this surface (sketches anchor to BREP faces).
- `docs/rfcs/0005-feature-tree.md` — Builds on this surface (`feature.extrude` etc. mutate the session).
- `docs/rfcs/0001-viewport-renderer.md` — Tessellated CAD bodies feed the renderer's surface stream.
- `docs/rfcs/0002-field-stream-protocol.md` — Sibling RFC; same shape of additive ABI add.
- `include/souxmar-c/geometry.h` — The discrete snapshot type `cad.*` plugins still produce.
- `examples/plugins/occt-reader/` — Existing reader; becomes the thin wrapper.
- `docs/PLUGIN_SDK.md` — Plugin-type taxonomy.
- OCCT 7.8 LTS release notes — TBD link at Sprint 28 day 1.
- OCCT linking exception text — TBD link in `docs/LICENSING.md`.
