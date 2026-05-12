# ADR-0030: Lightweight in-app geometry edits

- **Status:** Accepted
- **Date:** 2026-05-17 (Sprint 19 push 1)
- **Author:** souxmar geometry team
- **Deciders:** desktop, AI, plugin host
- **Tier:** 2 (the edit-operation set is a stable contract;
  adding operations is non-breaking, removing is Tier-2)
- **Affects:** new `include/souxmar/geometry/edit_op.h`;
  `souxmar::core::Geometry` gains an `apply_edit(EditOp)`
  member; the agent tool catalogue gains nothing (the editor
  is a UI surface, not an agent tool — explicitly).

## Context

SPRINT_PLAN.md § Sprint 19 names "lightweight in-app geometry
edits (reposition, suppress, parameter tweaks)" + explicitly
"not parametric modelling." Sprint 19 (this push) ratifies what
the surface is + what it isn't.

The "not parametric modelling" line matters: parametric CAD is
multiple sprints + a CAD kernel decision (OpenCASCADE supports
parameters; the souxmar geometry layer doesn't expose them
today). Sprint 19's scope is the *user-visible* surface that
exists today (reposition / suppress / dimension tweak) +
nothing else.

## Decision

### EditOp surface — three operations

```cpp
namespace souxmar::geometry {

enum class EditOpKind : std::uint8_t {
  Reposition  = 0,    // translate a body or feature by (dx, dy, dz)
  Suppress    = 1,    // toggle a feature's visibility-in-mesh
  Dimension   = 2,    // overwrite a numeric dimension parameter
};

struct EditOp {
  EditOpKind                 kind;
  std::string                target_id;        // entity id (face / body / parameter)
  std::array<double, 3>      vec;              // Reposition: dx,dy,dz; Dimension: [new_value, 0, 0]; ignored for Suppress
  bool                       boolean_state;    // Suppress: true=suppressed; ignored otherwise
};

// Applied on souxmar::core::Geometry. Returns success or a
// typed error.
enum class EditOpError {
  TargetNotFound,
  KindNotSupported,
  ParameterOutOfRange,
  Internal,
};

[[nodiscard]] std::variant<std::monostate, EditOpError>
apply_edit_op(souxmar::core::Geometry& geom, const EditOp& op);

}
```

### What this ADR does NOT cover

- **Boolean operations** (union / subtract / intersect). Need a
  CAD-kernel-level decision; queued for post-v1.0.
- **Parametric history.** No timeline of edits; each EditOp is
  applied destructively to the geometry. The desktop client
  shows an "undo" stack via the cloud-sync project-version
  history (Sprint 20+); the engine itself is stateless.
- **Sketch-based edits.** Drawing a new edge, splitting a face
  — post-v1.0.
- **Agent-driven edits.** The agent tool catalogue is **frozen
  final at v1 with 18 tools** (ADR-0011). No
  `edit_geometry` tool. The user drives edits through the
  desktop's geometry panel; the agent can describe what to do
  but not execute. This is intentional — geometry edits are
  user-intent-load-bearing; the agent assisting is fine; the
  agent executing requires a higher safety bar that Sprint
  20+ revisits.

### Persistence

Edit operations don't persist independently — they're applied
to the in-memory Geometry. The desktop saves the modified
project after applying. The cloud-sync project history
(Sprint 20+) is the audit trail.

### Why a small operation set rather than a full CAD API

- **The 80% case is reposition + suppress.** A user repositioning
  an inlet face by 5mm or suppressing a bolt-head for meshing
  covers the common ergonomic gap today.
- **CAD-kernel-complete is multiple sprints + an ADR-fight per
  operation set.** Sprint 19's scope intentionally avoids that
  fight.
- **The plugin C ABI doesn't need to change.** Geometry passed
  to a plugin is the post-edit Geometry; the plugin sees what
  the user committed.

## Consequences

- New `include/souxmar/geometry/edit_op.h` (declarations only
  in this push; implementation lands alongside the desktop
  geometry panel in Sprint 22's beta).
- The desktop's Inspector / Viewport panels gain an "Edit" mode
  in Sprint 22 push 1.
- ADR-0011 (tool contract v1 frozen final) is NOT touched —
  no edit tool joins the catalogue.

— Sprint 19 push 1.
