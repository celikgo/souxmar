# Sprint 19 retro — Geometry-edits architecture; carry-overs still parked

**Closed:** 2026-05-17. **Pushes:** 1. **Theme:** "architecture-only
sprint. ADR-0030 names the in-app geometry-edit surface +
explicitly limits it."

This is the shortest sprint of the v0.9.x window — one push.
Sprints 18-20 are all architecture-decision sprints by design.

## What landed

| Push | Deliverable                                       | Lines |
| ---- | ------------------------------------------------- | ----- |
| 1    | ADR-0030 lightweight geometry edits + retro + v0.9.7 (this commit) | ~140 |

~3 pts.

## What to keep

- **The "explicitly NOT" half of every ADR.** ADR-0030's
  "this ADR does NOT cover" section is the load-bearing part —
  it names parametric history, boolean ops, sketch-based
  edits as **out-of-scope-for-v1.0** before anyone asks. The
  pattern protects v1.0 from feature creep.

## What to fix

- **Carry-overs from Sprints 17-18 still parked.** Bootstrap PR,
  marketplace install body, account-portal Postgres,
  IExecutionRouter implementation, OffloadingRouter, geometry
  edit_op.h implementation. **Action: Sprint 22's beta cut
  forces the carry-overs to land or get explicitly excluded
  from the v0.99 scope.**
- **DNS / Discord / on-call rotation: stale 8 sprints.**
  **Action: Sprint 22 retro's "what to fix" promotes these to
  blocking-the-v1.0-launch unless resolved.**

## Risk register diff

No new risks. R-013/14/16/17 stay closed. R-015 external
feedback continuing; Sprint 21 retro is the first to fairly
report. R-018-R-038 monitoring.

R-010 velocity: Sprint 19 ~3 pts. Three consecutive sub-15
sprints (S17: 13, S18: 8, S19: 3). The architecture-decision-
only window has run its course; Sprint 20-21 will be similarly
small; Sprint 22's beta cut reverses the trend hard.

## Capacity for Sprint 20

Sprint 20 target: 35 ± 15. Themes: SSO/SCIM for Team
Enterprise + Enterprise-tier E2E cloud sync. Sprint 20 will
add 2 ADRs (likely 0031 + 0032).

## Outcome

souxmar is at **v0.9.7**. ADR count: 30. The plugin C ABI
stays v1.3 final; tool contract v1 final; bridge ABI v3.

Sprint 20 picks up SSO/SCIM + E2E sync.
