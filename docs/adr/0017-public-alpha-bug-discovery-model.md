# ADR-0017: Public-alpha bug-discovery model — external-first, synthetic-load-augmented

- **Status:** Accepted
- **Date:** 2026-05-11 (Sprint 13 push 1)
- **Author:** souxmar platform team
- **Deciders:** platform, AI, release engineering
- **Tier:** 1 (process — names the bug-discovery strategy for the
  v0.9.x → v1.0.0 stretch; does not change any frozen contract;
  ratifying the choice unblocks how Sprints 13-21 budget triage
  capacity vs. feature work)
- **Affects:** `docs/COMMUNITY.md` (the SLA matrix the model
  assumes); `.github/workflows/triage.yml` (the auto-ack the model
  promises); `scripts/synth-load/` (this ADR introduces the
  harness); the per-sprint capacity forecast in Sprint 13+ retros.

## Context

Sprint 12 cut **v0.9.0** — the first non-`-beta`-suffixed tag in
the project's history — and shipped it as a *public* alpha without
a prior internal-team dogfood week. SPRINT_PLAN.md had presumed an
internal dogfood week preceded the public alpha; Sprint 11's retro
documented why that step was deferred (one contributor cannot
*meaningfully* dogfood a tool whose value is breadth of usage); the
Sprint 12 retro actioned the deferral by tagging v0.9.0 anyway.

The Sprint 12 retro left the ratifying decision for Sprint 13:

> The pattern of "the *public alpha cut* is the dogfood" — releasing
> a v0.9.0 to external users before the team has driven extensive
> internal usage — needs a decision artefact. ... The question worth
> ADR'ing: **what's the bug-discovery model for souxmar's public
> alpha** — internal team load, external user load, or both?

This ADR is that decision artefact. It picks one of three shapes
and names the metrics by which we'll judge the choice.

The choice matters now (Sprint 13 push 1) because:

1. **It sets Sprint 13-21's capacity split.** External-first means
   the triage rotation is on the critical path; feature work
   absorbs the leftover. Synthetic-load-augmented adds a third
   queue. Team-first reverts the v0.9.0 tag.
2. **It is reversible only at the cost of pulling v0.9.0.** The
   external-first choice has already been made *de facto* by the
   public alpha cut. Reverting means yanking signed installers
   from the download page — a step that signals more disruption
   than the disruption it would prevent.
3. **It tells external users what to expect.** Without a
   documented model, "what does 'public alpha' mean for souxmar
   specifically?" is answered by inference. The COMMUNITY.md SLAs
   are necessary but not sufficient — they answer *response*
   timing, not *discovery* methodology.

## The three plausible shapes

Sprint 12's retro named these. Restated for the decision record.

### Option A — External-first

v0.9.0 ships. Bugs flow from real users via the triage workflow
landed in Sprint 12 push 1. Sprint 13+'s primary work allocation
is fixing what comes in. The forecast model is reactive —
backlog drains at the SLA rate, feature work fills the slack.

- **Pro:** highest signal-per-engineer-hour — every bug is one a
  real user actually hit.
- **Pro:** zero additional infrastructure to build.
- **Pro:** the public alpha launch is a single event with a single
  measurement window.
- **Con:** the first month's reports are skewed toward whatever
  workflow the first wave of early adopters happens to try.
  Breadth coverage is whatever the user base randomly samples.
- **Con:** if launch comms underperform, no signal arrives — the
  rotation starves and we learn nothing.
- **Con:** unknown-unknown failure modes (regressions that no
  user happens to hit in week 1) accumulate silently.

### Option B — Synthetic-load-augmented external

v0.9.0 ships. In parallel, a synthetic-load harness re-runs every
example pipeline + every eval task on every commit, diffs output
against committed golden hashes, and surfaces regressions to the
triage workflow as if they were external reports — same SLA, same
labels, same rotation queue. The harness is roughly one sprint of
work; it does not require external infrastructure (it runs in CI
on the existing eval-nightly cadence).

- **Pro:** breadth coverage independent of which external users
  show up. Every example exercises every commit.
- **Pro:** the regression-detection pipeline is also useful
  *forever* — Sprint 13 builds it; Sprint 24's RC hardening still
  uses it.
- **Pro:** regressions surface at commit time, not at release time
  — the actionable feedback loop is shorter for engine bugs than
  the external-user loop's days-to-weeks.
- **Con:** golden hashes for non-deterministic outputs (mesher
  ordering, agent traces) need careful normalization. The harness
  is only as good as its golden corpus.
- **Con:** ~8 pts of upfront work that the external-first path
  defers.
- **Con:** synthetic load is not a substitute for the workflow
  diversity real users bring — it complements but cannot replace.

### Option C — Team-first (orthodox)

Delay v0.9.0 until the team has driven the internal dogfood week.
The team grows from one contributor to N before the alpha cut.
Sprints 13-15 are the dogfood window.

- **Pro:** discovers internal-friction bugs (build, install,
  onboarding) before external users hit them — protects the
  launch reputation.
- **Pro:** the team understands the tool deeply before triaging
  external reports about it.
- **Con:** requires the team to grow first. Hiring is on R-010's
  critical path; one-contributor "dogfooding" is not meaningful
  signal generation.
- **Con:** weeks of slip on the public-alpha announcement window.
  The marketing momentum from the v0.9.0 cut decays if the cut
  doesn't ship.
