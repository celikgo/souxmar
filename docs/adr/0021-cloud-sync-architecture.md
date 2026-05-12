# ADR-0021: Cloud sync — Pro tier project synchronisation

- **Status:** Accepted
- **Date:** 2026-05-13 (Sprint 15 push 3)
- **Author:** souxmar platform team
- **Deciders:** desktop, platform, security review
- **Tier:** 1 (architecture — names what gets synced, how, and
  where; the concrete implementation accumulates through
  Sprints 15-17)
- **Affects:** new `services/cloud-sync/` directory tree
  (scaffold this push); the desktop client's Sprint 16+ "sync
  status" surface; the proxy's auth model (shared souxmar tokens
  → both proxy + cloud-sync namespaces).

## Context

`BUSINESS_MODEL.md` § "Pro tier" lists "cloud sync — your
projects available across machines" as one of three Pro-tier
deliverables (alongside managed-AI and hosted compute). Sprint
14 push 3's ADR-0019 stood up the managed-AI proxy; Sprint 15
push 3 (this push) lands the architecture for cloud sync.

Sprint 15's exit criterion per SPRINT_PLAN.md § Sprint 15 is
"Cloud sync MVP for Pro tier; encrypted-at-rest, end-to-end if
Enterprise." This ADR names the architecture; the
implementation lands in Sprint 16 (server-side write path),
Sprint 17 (client-side reconciliation), Sprint 20
(Enterprise-tier end-to-end encryption).

Decisions worth recording rather than discovering in the impl:

1. **What gets synced.**
2. **Conflict-resolution model.**
3. **Encryption posture per tier.**
4. **Separate service from the managed-AI proxy.**
5. **No cloud sync for Free tier (and how the desktop client
   communicates that).**

## Decision

### 1. What gets synced

Projects under the user's "synced projects" list. Per-project,
the synced surface is:

- `pipeline.yaml` and `project.souxmar.toml` (the project
  definition).
- The agent's audit-log entries (audit.toml).
- The agent's budget state (budget.toml).
- Any user-authored YAML/JSON under `<project>/inputs/`.
- The latest `<project>/outputs/*.vtu` (capped at 100MB per
  project; ADR-0022 candidate if the cap binds).

What is **not** synced:

- `project.ai.toml` (ADR-0020 — secrets-adjacent, gitignored;
  cloud sync respects the same boundary). Users who want
  their provider config replicated edit the file twice. The
  alternative — server-side stash of secrets-adjacent
  preferences — punts on R-019's existence.
- BYOK secrets (in the OS keychain; never leave the device).
- `build/` artefacts (re-generatable from the synced inputs).
- The plugin host's cache directory (re-fillable on demand).

### 2. Conflict resolution: last-write-wins per file, alert UI on conflicts

Two machines editing the same project file simultaneously is
expected (the user opens a project from their laptop after
last working on it from the desktop). The MVP uses **last-
write-wins per file** plus an explicit "conflict detected"
panel in the desktop client when the server's version is
newer than the local checkout.

Reasons:

- **Three-way merge requires a content-aware merger per file
  type** (YAML, TOML, binary VTU). Building three of those is
  multiple sprints; punting solves the 95% case (same user,
  different machine, infrequent overlap) immediately.
- **The conflict UI is honest.** "Server has a newer version
  of pipeline.yaml; keep yours / take theirs / open both"
  exposes the choice to the user instead of attempting a
  silent merge.
- **Per-file timestamping** uses the server's RFC-3339
  monotonic clock, not the client's. Clock-skew between
  machines doesn't cause silent flips.

Sprint 18+ revisits if user feedback shows the
last-write-wins choice causes data loss often enough; the
candidate replacement is a YAML-aware three-way merger for
the project-definition files only (binary outputs stay
last-write-wins).

### 3. Encryption posture per tier

