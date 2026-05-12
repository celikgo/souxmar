# RFC 0005: Parametric feature tree — `design.yaml` v1 + replay semantics

- **Author:** celikgokhun
- **Status:** Draft
- **Tracking issue:** TBD — file at Sprint 30 kickoff
- **Affects:** ABI, agent tool contract, on-disk format (new `design.yaml`)
- **Tier:** 3
- **Date opened:** 2026-05-12
- **Date `final-comment` started:** —
- **Date accepted / rejected:** —

## Summary

Introduce `design.yaml` — a single project-level file recording the ordered list of feature operations that produce the part. Each feature is a typed record (extrude / revolve / fillet / boolean / pattern / …) referencing inputs by stable ID (sketch IDs from RFC-004, body/face/edge IDs from RFC-003). Replaying the file against a fresh BREP session is deterministic: same `design.yaml` + same input sketches + same OCCT version → same resulting bodies. The C ABI extends the existing `brep.h` (introduced in RFC-003) with feature-operation entry points (additive minor bump v1.7 → v1.8 under the ADR-0008 ratchet — assumes RFCs 001-004 have landed). The agent surface gains ten `feature.*` tools. The workbench renders a feature tree panel with edit / suppress / reorder / delete + undo/redo.

## Motivation

RFC-003 stood up the kernel; RFC-004 produced sketches. RFC-005 closes the loop: take sketches + features, persist them, replay them, edit them. Without this surface:

- Sprint 30's exit criterion ("build a parametric bracket from a sketch via UI **and** via chat; edit a dimension and watch the model regenerate") cannot ship.
- The agent's "design a bracket" eval case has no place to land the multi-step authoring path.
- Users cannot share parametric parts — only static STEP exports. The collaboration story drops to the same level as proprietary kernels' export-only path.

Determinism is load-bearing: the agent must be able to author a design, save it, and have a human re-open it and see the same body. The same `design.yaml` opened on a different machine with the same OCCT version must produce the same BREP. Without this, the agent's "edit your sketch and regenerate" loop devolves to "regenerate something *like* your sketch."

## Proposal

Four layers, top-down.

### 1. On-disk format: `design.yaml` v1

```yaml
# design.yaml — v1 frozen at v1.2 release.
#
# Replay semantics: features execute in the order listed against an
# initially empty BREP session. Each feature's inputs reference IDs
# produced by earlier features OR by external artefacts (sketches in
# <project>/sketches/, STEP files imported at the top of the tree).
#
# Versioning: the `version` key is the format version, not the project
# version. Format changes require a Tier-3 RFC and a major bump (v2).

version: 1
units:      mm                 # "mm" | "cm" | "m" | "in" | "ft"
angle_unit: deg                # "deg" | "rad"

# Top-level imports — produce initial bodies that subsequent features
# can reference. Empty list is valid (everything is sketch-driven).
imports:
  - id: import_1
    kind: step
    path: refs/bracket-bolt.step      # relative to project root

# Ordered feature list. Each feature has a stable `id` — chat-driven
# edits address features by ID. IDs are user-readable; defaults are
# kind + sequence number ("extrude_1", "fillet_2").
features:
  - id: extrude_1
    kind: extrude
    sketch: sketches/profile.sketch.yaml
    distance: 10.0               # mm; or a parameter reference
    direction: normal            # "normal" | "reversed" | "symmetric"
    operation: new_body          # "new_body" | "add" | "subtract" | "intersect"
    suppressed: false

  - id: fillet_1
    kind: fillet
    edges:                       # references into the current body graph
      - body: extrude_1
        edges: [4, 5, 12]
    radius: 2.0
    suppressed: false

  - id: pattern_1
    kind: pattern_linear
    features: [extrude_1]
    direction: [1.0, 0.0, 0.0]
    spacing: 25.0
    count: 4
    suppressed: false

# Named parameters — the only thing the agent (or the user) edits
# routinely. Feature fields reference parameters by `$name`.
parameters:
  bracket_height:
    value: 10.0
    unit: mm
```

### Replay semantics — strict and deterministic

