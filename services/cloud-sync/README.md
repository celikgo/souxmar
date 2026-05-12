# cloud-sync — Pro-tier project synchronisation

Sprint 15 push 3 scaffold. Architecture in
[ADR-0021](../../docs/adr/0021-cloud-sync-architecture.md);
this README is the operator's quick reference.

## What this is

A stateful HTTP service that stores per-user project files
(pipeline.yaml, project.souxmar.toml, audit/budget state,
inputs/, capped outputs). Pro / Team tiers see encryption-at-
rest; Enterprise sees end-to-end encryption (server stores
ciphertext only; Sprint 20).

## What this is NOT

- Not a general-purpose file store. The set of synced surfaces is
  fixed (see ADR-0021 § 1).
- Not a substitute for git. Users who want full version history
  pair cloud sync with a git workflow; sync is for "last few
  versions across machines."
- Not the agent dispatcher's cache directory. Plugin host caches
  stay local; they're cheap to regenerate.

## Endpoints (v1)

See `api/openapi.yaml`:

- `PUT  /v1/project/<id>/file/<name>` — upload one file.
- `GET  /v1/project/<id>/file/<name>` — download one file.
- `GET  /v1/project/<id>/manifest`    — list files + per-file
  server timestamps for conflict detection.

Auth: shared `sxm_pro_<...>` token validated against the same
account portal as the managed-AI proxy (ADR-0019 + ADR-0021).
Each tier gets a different per-request quota (Pro: 1 GB total /
month upload; Team: 10 GB; Enterprise: 100 GB).

## Status

**MVP scaffold.** Today's binary binds + returns 503 with
"MVP — Sprint 16+ implementation pending" on every endpoint.
Same shape as the managed-AI proxy's Sprint 14 push 3 scaffold.

Sprint 16 push 1 lands the S3-backed write path.
Sprint 17 lands the conflict-panel signal.
Sprint 20 lands Enterprise E2E.

## Build + run

```sh
cd services/cloud-sync
cargo build --release
./target/release/cloud-sync --addr 127.0.0.1:8081
```

The binary is **not** part of the workspace (same reason as
the managed-ai-proxy). The services-build CI workflow
(Sprint 15 push 1) compiles every Rust service under
`services/*/` to catch link breaks early.

## Why a different port from the proxy

Cloud sync and the managed-AI proxy run as separate services
(ADR-0021 § 4). Different deploy story, different scale
profile. In production, separate load balancers; in dev,
separate ports (8080 / 8081) so a developer can run both
locally.

## Sprint 15 acceptance

This push lands the scaffold and the ADR. Acceptance:

- `cargo build` succeeds locally.
- The binary binds + responds 503 to every endpoint.
- The OpenAPI contract is consistent with ADR-0021's
  "what gets synced" list.

It does **not** yet:

- Persist files anywhere — Sprint 16 wires S3.
- Validate tokens against the account portal — Sprint 17.
- Encrypt — Sprint 16 (Pro/Team rest-only) + Sprint 20 (E2E).

— Sprint 15 push 3.