| Tier        | Encryption posture                                                                                  |
| ----------- | --------------------------------------------------------------------------------------------------- |
| Free        | **Cloud sync not available.** The Free-tier desktop client doesn't expose the "sync" surface.        |
| Pro         | Encrypted-at-rest server-side (AES-256-GCM via libsodium's secretstream); TLS in transit.           |
| Team        | Same as Pro + a shared-team-namespace boundary (server enforces).                                   |
| Enterprise  | **End-to-end encrypted.** The client encrypts before upload; the server stores ciphertext only.     |

The Enterprise tier's E2E surface is the load-bearing
differentiator. The MVP (Sprint 16-17) ships Pro + Team
encryption; Enterprise E2E lands Sprint 20.

Server-side key custody for Pro / Team: per-org key in AWS
KMS / GCP KMS (deployment-side concern; ADR-0023 Sprint 17+).
The proxy and the cloud-sync service share the org key store —
not the keys themselves (each service has its own KMS access
role).

### 4. Separate service from the proxy

Cloud sync lives at `services/cloud-sync/` — its own Rust
binary. Reasons (same shape as ADR-0019 § 4):

- **Different load profile.** The proxy is request-response
  (latency-sensitive, low storage). Cloud sync is bulk
  upload / download (throughput-sensitive, high storage).
  Co-locating risks one's load profile pessimising the
  other.
- **Separate deploy / scale story.** Cloud sync scales with
  user count + project size; the proxy scales with active
  chat sessions. Decoupling means each scales independently.
- **Shared token boundary** — both services validate
  `sxm_pro_<...>` tokens against the same account portal
  (Sprint 17). Auth is centralised; the workloads are not.

### 5. Free tier and the "no sync available" surface

The Free-tier desktop client shows the "sync" surface as
**disabled with a clear upgrade prompt** in the Settings →
Sync panel. Not "Sync is a Pro feature, sign up here" with
upsell pressure — just an honest "Cloud sync ships with the
Pro tier. Your project files stay local."

This matches the BYOK-as-default policy (ADR-0003) and the
no-telemetry default (`docs/COMMUNITY.md`) — the Free tier
is intentionally usable as a permanent state, not a
trialware funnel.

## Consequences

- New `services/cloud-sync/` directory tree (scaffold this
  push):
    - `services/cloud-sync/README.md` — architecture
      summary linking back to this ADR.
    - `services/cloud-sync/api/openapi.yaml` — the v1 HTTP
      contract: `PUT /v1/project/<id>/file/<name>`,
      `GET /v1/project/<id>/file/<name>`,
      `GET /v1/project/<id>/manifest`.
    - `services/cloud-sync/src/main.rs` — axum binary
      returning honest 503s on every endpoint. Same shape
      as the managed-AI proxy's Sprint 14 push 3 scaffold.
- Sprint 16 push 1 lands the S3-backed write path.
- Sprint 17 lands the desktop client's "synced projects"
  UI surface + the conflict panel.
- Sprint 20 lands the Enterprise-tier E2E encryption.

## Risks

- **R-021 (sync-without-encryption window).** The Sprint
  16-17 MVP ships Pro encrypted-at-rest only; Team's namespace
  boundary lands later in Sprint 17, and Enterprise E2E lands
  Sprint 20. A Team-tier user could see other-org data in a
  Sprint-17 partial-deploy scenario if the namespace check is
  wrong. **Mitigation:** Sprint 17 push 1's integration tests
  exercise the cross-tenant attack. **Likelihood: Low;
  Impact: High.**
- **R-022 (binary output cap).** The 100MB-per-project VTU
  cap is a guess. **Mitigation:** ADR-0022 candidate fires
  during Sprint 18 if real users hit it.
- **R-023 (conflict-panel UX is the load-bearing
  surface).** A bad conflict UI is worse than no sync — the
  user's reaction is "the tool ate my work" even when both
  versions are recoverable. **Mitigation:** Sprint 17's
  visual-regression suite covers the conflict panel; user
  testing in Sprint 18.

## Related ADRs

- ADR-0019 (managed-AI proxy) — same separate-service-with-
  shared-token pattern.
- ADR-0020 (per-project provider override) — the
  `project.ai.toml` file the cloud sync deliberately
  **doesn't** sync.
- ADR-0017 (public-alpha bug-discovery) — the conflict-
  panel signal will feed back through the triage workflow.
- ADR-0011 (tool contract v1 frozen) — none of the tools
  this contract names touch the cloud sync surface; the
  agent does not have a "sync now" tool today.

— Sprint 15 push 3.
