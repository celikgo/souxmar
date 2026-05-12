# ADR-0036: v1.0.0 final freeze — the contracts are locked

- **Status:** Accepted
- **Date:** 2026-05-22 (Sprint 24 push 1)
- **Tier:** 1 (release engineering)
- **Affects:** v1.0.0 tag artefact + every Tier-2 contract
  the project ships.

## Decision

**v1.0.0 ships at the scope ADR-0034 named + the contracts
ADR-0035 baked.**

The four "FINAL-FINAL" contracts pass the v1.0 line as such:

| Contract                | Status     | First named  | v1.0 result |
| ----------------------- | ---------- | ------------ | ----------- |
| Plugin C ABI            | v1.3       | ADR-0008     | FROZEN      |
| Agent tool contract     | v1 (18 tools) | ADR-0011  | FROZEN      |
| On-disk pipeline format | v1         | (Sprint 3)   | FROZEN      |
| Update manifest         | v1         | ADR-0013     | FROZEN      |

The four "additive-Tier-0" contracts pass v1.0 as such:

| Contract            | Status      | Rule                                |
| ------------------- | ----------- | ----------------------------------- |
| BridgeFeatureSet    | 6 fields, ABI v3 | Additive Tier-0 (ADR-0016)        |
| Service OpenAPI x6  | v1          | Additive Tier-0                      |
| CLI --json shapes   | schema=1, 3 kinds | Additive Tier-0 (ADR-0025)      |
| project.ai.toml     | schema=1    | Additive Tier-0 (ADR-0020)          |

### What ships in v1.0.0

Everything in v0.99-rc1 + the 1 deferred Medium pen-test
finding's documented mitigation. No code changes from rc1.

### What's NOT in v1.0.0 (and what release it ships in)

- Viewport rendering — v1.1.0
- Enterprise E2E cloud sync — v1.0.1
- In-app geometry edits — v1.0.x
- Paid plugins live + publisher onboarding flow — v1.0.2
- Live HPC backend for compute-offload (currently stub-only) — v1.2.0
- Boolean ops / parametric history / sketches — v2.x (require ABI v2)

### v1.x release cadence

- **v1.0.1** — Sprint 25-26. Enterprise E2E cloud sync + the
  pen-test Medium deferral closed.
- **v1.0.2** — Sprint 27. Paid plugins live + publisher
  onboarding.
- **v1.1.0** — Sprint 28+. Viewport rendering (Three.js +
  VTK.js wired through the souxmar-bridge's
  `viewport_renderer` flag).
- **v1.0.x → v1.1.0** path stays additive-Tier-0 only.

### v2.0 trigger

A v2.0 ABI / tool / pipeline-format break is the *only* path
that lets:

- A new plugin C ABI (multiphysics coupling, GPU dispatch).
- A second plugin language (Python decorator → real ABI, beyond
  the prototyping shim).
- A new agent tool category that violates the v1 18-tool freeze.

v2.0 is *post-v1.0 future-sprint scope*. ADR-0036 doesn't open it.

## Consequences

- The `souxmar` GitHub release page hosts v1.0.0 artefacts
  signed via the release signing key per ADR-0014.
- `docs.souxmar.dev` updates with v1.0 release notes; the
  "Status" banner in README.md flips from "Release candidate"
  to "Stable: v1.0.0."
- Sprint 25+ enters the v1.x maintenance cadence; the
  per-sprint retro convention continues; SPRINT_PLAN.md
  rewritten post-v1.0 to cover v1.1 + v1.2.

— Sprint 24 push 1.
