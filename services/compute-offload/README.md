# compute-offload — Pro-tier hosted compute

Sprint 17 push 3 scaffold. ADR-0027 documents the architecture.

## What this is

A job queue + worker pool that accepts `solve` stages from
the desktop / CLI and runs them on managed HPC. The user's
local machine submits the mesh + inputs, the service runs
the solver, the user's machine polls for + downloads the
resulting field handle.

## Endpoints (v1)

- `POST /v1/jobs` — submit a stage for remote execution.
- `GET  /v1/jobs/{id}` — poll status + retrieve completed
  output.
- `DELETE /v1/jobs/{id}` — cancel a queued or running job
  (refunds the core-hour estimate per ADR-0024 § 3
  in-flight holds).

Auth: `sxm_pro_*` token (same as the proxy + cloud-sync).

## Status

MVP scaffold. Today's binary binds + 503s. Sprint 19 push 2
lands the real worker pool + storage backend.

— Sprint 17 push 3.
