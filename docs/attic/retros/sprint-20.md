# Sprint 20 retro — SSO/SCIM + E2E cloud sync architecture

**Closed:** 2026-05-18. **Pushes:** 2. **Theme:** "Team + Enterprise
tier auth/encryption decisions before the v0.99 beta cut."

## What landed

| Push | Deliverable                                       | Lines |
| ---- | ------------------------------------------------- | ----- |
| 1    | ADR-0031 SSO/SCIM + ADR-0032 E2E cloud sync       | ~190 |
| 2    | Sprint 20 retro + v0.9.8 (this commit)            | this |

~5 pts.

## What to keep

- **Two ADRs in one push** when they share a common Enterprise-
  tier theme. Sprint 20 push 1 ratified SSO/SCIM + E2E sync
  together because the auth-token shape ADR-0031 picks is the
  one ADR-0032 builds the E2E recovery flow on.

## What to fix

- All Sprints 17-19 carry-overs still parked. Sprint 22's
  v0.99 cut forces them.
- DNS / Discord / on-call rotation stale 9 sprints. **Sprint
  22 retro promotes to blocking-v1.0 unless resolved.**

## Risk register diff

- **R-039 (SAML signature-validation bugs).** New;
  Sprint 21 pen-test exercises.
- **R-040 (E2E + cross-platform key portability).** New;
  documented recovery path.
- Other risks: monitoring.

## Capacity for Sprint 21

Sprint 21 target: 35 ± 15. Theme: security audit + bug-bounty.

## Outcome

souxmar **v0.9.8**. ADR count 32. The plugin C ABI stays v1.3
final; tool contract v1 final; bridge ABI v3.

Sprint 21 turns to security; Sprint 22 cuts the public beta.
