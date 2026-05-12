# ADR-0033: Security audit scope + bug-bounty programme

- **Status:** Accepted
- **Date:** 2026-05-19 (Sprint 21 push 1)
- **Tier:** 1 (security + process)
- **Affects:** all of `src/`, all `services/`, the OS keychain
  integration, the auto-updater's signing key custody; new
  `docs/SECURITY.md` § Bug bounty.

## Decision

### Pen-test scope (Sprint 21 push 1 engagement)

External firm (selected by the platform team, not named in
this ADR) engaged for a 3-week engagement covering:

1. **Plugin loading + signature verification path** (ADR-0008,
   ADR-0013) — manifest tampering, malicious .so payloads,
   signature-stripping attacks.
2. **Auto-updater state machine** (ADR-0013 + ADR-0014) —
   replay-downgrade, key-rotation-window attacks, install-
   layout corruption.
3. **SAML response validation** in the account portal (ADR-
   0031 R-039 — the 2017 XML-signature-wrapping CVE class).
4. **FFI boundary** (ADR-0018) — `souxmar-c-bridge.a` surface
   for memory-safety + use-after-free across the Rust ⇄ C++
   boundary.
5. **Stripe webhook validation** (ADR-0024) — signature
   bypass, replay, idempotency-id collisions.
6. **E2E key custody** (ADR-0032) — keychain export paths,
   per-org master-key leakage scenarios.

Out of scope: the C++ engine's mathematical correctness (eval
suite covers); plugin authors' own keys (ADR-0023 R-025 names
this as the publisher's responsibility).

### Bug-bounty programme

- **Launch:** Sprint 21 push 1 (this push).
- **Platform:** HackerOne (selected for triage workflow
  integration matching ADR-0017's SLA matrix).
- **Scope:** matches the pen-test scope plus
  `*.souxmar.dev` services.
- **Out of scope:** social engineering, physical access,
  third-party services (Anthropic / Stripe / GitHub).
- **Bounty tiers:**

  | Severity | Bounty |
  | -------- | ------ |
  | Critical | $5000  |
  | High     | $2000  |
  | Medium   | $500   |
  | Low      | $100   |

  Aligned with reasonable mid-2020s open-source bounty rates;
  funded out of the Pro-tier revenue stream.

- **Disclosure:** 90 days from report → coordinated public
  disclosure unless extension requested.

### SOC 2 readiness

Sprint 21's secondary deliverable is a SOC 2 Type I
readiness document for the managed services (proxy, cloud-
sync, marketplace, billing, account-portal, compute-offload).
The audit itself happens post-v1.0 (likely Sprint 28+); the
readiness doc + the audit-trail policies it codifies land
this sprint.

## Consequences

- `docs/SECURITY.md` (existing) gains a "Bug bounty" section
  pointing at the HackerOne programme + the scope.
- The triage workflow (Sprint 12 push 1) gets an additional
  auto-label `security` for issues that arrive via the bounty
  programme, with a P0 SLA bumped to 12 hours (down from 24).
- Sprint 22's v0.99 beta cut includes any P0 findings from
  the pen-test as blockers.

## Risks

- **R-041 (pen-test finding cluster).** A cluster of findings
  in the SAML path could delay the v0.99 beta cut.
  **Mitigation:** Sprint 22's beta scope (ADR-0034 candidate)
  documents which findings block the cut + which are
  acceptable-with-mitigation.

— Sprint 21 push 1.
