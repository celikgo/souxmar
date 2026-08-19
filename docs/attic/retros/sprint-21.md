# Sprint 21 retro — Security audit + bug bounty; first ADR-0017 metric report

**Closed:** 2026-05-19. **Pushes:** 2. **Theme:** "Security spine
in place before the v0.99 beta cut; first sprint that can fairly
report against ADR-0017's external-volume metric."

## What landed

| Push | Deliverable                                       | Lines |
| ---- | ------------------------------------------------- | ----- |
| 1    | ADR-0033 security audit + bug-bounty programme    | ~130 |
| 2    | Sprint 21 retro + v0.9.9 (this commit)            | this |

## ADR-0017 metric report — first fair window

The launch-comms push from Sprint 17 happened on 2026-05-15.
30 days from then = 2026-06-14. This sprint (closing 2026-05-19)
is 4 days post-launch — early but the first fair report
window starts here.

Hypothetical 4-day post-launch numbers (not yet measurable in
this retro because they're real-time):

- HN post outcome: pending.
- 5 partner outreach replies: pending.
- External bug reports: pending.

**Action:** Sprint 22 push 1's pre-beta gate revisits these
numbers + writes the v0.99-beta1 release-note "what we
learned" paragraph. Sprint 22's retro is the first one to
report against ADR-0017's "≥ 20 reports in the first month."

## What to keep

- **Security ADR-0033 scopes pen-test BEFORE engaging the
  firm.** Six numbered surfaces + explicit out-of-scope
  list. Saves firm-side scoping confusion + lets the engagement
  budget be predictable.
- **Bug-bounty tiers chosen against mid-2020s open-source
  norms.** $5k/$2k/$500/$100 is the right shape for a
  pre-revenue Pro-tier-bootstrapping org; reviewed against
  v1.0+ revenue.

## What to fix

- All carry-overs from Sprints 17-19 still parked. **Action:
  Sprint 22 push 1 forces them into the v0.99 scope or
  explicitly carves them out.**
- DNS / Discord / on-call rotation: stale 10 sprints. **Sprint
  22 retro promotes these to "blocking the v1.0 launch
  unless resolved beforehand"** per the Sprint 20 retro's
  forward-looking note.

## Risk register diff

- **R-041 (pen-test finding cluster delays v0.99).** New
  this sprint per ADR-0033. Sprint 22's beta-scope ADR-0034
  documents which findings block + which are acceptable.
- All other risks: monitoring.
- **R-010 velocity:** ~3 pts this sprint (single ADR + retro).
  Five consecutive sub-15 sprints. Sprint 22 will reverse hard
  with the v0.99 cut.

## Capacity for Sprint 22

Sprint 22 target: 50 pts (the v0.99 cut is intentionally
larger than the rolling-median band). Themes: v0.99-beta1 cut +
marketplace soft-open + first paid customers onboard (test
mode) + carry-over fold-in.

## Outcome

souxmar **v0.9.9**. ADR count 33. The plugin C ABI stays v1.3
final; tool contract v1 final; bridge ABI v3.

Sprint 22 cuts the public beta.
