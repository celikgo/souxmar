# Sprint 17 retro — Pro-tier service surface complete in scaffold; ADR-0025/26/27

**Closed:** 2026-05-15. **Pushes:** 4. **Theme:** "the six-service
Pro-tier surface is now complete in scaffold; ADR-0025 names the
typed CLI shape contract; launch-comms actually executes."

## What landed

| Push | Deliverable                                                                       | Lines |
| ---- | --------------------------------------------------------------------------------- | ----- |
| 1    | ADR-0025 + CLI shapes mirror + retrofits + launch-comms execution + billing test | ~380 |
| 2    | Account portal scaffold + ADR-0026                                               | ~265 |
| 3    | Compute-offload scaffold + ADR-0027                                              | ~240 |
| 4    | Sprint 17 retro + v0.9.5 (this commit)                                           | this |

Total LOC ~880. Sprint 17 was a "consolidate the carry-overs +
two more scaffolds" sprint; the template compresses further.
Rolling 4-sprint median (S14-S17): 25, 18 — settling at ~20.

## What to keep

- **ADR-0025's typed CLI shapes pattern.** The `kind` discriminator
  + serde-default forward-compat shape will pay off through
  Sprints 18-22 as more subcommands grow `--json` output. The
  three unit tests in cli_shapes.rs cover the round-trip /
  forward-compat / unknown-kind cases — the contract is
  machine-verified.
- **Launch-comms-as-doc.** docs/launch/2026-05-15-show-hn.md
  records the event as it happens — the HN post, the 5 partner
  emails, the metrics to report against. Future launch events
  (Sprint 22 public beta, v1.0 release) follow the same shape.
- **Six-service surface complete-in-scaffold.** The Pro-tier
  business shape is now nameable end-to-end across 9 ADRs
  (0019-0027). Sprints 18-22 turn scaffolds into running code
  in business-priority order without re-litigating architecture.

## What to fix

- **Synth-load + VR baselines: stale 5 / 4 sprints.** The
  INFRA_STATUS counter passes the 5-sprint escalation threshold
  on synth-load. **Action: Sprint 18 push 1 lands the bootstrap
  PR or retires the gate mechanism.**
- **Marketplace install body still not_yet_wired.** Sprint 16
  push 4's CLI surface stable; Sprint 17 push 2 deferred the
  body to focus on account-portal. **Action: Sprint 18 push 2
  lands the body using the Sprint 11 push 2 fetcher.**
- **Launch-comms result not yet measurable.** The HN post + 5
  emails went out on the day this retro is written; ADR-0017's
  metrics ("≥ 20 bug reports / month") need 30 more days
  before they're reportable. **Action: Sprint 21 retro is the
  first one that can fairly report against the metric.** Until
  then: assume zero-volume is "audience-absent" until proven
  otherwise.
- **DNS / Discord / on-call rotation: stale 6 sprints each.**
  Past threshold. Operational items; "what to fix" repeats
  with no movement. **Action: this is the third repetition;
  Sprint 18 retro names them under "what to fix → escalate"**
  if still unmoved.

## One ADR-worthy decision queued

**Engine-side ExecutionRouter hook for hosted-compute routing.**
ADR-0027 named the wire-protocol; the engine-side router that
inspects `stage.execution.target` and either local-dispatches
or remote-submits needs an ADR before Sprint 19 push 1
implements it. The hook touches the pipeline runner — the
load-bearing single-thread of the engine.

**ADR-0028 candidate (Sprint 18 push 2):** "Pipeline runner
ExecutionRouter — typed StageExecutionTarget + per-stage
dispatch decision." Three viable shapes:

1. **Routing decided at dispatch time** by the runner
   inspecting the stage; the existing RegistryDispatcher gains
   a "skip to remote" code path.
2. **Routing decided at parse time** with the dispatcher
   selected per-stage; runs the existing dispatcher for local
   stages + a new RemoteDispatcher for managed ones.
3. **Hybrid:** dispatcher is local; the local plugin's solver
   stub forwards to the remote service. Lets unmodified
   plugins participate without router awareness.

ADR-0028 will pick. Probably option 2 — keeps the dispatcher
contract clean.

## Risk register diff

- **R-015 (external feedback):** launch-comms executed today.
  Volume reportable in Sprint 21 retro. **Monitoring.**
- **R-017 (managed-AI proxy MVP):** **closes** — the proxy is
  feature-complete for v0.99 minus the account portal +
  billing wire-up that land Sprint 18.
- **R-024 (Anthropic price shock):** Sprint 18 push 1 wires
  the hot-reloadable price table. **Monitoring.**
- **R-028 / R-029 / R-030 (billing):** unit-test added for
  R-030 in push 1. **Monitoring.**
- **R-031 / R-032 (account portal):** scaffold; surface
  during Sprint 18 push 1's wire-up.
- **R-033 / R-034 (compute offload):** scaffold; surface
  during Sprint 19 push 2.
- **R-010 (velocity):** Sprint 17 ~13 pts. Trend continues
  down (S13: 30, S14: 28, S15: 25, S16: 18, S17: 13). The
  template compresses; the *thicker* Sprint 18+ pushes
  (Postgres wire-up, real install body) will reverse the
  trend. Sprint 18 target: 35 ± 15 stays.
- **R-013, R-014, R-016 stay closed.**

## Capacity for Sprint 18

Sprint 17 ran ~13 pts. Sprint 18 target: 35 ± 15.

SPRINT_PLAN.md § Sprint 18 ("Multi-window / multi-project
polish; project-level isolation of AI providers"). Combined
with carry-overs:

- ADR-0028 + engine-side ExecutionRouter (~5 pts).
- Synth-load + VR baselines actual bootstrap PR (~3 pts).
- Marketplace install body (~6 pts; uses fetcher.cpp).
- Account portal Postgres + Postmark wire-up (~8 pts).
- Multi-window polish + per-project provider isolation (~6 pts;
  Sprint 18 theme).
- Sprint 18 retro + v0.9.6 (~3 pts).

## Outcome

souxmar is at **v0.9.5** as of this commit. The Pro-tier
service surface is **complete in scaffold** — six services
(proxy, cloud-sync, marketplace, billing, account-portal,
compute-offload), nine ADRs (0019-0027), one CLI subcommand
ratchet (`plugin install`), one typed CLI shape contract
(ADR-0025), one consolidated CLI shapes Rust mirror.

The ABI stays at v1.3 frozen final. The tool contract stays at
v1 frozen final with 18 tools. The bridge ABI stays at v3.
Five engine surfaces consume the bridge or the engine
(CLI, Python, three desktop panels).

The "scaffold the contract, stub the implementation" pattern
that started in Sprint 14 is now reflexive enough that Sprint
17's scaffolds compressed to ~250 LOC each. Sprint 18 starts
the thicker turn-scaffolds-into-running-code work.