1. **Imports execute first**, in listed order. Each produces a body with a stable ID (the `imports[i].id`).
2. **Features execute in listed order.** A suppressed feature is skipped — its outputs do not exist for downstream features. Suppressed-but-not-deleted is the supported "comment out" workflow.
3. **Each feature consumes inputs by stable ID**: a previous feature's `id`, an import's `id`, or a sketch file path. Resolution is name-based; renaming a feature is a graph mutation and must update all downstream references (the host does this automatically).
4. **Topology references** (body / face / edge IDs) inside features are *resolved against the BREP session at the moment that feature replays*. Face IDs come from RFC-003's "stable across edits within a session" promise. Re-running with a different OCCT version may break references — same-OCCT-version is the determinism contract; cross-version is a separate problem (see Open questions).
5. **Failures are typed.** A feature whose inputs no longer resolve (e.g., a fillet on an edge that doesn't exist after an earlier sketch edit) fails with `SOUXMAR_E_DANGLING_REFERENCE`; downstream features are skipped; the feature tree UI surfaces the broken feature with a red marker.

This is the same "regenerate from history" semantics as every parametric CAD tool — Onshape, Fusion, FreeCAD, Inventor. The promise is conventional; the implementation has to be precise.

### 2. C ABI extension: feature ops in `brep.h`

`SOUXMAR_ABI_VERSION_MINOR` in `abi.h` bumps from **7** to **8**, with a new history line:

```
 *   v1.8  Sprint 30 push 1 — BREP feature ops (extend brep.h);
 *                            ADR-0041.
```

Additive minor under the ADR-0008 ratchet. Existing plugins compiled against v1.0–v1.7 link and load unchanged. Function prototypes are bare (no `SOUXMAR_API` decoration), matching the rest of `brep.h`.

Adds ~10 feature-op entry points to `include/souxmar-c/brep.h`:

```c
/* Each feature op mutates the session, returning the resulting body
 * ID (or SOUXMAR_INVALID_ID + status on failure). Caller is the host
 * replayer; plugin authors should not call these directly. */

uint64_t souxmar_brep_extrude(souxmar_brep_session_t* session,
                               const souxmar_sketch_t* sketch,
                               double                   distance,
                               uint8_t                  direction, /* 0/1/2 */
                               uint8_t                  operation, /* 0..3 */
                               uint64_t                 target_body_id, /* 0 for new_body */
                               souxmar_status_t*        out_status);

uint64_t souxmar_brep_revolve(souxmar_brep_session_t* session,
                               const souxmar_sketch_t* sketch,
                               uint32_t                 axis_line_id,
                               double                   angle_rad,
                               uint8_t                  operation,
                               uint64_t                 target_body_id,
                               souxmar_status_t*        out_status);

uint64_t souxmar_brep_sweep(souxmar_brep_session_t* session,
                             const souxmar_sketch_t* profile,
                             const souxmar_sketch_t* path,
                             uint8_t                  operation,
                             uint64_t                 target_body_id,
                             souxmar_status_t*        out_status);

uint64_t souxmar_brep_loft(souxmar_brep_session_t*  session,
                            const souxmar_sketch_t**  profiles, /* array */
                            size_t                     profile_count,
                            uint8_t                    operation,
                            uint64_t                   target_body_id,
                            souxmar_status_t*          out_status);

souxmar_status_t souxmar_brep_fillet(souxmar_brep_session_t* session,
                                      uint64_t                 body_id,
                                      const uint64_t*          edge_ids,
                                      size_t                   edge_count,
                                      double                   radius);

souxmar_status_t souxmar_brep_chamfer(souxmar_brep_session_t* session,
                                       uint64_t                 body_id,
                                       const uint64_t*          edge_ids,
                                       size_t                   edge_count,
                                       double                   distance);

souxmar_status_t souxmar_brep_boolean(souxmar_brep_session_t* session,
                                       uint64_t                 target_body_id,
                                       uint64_t                 tool_body_id,
                                       uint8_t                  operation /* 1..3 */);

souxmar_status_t souxmar_brep_pattern_linear(souxmar_brep_session_t* session,
                                              const uint64_t*          source_body_ids,
                                              size_t                   source_count,
                                              const double             direction[3],
                                              double                   spacing,
                                              uint32_t                 count);

souxmar_status_t souxmar_brep_pattern_circular(souxmar_brep_session_t* session,
                                                const uint64_t*         source_body_ids,
                                                size_t                  source_count,
                                                const double            axis_origin[3],
                                                const double            axis_direction[3],
                                                double                  angle_rad,
                                                uint32_t                count);

souxmar_status_t souxmar_brep_delete_body(souxmar_brep_session_t* session,
                                           uint64_t                 body_id);
```

The implementation lives in the OCCT-bound plugin (`cad-occt`); the host calls these entry points via the plugin's vtable per the `cad.*` plugin contract from RFC-003.

### 3. Bridge surface

```rust
// src-tauri/souxmar-bridge/src/design.rs (sketch)

impl Bridge {
    pub fn design_open(&self, project_id: &str) -> Result<DesignSummary, BridgeError>;
    pub fn design_save(&self, project_id: &str) -> Result<(), BridgeError>;

    pub fn feature_add(&self, project_id: &str, kind: &str, params: serde_json::Value)
        -> Result<String, BridgeError>;          // returns feature_id

    pub fn feature_edit(&self, project_id: &str, feature_id: &str, params: serde_json::Value)
        -> Result<(), BridgeError>;

    pub fn feature_suppress(&self, project_id: &str, feature_id: &str, suppressed: bool)
        -> Result<(), BridgeError>;

    pub fn feature_delete(&self, project_id: &str, feature_id: &str)
        -> Result<(), BridgeError>;

    pub fn feature_reorder(&self, project_id: &str, feature_id: &str, new_index: u32)
        -> Result<(), BridgeError>;

    pub fn undo(&self, project_id: &str) -> Result<(), BridgeError>;
    pub fn redo(&self, project_id: &str) -> Result<(), BridgeError>;

    pub fn parameter_set(&self, project_id: &str, name: &str, value: f64)
        -> Result<(), BridgeError>;

    pub fn regenerate(&self, project_id: &str) -> Result<RegenerateSummary, BridgeError>;
}
```

Every mutating call triggers an immediate replay-from-affected-feature (incremental regenerate; the host tracks dependencies). The renderer's surface-stream handle (RFC-001) invalidates automatically when a body's BREP changes.

### 4. React side: feature tree panel + undo

A new panel `src/desktop/src/workbench/FeatureTree.tsx` in the Inspector area renders the feature list with: rename, suppress, reorder (drag), delete. Selecting a feature highlights its output body in the viewport. A parameter spreadsheet renders alongside.

Selection promotion during a feature dialog: when the user clicks "Add Fillet" and the dialog asks for edges, clicking edges in the viewport (RFC-001's picking) injects their IDs into the dialog. The same pattern applies to face / vertex selection in other ops.

Undo / redo: every mutating bridge call records to a per-project undo stack. Replay through the same code path keeps the BREP session and the on-disk `design.yaml` in lockstep.

### Agent tool surface (10 tools, all `confirm-once`)

| Tool                          | Purpose                                              |
| ----------------------------- | ---------------------------------------------------- |
| `feature.extrude`             | Add an extrude feature.                              |
| `feature.revolve`             | Add a revolve feature.                               |
| `feature.sweep`               | Add a sweep feature.                                 |
| `feature.loft`                | Add a loft feature.                                  |
| `feature.fillet`              | Add a fillet on a set of edges.                      |
| `feature.chamfer`             | Add a chamfer.                                       |
| `feature.boolean`             | Add a boolean op (union / subtract / intersect).      |
| `feature.pattern_linear`      | Linear pattern of a feature.                          |
| `feature.pattern_circular`    | Circular pattern of a feature.                        |
| `feature.set_parameter`       | Set a named parameter; triggers regenerate.           |

Each ships with at least one eval case. The "design a bracket" end-to-end eval case exercises all ten in sequence.

## Alternatives considered

### Alternative A: Direct modeling — no history, no replay

Skip the feature tree; let users edit BREP directly and persist only the final body.

Rejected because: direct modeling is a valid mode but it's a *secondary* one — without history, dimensions are unmaintained and the agent's "edit your sketch and regenerate" loop becomes "redo your work." Every modern parametric CAD tool offers direct modeling alongside parametric, not instead of. Add direct-modeling later if user demand surfaces; ship parametric in v1.2.

### Alternative B: JSON instead of YAML

The rest of souxmar's on-disk formats are YAML (`pipeline.yaml`, `*.sketch.yaml`). Sticking with YAML maintains consistency; JSON would force a per-format-type stance. Rejected.

### Alternative C: Imperative script instead of declarative tree

Persist designs as a Python script that calls the kernel ABI directly. Replay = run the script.

Rejected because: (a) scripts are not safely editable by an agent at the structural level (parsing-and-emitting Python AST is fragile); (b) determinism becomes a function of arbitrary user code, not a typed tree; (c) the format becomes load-bearing on a specific language runtime. YAML + a finite vocabulary of feature kinds keeps the agent and the human author working in the same surface.

### (Considered and rejected: do nothing)

Without a feature tree, v1.2 cannot ship its load-bearing feature. The post-v1.0 block fails its mid-point milestone.

## Drawbacks

- **`design.yaml` is the new highest-stakes frozen format.** A bug in v1.0 of this schema costs ten years (until v2.x ABI). The Tier-3 RFC review must be thorough; the conformance corpus must be unusually large.
- **Replay performance is the dominant interactive perf concern.** Editing the second-from-top feature of a 50-feature design re-runs 48 features. Mitigation: incremental replay (cache feature outputs and dependency graph); spec'd in PR 4.
- **Topology-reference stability is hard.** OCCT's face/edge IDs are stable within a session but not across kernel-version bumps. We accept "same kernel version → deterministic" and add a "migrate to new kernel version" tool as a follow-up; not in this RFC.
- **The 10 agent tools cross the line from "obvious tool surface" into "model what a CAD operation actually means."** Schema design for `feature.fillet`'s edge-selection field is non-trivial.
- **Undo/redo across the parametric graph has corner cases.** Reorder + undo + suppress = behaviour-defined-by-implementation if we're not careful. Test corpus has to cover this explicitly.

## Migration plan

- **Existing projects (pre-design.yaml):** no `design.yaml` present → workbench opens in non-parametric mode; the existing `pipeline.yaml` still drives the mesh/solve path. Adding parametric is a user action ("add a design"), not an automatic migration.
- **Existing `pipeline.yaml`:** unaffected. A project that has both `design.yaml` and `pipeline.yaml` references the design's output body as the geometry input to the pipeline's first mesher stage.
- **Existing in-tree / out-of-tree plugins:** unaffected (additive ABI).
- **Saved chat sessions:** the 10 `feature.*` tools are additive; older sessions replay identically.
- **Documentation:** new `docs/tutorials/modeling-a-bracket.md`; `docs/DESKTOP_APP.md` ("Feature tree" section); `docs/AI_INTEGRATION.md` (10 new tools); update `docs/PLUGIN_SDK.md` (note `design.yaml`'s role); new ADR for the format freeze.

## Pre-mortem

It is 2027-05-12. RFC-005 went badly. What happened:

The "stable face IDs within a session" promise turned out to be weaker than we needed. OCCT 7.9 (which we bumped to in v1.2.3 for a security fix) changed its face-numbering for `BRepAlgoAPI_Cut` outputs, and any `design.yaml` that filleted an edge produced by a cut feature broke silently — the fillet attached to the *wrong* edge instead of failing loudly. Users reported "my bracket has the wrong corner rounded." We added a check ("verify topology references resolve to the same geometric location they did at save time, within tolerance"), but the migration of saved designs took months. Separately, the parameter-spreadsheet UX let users enter `bracket_height = 0` and the regenerate failed in OCCT's `BRepPrimAPI_MakePrism` with an unhelpful exception — we should have added a parameter-validation step in the host before calling the kernel.

Leading indicators to watch in the first six months:

- Any user-reported "wrong edge filleted" or "feature attached to wrong face" issue.
- OCCT version bump PRs that don't include a regression suite run on a corpus of designs from v1.2 GA.
- Parameter values of 0 / negative / NaN reaching the kernel.
- Any single feature failing in > 1% of dogfood regenerates — implies the feature's input schema is fragile.

## Open questions

1. **Topology reference robustness.** The current promise ("stable within a session") may not be enough for designs that re-import STEP after the first save. Add a "fingerprint" alongside the ID (face centroid + normal) so a kernel bump can re-bind references heuristically? Lean yes; details TBD.
2. **Parameter expressions.** Today's parameter is a scalar value. Real CAD tools support expressions (`bracket_height = bolt_diameter * 1.5`). Add now or post-v1.2? Lean later; ship the scalar version in v1.2 and treat expressions as a v1.3 RFC.
3. **Multi-body designs.** Two solids in the same `design.yaml` — fine, supported (`new_body` operation). Multi-body designs as assemblies (with mates/joints) is a separate, larger problem; punt to a future RFC.
4. **Configurations / variants.** "Show me this bracket in three thicknesses" — a v1.3 feature; not in this RFC.
5. **External references.** Can `design.yaml` import another project's `design.yaml`? Probably yes eventually; not in v1.
6. **Undo granularity.** Every parameter change is one undo step? Every primitive add is one? Lean "every bridge call is one undo step"; document the rule.
7. **Format extensibility within v1.x.** New feature kinds added in v1.3 — additive YAML keys readable by v1.2? Yes; specify the forward-compat behaviour (unknown feature kinds load as "skipped + flagged" rather than fail).

## Implementation plan

Seven PRs in Sprint 30 — the most PR-heavy sprint of the post-v1.0 block.

- [ ] **PR 1 — ABI extension.** Feature-op entry points added to `brep.h`; in-core stubs returning `SOUXMAR_E_NOT_IMPLEMENTED`; `SOUXMAR_ABI_VERSION_MINOR` bump 7 → 8 with the new history line; conformance scaffold. Commit marker `Ratchet: additive minor surface (ADR-0008)`. Reviewer: ABI gate.
- [ ] **PR 2 — `cad-occt` feature ops.** Real OCCT implementations of extrude / revolve / sweep / loft / fillet / chamfer / boolean / patterns; per-op golden tests against reference output.
- [ ] **PR 3 — `design.yaml` schema + replayer.** Format parser; replay engine; incremental regenerate with dependency tracking. Conformance corpus of 20+ test designs.
- [ ] **PR 4 — Bridge surface.** Rust commands; Tauri registration; undo/redo stack.
- [ ] **PR 5 — Feature tree UI.** Panel; drag-reorder; rename/suppress/delete; selection promotion in feature dialogs.
- [ ] **PR 6 — Parameter spreadsheet.** Read/write parameters; trigger regenerate; validation (no zero/negative on dimensional inputs by default).
- [ ] **PR 7 — Agent tools + eval.** 10 `feature.*` tools; eval cases for each; the "design a bracket" end-to-end case.
- [ ] ADR-0041 filed at `docs/adr/0041-abi-v1-8-feature-ops-ratchet.md` — records the v1.8 minor bump under the ADR-0008 ratchet. Filed with PR 1.
- [ ] ADR filed at `docs/adr/NNNN-design-yaml-v1-freeze.md`.
- [ ] Documentation: tutorial "Modeling a bracket"; chat-driven variant; `docs/DESKTOP_APP.md` + `docs/AI_INTEGRATION.md` updates.
- [ ] `v1.2` release notes drafted.

## References

- `docs/SPRINT_PLAN.md` — Post-v1.0 plan, Sprint 30 row (v1.2 release).
- `docs/rfcs/0003-cad-kernel.md` — BREP session this RFC mutates.
- `docs/rfcs/0004-2d-sketcher.md` — Sketches consumed as feature inputs.
- `docs/rfcs/0001-viewport-renderer.md` — Renderer invalidates on body change.
- `docs/AI_INTEGRATION.md` — Tool-surface contract.
- `docs/PLUGIN_SDK.md` — Plugin-type taxonomy and on-disk format taxonomy.
- `include/souxmar-c/brep.h` — Header this RFC extends.
- Onshape / Fusion / FreeCAD parametric model docs — external precedent for the replay-from-history semantics. TBD links.
