# Sprint 23 retro — RC hardening + v0.99-rc1 + Stripe live flip

**Closed:** 2026-05-21. **Pushes:** 2. **Theme:** "bake the v1.0
contracts; flip Stripe live; ship the release candidate."

## What landed

| Push | Deliverable                                       | Lines |
| ---- | ------------------------------------------------- | ----- |
| 1    | ADR-0035 v1.0 contract bake + RC hardening + v0.99-rc1 | ~140 |
| 2    | Sprint 23 retro + retest of bake (this commit)        | this |

## What to keep

- **The "final-final vs additive-Tier-0" split** in ADR-0035.
  Names which contracts are frozen at v1.0 (ABI, tool, pipeline,
  manifest) vs. which can evolve through v1.x under additive
  rules (BridgeFeatureSet, OpenAPI, CLI --json,
  project.ai.toml). The split lets the project ratchet
  without breaking the v1.0 promise.

## What to fix

- **DNS / Discord / on-call rotation: stale 11 sprints.**
  Promoted last sprint to "blocking v1.0." **Status this
  sprint:** the founder cleared DNS CNAME for
  `docs.souxmar.dev` (12 hours of registrar work, real but
  outside-CI). Discord server stood up + invite redirect
  wired to `souxmar.dev/community`. On-call rotation table
  populated with 2 names (founder + first contributor).
  **All three resolved this sprint** — v1.0 launch is no
  longer blocked operationally.

## Risk register diff

- **R-013, R-014, R-016, R-017** closed.
- **R-041, R-042, R-043:** pen-test concluded with 0 Critical, 1
  High (Anthropic-call timeout handling — fixed during the
  RC hardening), 4 Medium (all triaged + 3 fixed; 1 deferred
  to v1.0.1 with explicit documentation). Bug bounty: 2 Low
  reports landed; both paid + fixed.
- **R-044 (last-minute contract bug):** ratified zero new
  contract changes in this sprint — the bake holds.

## Capacity for Sprint 24

Sprint 24 target: 30 pts. Final sprint. v1.0.0 tag + a
post-v1.0 plan brief.

## Outcome

souxmar is at **v0.99-rc1**. Stripe is live (first real Pro
subscription transaction processed this morning). Pen-test
concluded; 1 High fixed, 1 Medium deferred to v1.0.1, 0 Critical.
Bug bounty live with first 2 Low reports paid and fixed.

The plugin C ABI is **v1.3 final-final** under ADR-0035 (no
further changes pre-v1.0); the tool contract is **v1
final-final** at 18 tools; the on-disk pipeline format is **v1
final-final**; the update manifest format is **v1
final-final**.

DNS / Discord / on-call rotation operational items all resolved
this sprint. v1.0 launch unblocked operationally.

One sprint to v1.0.0.
