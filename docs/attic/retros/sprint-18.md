# Sprint 18 retro — ExecutionRouter + multi-window architecture decisions

**Closed:** 2026-05-16. **Pushes:** 3. **Theme:** "two more
architecture-decision pushes before the v0.99 public beta
window opens (Sprint 22). Smaller sprint by design."

This sprint and the following 2-3 are intentionally
architecture-heavier than implementation-heavier — the v1.0
contract surface needs to be named before Sprint 22 freezes the
public beta API.

## What landed

| Push | Deliverable                                       | Lines |
| ---- | ------------------------------------------------- | ----- |
| 1    | ADR-0028 ExecutionRouter                          | ~135 |
| 2    | ADR-0029 multi-window + per-project AI isolation  | ~115 |
| 3    | Sprint 18 retro + v0.9.6 (this commit)            | this |

~8 pts. Two ADRs that name the contracts; the implementations
land alongside their consumers — ADR-0028's
IExecutionRouter ships in Sprint 19 push 1 with
OffloadingRouter; ADR-0029's window manager ships in Sprint 22
push 1 with the beta polish.

## What to keep

- **"Name the contract; ship the code with its consumer."**
  Sprint 18's two ADRs deliberately don't ship code — they
  ratify decisions that have load-bearing consumers in Sprint
  19+. Shipping the contract without the consumer would force
  a needless API change (the run_pipeline signature for ADR-
  0028) the moment the consumer lands.

## What to fix

- **Carry-overs from Sprint 17 push 4 didn't land.** Bootstrap
  PR, marketplace install body, account-portal Postgres wire-up
  — all parked. **Status: still parked.** Sprint 19 push 2 is
  the next opportunity; if still missing at Sprint 21, those
  surfaces enter the v0.99 cut without their first real
  exercise.
- **DNS / Discord / on-call rotation: stale 7 sprints.**
  **Sprint 19 retro: escalate** if still operational-pending.

## Risk register diff

No new risks this sprint. R-013/14/16/17 stay closed. R-015
external feedback still measuring (Sprint 21 reportable).
R-010 velocity is at ~8 pts this sprint — second consecutive
sub-15. The architecture-decision-heavy work compresses;
Sprint 22's implementation-heavy beta polish reverses.

## Capacity for Sprint 19

Sprint 18 ran ~8 pts. Sprint 19 target: 35 ± 15. Themes per
SPRINT_PLAN.md § Sprint 19 ("Lightweight in-app geometry edits
— reposition, suppress, parameter tweaks. Not parametric
modelling."). Plus carry-overs:
- IExecutionRouter implementation + OffloadingRouter (~6 pts).
- Marketplace install body (~6 pts).
- Bootstrap PR (~3 pts).
- Account-portal Postgres + Postmark (~8 pts).
- In-app geometry edits (~6 pts; Sprint 19 theme).
- Sprint 19 retro + v0.9.7 (~3 pts).

## Outcome

souxmar is at **v0.9.6**. Two ADRs added (0028, 0029). Total
ADR count: 0001 through 0029. The plugin C ABI stays at v1.3
frozen final; the tool contract stays at v1 frozen final with
18 tools; the bridge ABI stays at v3.

Sprint 19 turns ADR-0028 + ADR-0029 into running code alongside
the geometry-edits theme.
