# ADR-0034: v0.99 public-beta scope — what ships, what doesn't, what carries to v1.0

- **Status:** Accepted
- **Date:** 2026-05-20 (Sprint 22 push 1)
- **Tier:** 1 (release scope)
- **Affects:** v0.99-beta1 release artefacts; the desktop +
  managed services that flip from "test mode" to "soft-open";
  the marketplace's first paid customer onboarding.

## Decision

### What ships in v0.99-beta1

**Engine** (unchanged from v0.9.x):
- Plugin C ABI v1.3 frozen FINAL.
- Agent tool contract v1 frozen FINAL at 18 tools.
- Bridge C ABI v3.
- CLI + Python library + desktop app (three FFI surfaces
  flipped structural; viewport_renderer still scaffolded).

**Pro-tier services** (managed AI + cloud sync + marketplace +
billing + account portal + compute offload):
- Account portal Postgres + Postmark wire-up live (test mode).
- Managed-AI proxy wired through to Anthropic upstream (live
  key + Stripe-mode=test).
- Marketplace install body wired (manifest fetch + signature
  verify; the install body actually downloads + extracts).
- Cloud-sync MVP write path live (S3-backed; Pro + Team
  encryption-at-rest; Enterprise E2E targeted for v1.0).
- Compute-offload service in stub mode (synthetic results
  for protocol-shape testing; real HPC backend wires post-
  v1.0).
- Billing service in test mode only — Sprint 22 → Sprint 23
  runs Stripe in test mode for ≥ 4 weeks (per ADR-0024).

**Marketplace soft-open:**
- 5 free plugins from the existing index.
- 0 paid plugins at beta1 (publisher onboarding flow doesn't
  go live until ADR-0033's pen-test clears the SAML / Stripe
  paths).
- First *paid* customer onboarding moves to v0.99-beta2 once
  pen-test concludes.

### What doesn't ship in v0.99-beta1 (explicitly)

- **Viewport rendering** — `viewport_renderer` flag stays
  off. The 6th BridgeFeatureSet flag was queued for Sprint
  17; it slid past the v0.99 cut and explicitly stays out.
  v1.0 ships without rendered Three.js viewport (the
  desktop's viewport pane shows a "open in ParaView" link to
  the writer's output).
- **Enterprise E2E cloud sync** — ADR-0032 lands in v1.0.1
  (post-v1.0). Pro/Team rest-only encryption is the v0.99
  ceiling.
- **In-app geometry edits** — ADR-0030 ratified; impl slides
  to v1.0.x.
- **Live billing mode** — Sprint 22 runs Stripe in test mode;
  Sprint 23 flips live alongside the v0.99-rc1 cut.
- **Boolean operations / parametric history / sketches** —
  post-v1.0.

### What carries to v1.0 as remediable risk

- The pen-test engagement runs in parallel with the beta
  window. Findings get triaged per ADR-0033's bumped P0 SLA
  (12h).
- ADR-0017's external-feedback metric (≥ 20 reports in the
  first month) reports back in this sprint's retro. Below
  threshold = launch-comms second amendment (ADR-0017's
  reserved branch) fires in Sprint 22 retro.
- DNS / Discord / on-call rotation: blocking v1.0 if still
  unmoved at Sprint 23 retro (per Sprint 20's forward note).

### Tag

**`v0.99-beta1`** — first non-`-alpha`-suffixed tag in the
project's history that precedes a final `v1.0.0`. The "v0.99"
prefix communicates "almost there, not quite v1.0" to
external readers.

## Consequences

- This commit cuts v0.99-beta1. Subsequent betas (beta2,
  beta3) ratchet as the pen-test concludes + as bug-bounty
  findings flow in.
- The release-CI workflow (`.github/workflows/release.yml`)
  picks the v0.99 prefix on tag-push.
- Sprint 23's RC hardening freezes the contracts that ship at
  v1.0 (ABI bake, tool contract final, on-disk pipeline
  format final).

## Risks

- **R-042 (beta-vs-final scope creep).** External users
  filing bug reports during the beta window will surface
  feature requests that creep the v1.0 scope. **Mitigation:**
  this ADR's "what doesn't ship" list is the gate; Sprint 23
  retro reviews against it.
- **R-043 (pen-test cluster Y in critical surface).**
  ADR-0033 R-041 carries forward. Critical findings block
  the v0.99-beta2 → rc1 transition.

— Sprint 22 push 1.
