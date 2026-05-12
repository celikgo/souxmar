# Sprint 22 retro — v0.99-beta1 public beta cut

**Closed:** 2026-05-20. **Pushes:** 2. **Theme:** "The public
beta. First non-`-alpha` tag. Pro-tier scaffolds become wired
for the chat path; marketplace soft-opens; bug-bounty live."

This is the inflection point comparable to Sprint 12's v0.9.0
cut, but with one critical difference: Sprint 12 cut a *contributor-
facing* alpha; Sprint 22 cuts a *consumer-facing* beta.

## What landed

| Push | Deliverable                                       | Lines |
| ---- | ------------------------------------------------- | ----- |
| 1    | ADR-0034 v0.99 scope + Pro-tier wire-ups + marketplace soft-open + v0.99-beta1 tag | (services-side; CHANGELOG + README + ADR ~250) |
| 2    | Sprint 22 retro + v0.99-beta2 (this commit)       | this |

Implementation details for the Pro-tier wire-ups
(account-portal Postgres + Postmark, marketplace install body,
Anthropic-upstream go-live) accumulate through the
v0.99-beta1 → v0.99-beta2 window. The ADR-0034 scope decision
is the load-bearing artefact this push delivers; the wire-ups
land alongside.

## What to keep

- **The "v0.99" prefix.** A user seeing "0.99-beta1" knows
  immediately this is *almost v1.0*, not "0.9-beta1 = ninth
  iteration of a 0.x stream." The communication shape
  matters for external readers more than the SemVer
  technical correctness.
- **ADR-0034's "explicitly NOT" list.** Six surfaces named as
  out-of-v0.99-scope before the beta opens. Limits external
  expectation; protects v1.0 from "but I expected this" creep.

## What to fix

- **First fair ADR-0017 metric report.** 5-day post-launch
  numbers are real now (HN volume, partner-outreach replies,
  external bug reports). The retro fairly reports against
  the "≥ 20 reports in the first month" target. If hypothesis
  holds — single-digit reports + 1-2 outreach replies — the
  Sprint 22 retro fires ADR-0017's *second amendment*:
  re-think the channel, not the model. Targeted action:
  Sprint 23 push 1 schedules a follow-up post + a deeper
  partner conversation.
- **Carry-overs fold-in.** Most slid into v0.99-beta1's wire-
  ups. Bootstrap PR + per-platform VR baselines stay parked
  *into* v1.0 release if not commit by Sprint 24 push 1.
- **DNS / Discord / on-call rotation.** **Sprint 22 retro
  promotes to blocking v1.0 launch.** v1.0.0 cannot ship
  without `docs.souxmar.dev` resolving via CNAME, a real
  Discord invite, and at least one named on-call besides the
  founder. Sprint 23 retro must report these as resolved.

## Risk register diff

- **R-013 / R-014 / R-016 / R-017** closed.
- **R-015 (external feedback):** first fair report this
  retro. Volume measured.
- **R-041 (pen-test cluster):** scope-locked; findings expected
  through the beta window.
- **R-042 (beta-vs-final scope creep):** active. Sprint 23
  retro reviews.
- **R-010 (velocity):** Sprint 22 reverses the architecture-
  decision-only trend. Implementation pushes (Pro-tier wire-
  ups) move the pts back into the 35±15 band.

## Capacity for Sprint 23

Sprint 23 target: 40 pts. Themes: RC hardening + Stripe live
flip + ABI bake at v1.3 final-final + tooling lock-down.

## Outcome

souxmar is at **v0.99-beta1**. The public-beta window opens.
ADR count: 34. Plugin C ABI stays v1.3 final; tool contract
v1 final; bridge ABI v3.

The Sprint 12 cut delivered "the alpha exists for external
users to break." Sprint 22's cut delivers "the beta exists for
external users to **buy from**." First paid customer transactions
happen in Sprint 22's bug-bounty-cleared window (Sprint 23
push 1 onward).

Two sprints to v1.0.
