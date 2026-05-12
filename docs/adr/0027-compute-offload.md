# ADR-0027: Hosted compute offload — Pro-tier solve-stage routing

- **Status:** Accepted
- **Date:** 2026-05-15 (Sprint 17 push 3)
- **Author:** souxmar platform team
- **Deciders:** platform, security review
- **Tier:** 1 (architecture)
- **Affects:** new `services/compute-offload/`; per-project pipeline
  YAML gains an `execution.target` field; the engine's runner
  detects + routes when target is "managed".

## Context

BUSINESS_MODEL.md § Pro tier names "hosted compute offload" as
the third Pro-tier deliverable (alongside managed-AI and cloud
sync). A user with a large CFD solve can submit the `solve`
stage to a managed HPC backend; the local desktop streams
results back without holding the user's machine for hours.

Sprint 17's exit criterion is the POC architecture; the actual
runner accumulates through Sprints 18-19.

## Decision

### Per-stage routing via `execution.target`

```yaml
version: 1
stages:
  - id: mesh
    plugin: mesher.tetra.gmsh
    # no execution block → local (default)
  - id: solve
    plugin: solver.cfd.openfoam
    execution:
      target:   "managed"          # or "local" (default)
      timeout:  3600                # seconds
      capacity: { cpu_cores: 16, memory_gb: 64 }
    input:
      mesh: { from: mesh }
```

Local execution is unchanged from today. `target: "managed"`
routes the stage's `Mesh` + `Field` handles + `pipeline::Value`
inputs through `compute-offload` service.

### Wire-protocol

The desktop client (or CLI) `POSTs` to `compute.souxmar.dev/v1/jobs`:

- Request: `{ project_id, stage_id, plugin_id, inputs_json,
  mesh_handle_blob, capacity }`. Mesh + Field handles are
  serialised via the souxmar-c-bridge's existing `mesh_buffer`
  surface (Sprint 5+).
- Response: `{ job_id, status: "queued" }`.
- Client polls `GET /v1/jobs/{job_id}` for status + (on
  success) `field_handle_blob` to load locally.

### What stays local

- Geometry / mesh stages (the file sizes are smaller than the
  data the solve would consume).
- Postproc / writer stages (the field is local once retrieved).
- Anything the user marks `execution.target: "local"` explicitly.

### Quota model

Per-tier hosted compute is metered by `core-hour` consumption:

| Tier        | Monthly allotment       | Overage rate     |
| ----------- | ----------------------- | ---------------- |
| Free        | 0 (not available)       | n/a              |
| Pro         | 100 core-hours          | $0.20 / core-hr  |
| Team        | 1000 core-hours / seat  | $0.18 / core-hr  |
| Enterprise  | custom                  | negotiated       |

Aligned with ADR-0024's billing service.

## Consequences

- New `services/compute-offload/` scaffold.
- The engine's pipeline runner gains an `ExecutionRouter`
  hook that decides between local-dispatch + remote-submit
  based on the stage's `execution.target`.
- Sprint 19 push 1 lands the engine-side router; Sprint 19
  push 2 lands the compute-offload service's real job queue
  + worker pool.
- Sprint 22's public beta exercises the path in stub-only
  mode (returns synthetic results for testing the protocol)
  before any real HPC backend wires up.

## Risks

- **R-033 (HPC backend cost overruns).** A buggy solver
  stage can consume unbounded core-hours. **Mitigation:**
  per-stage timeout + capacity caps are mandatory; the
  service rejects unbounded jobs.
- **R-034 (data-exfil via mesh upload).** A user submits a
  proprietary mesh to the managed backend; the backend
  operator (us) could in theory access it. **Mitigation:**
  encryption-at-rest on the backend storage (same posture
  as cloud-sync Pro tier); the Enterprise tier's HPC
  backend uses customer-tenant isolation per ADR-0028
  candidate (Sprint 20).

— Sprint 17 push 3.