- **Con:** the v0.9.0 tag already exists — reverting it costs
  more than the reversion saves.

## Decision

**We adopt Option B (synthetic-load-augmented external).**

Option A is what we already did *de facto*. Option C is closed off
by the v0.9.0 tag's existence. Option B is the path that
preserves what Option A already locked in *and* makes the choice
defensible by adding breadth coverage that the random-walk of
early-adopter workflows alone cannot provide.

Concretely:

1. **External-first stays the headline strategy.** Public-alpha
   bug reports flow into the triage workflow per COMMUNITY.md. The
   maintainer rotation handles them per the SLA matrix.
2. **The synthetic-load harness backs it up.** Sprint 13 push 1
   (this push) introduces `scripts/synth-load/run.sh` — a driver
   that runs every `examples/*/pipeline.yaml` end-to-end, every
   `evals/v1/*.yaml` task, diffs the output (or trace) against a
   committed golden hash, and emits a JSON report. The driver is
   wired into the eval-nightly workflow.
3. **Synthetic regressions enter the same triage queue.** When
   the harness flags a diff, it files an issue with the same
   label conventions the auto-ack uses for external reports. The
   rotation does not distinguish between "external user reported
   X" and "synthetic load detected X" at triage time — both are
   work items with the same SLA.
4. **The golden corpus grows over time.** Sprint 13 lands the
   harness + a small starter corpus (cantilever-beam + the 3
   smoke evals). Subsequent sprints add coverage as bug reports
   come in — every reproducer becomes a synthetic-load golden,
   ratcheting the regression net.

## Metrics

The decision is judged against four metrics. Sprint 18 reviews
them and re-opens this ADR if any are red.

| Metric                                                          | Target                              | Source                            |
| --------------------------------------------------------------- | ----------------------------------- | --------------------------------- |
| External bug reports in v0.9.0's first month                    | ≥ 20                                | Triage workflow count             |
| P0/P1 reports resolved within the COMMUNITY.md SLA              | ≥ 50 %                              | Triage workflow timestamps        |
| Synthetic-load runs without false-positive diffs (weekly)       | ≥ 6 of 7                            | `scripts/synth-load/` JSON output |
| Synthetic-load regressions caught *before* any external report  | ≥ 1 per month after Sprint 14       | Cross-referenced manually         |

The first two test the external-first half. The last two test the
synthetic-load half. A red metric on one half does not
automatically pivot to a different option — it triggers a focused
ADR-0017 amendment that names the specific failure mode and the
adjustment, not a wholesale re-litigation.

## What this ADR does NOT do

- **It does not commit to a perpetual "external+synthetic" model.**
  Once the team grows past N=3 contributors (R-010 closes), Sprint
  X revisits and considers folding a real internal dogfood window
  back in. That revisit is an ADR-0017 amendment, not a fresh
  decision.
- **It does not freeze the synthetic-load harness schema.** The
  golden-output format is a Tier-0 contract — the harness's output
  shape is allowed to evolve sprint-to-sprint until the corpus
  stabilises (target: Sprint 18).
- **It does not promise the harness catches every regression.**
  Golden-hash comparison catches *deterministic* output drift.
  Non-deterministic regressions (ordering, timing, agent
  reasoning) need a separate strategy — likely Sprint 16+.
- **It does not change any frozen contract.** The plugin C ABI
  stays at v1.3 frozen final; the tool contract stays at v1
  frozen final with 18 tools; the BridgeFeatureSet stays at
  Tier-2.

## Consequences

- Sprint 13 push 1 lands `scripts/synth-load/run.sh` + a starter
  golden corpus + a CI wiring stub.
- Sprint 13's capacity forecast accounts for both the rotation
  load (variable; depends on external volume) and the harness
  build (8 pts upfront).
- The "honest framing" in README.md / docs.souxmar.dev is updated
  to say "public alpha, synthetic-load-backed" — clearer than
  "public alpha, dogfood-deferred."
- The Sprint 13 retro reports the first week's external-volume
  number against the ≥ 20-in-a-month target.

## Alternatives reconsidered later

If Sprint 18's metric review shows:

- External volume < 20: ADR-0017 amendment to either (a) reduce
  the threshold based on observed market size, or (b) invest in
  launch comms — *not* a pivot away from external-first.
- P0/P1 SLA-met < 50 %: ADR-0017 amendment to either (a) grow the
  rotation (R-010 dependent), or (b) reduce SLA promise in
  COMMUNITY.md — both are explicit, neither is silent.
- Synthetic-load false positives > 1/week: harness's golden
  normalization is broken — fix the harness, not the model.
- Synthetic catches < 1/month after Sprint 14: corpus is too
  small. Add more pipelines, not a different strategy.

The shape of the model survives all four amendments. The numbers
change.

## Related ADRs

- ADR-0011 (tool contract v1 final freeze) — the evals corpus the
  harness re-runs is built on the contract this ADR ratifies.
- ADR-0013 (signed update manifest) — the auto-updater is one of
  the surfaces the synthetic harness must cover before v1.0.
- ADR-0016 (BridgeFeatureSet contract) — the desktop's
  "scaffolding vs wired" status is *not* covered by this harness;
  feature-flag flips are a separate signal documented in retros.

— Sprint 13 push 1.
