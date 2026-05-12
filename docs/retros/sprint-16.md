# Sprint 16 retro — Pro-tier business shape complete in scaffold + ADR-0022 ratified + launch-comms reports

**Closed:** 2026-05-15. **Pushes:** 5. **Theme:** "the three Pro-tier
service scaffolds are now feature-complete-as-scaffolds; ADR-0022
names the MVC-via-subprocess pattern; the launch-comms amendment
from Sprint 15 reports back."

## What landed

| Push | Deliverable                                                                                                       | Lines |
| ---- | ----------------------------------------------------------------------------------------------------------------- | ----- |
| 1    | ADR-0022 (MVC-via-subprocess) + INFRA_STATUS stale-counter                                                         | ~150  |
| 2    | Plugin marketplace v1 scaffold + ADR-0023                                                                          | ~410  |
| 3    | Stripe billing integration POC + ADR-0024 + mode/key consistency check                                             | ~290  |
| 4    | `souxmar plugin install` CLI surface ratchet + license-key flag                                                    | ~130  |
| 5    | Sprint 16 retro + v0.9.4 + launch-comms result (this commit)                                                       | this  |

Total LOC ~1,200 — smaller than Sprint 15 because all four scaffold
pushes followed the now-named template (ADR + axum binary +
openapi.yaml + 503 stubs). The pattern compresses.

## What to keep

- **The four-services-one-pattern shape.** managed-ai-proxy (S14),
  cloud-sync (S15), plugin-marketplace (S16 push 2), billing
  (S16 push 3) all share the same scaffold: ADR-NNNN + axum
  binary + api/openapi.yaml + honest-503 stubs + a build that
  picks the service up via services-build.yml's `services/*/Cargo.toml`
  glob. Adding a fifth service in Sprint 17 will compress further.
- **Honest-503 with per-endpoint sprint pointers.** Every stubbed
  endpoint cites the sprint that lands its real impl. A user
  pointed at the proxy during Sprint 14-15 sees "Sprint 15 push 1
  lands the upstream forwarder"; pointed at billing today they
  see "Sprint 22 public-beta runs the full path in test mode for
  ≥ 4 weeks before live." Operationally useful + a hedge against
  the "every service ships at v1" expectation.
- **Mode/key consistency check at startup** (billing). The
  failure mode this catches (mode=live + sk_test_*) would create
  real charges in a test environment. The panic-at-startup is
  the right place to enforce; the cost of one extra check is
  trivial.
- **`--json` as the CLI's shell-out contract.** Sprint 16 push 4's
  `souxmar plugin install` follows the same shape Sprint 13+'s
  `souxmar agent list --json` introduced. The desktop's
  shell-outs all parse `--json` output rather than scraping
  human-readable text. Consistency lets the React side pick up
  new fields via additive Tier-0 changes.

## What to fix

- **Synth-load + VR baselines: stale 4 / 3 sprints respectively.**
  No closer to commit. The INFRA_STATUS counter passed 3 last
  sprint; on track to escalate to "what to fix" in Sprint 17's
  retro if no bootstrap PR lands. **Action: Sprint 17 push 1
  commits the bootstrap PR or retires the gate mechanism.**
- **Launch-comms result reports below target.** Sprint 15's
  amendment queued a Show HN + partner outreach pass; this
  sprint executed neither (the maintainer had every push in
  scope on the marketplace scaffold). External bug volume:
  third consecutive zero-volume week. The amendment didn't move
  the metric because the action didn't happen. **Action: Sprint
  17 push 1 (alongside the bootstrap PR) actually schedules the
  HN post + sends 5 partner-outreach emails.** Not a strategy
  pivot; still external-first per ADR-0017.
- **No billing-mode integration test.** The mode/key consistency
  check is tested only by inspection. **Action: Sprint 17 push 1
  adds a unit test for the billing service's parse_mode +
  key_consistency_check pair.**
- **DNS / Discord / on-call rotation: stale 5 sprints each.**
  The INFRA_STATUS counter passes the 5-sprint threshold next
  sprint. Operational items; Sprint 17's retro names them in
  "what to fix" if still unmoved.

## One ADR-worthy decision queued

**Sprint 16 push 4 named that the `souxmar plugin install`
output shape is a contract.** Today it's an ad-hoc JSON shape
defined inline in the CLI; the desktop client parses it without
a typed Rust mirror. Three sprints from now there will be a
half-dozen CLI subcommands with `--json` shapes the desktop
shells out to (`update apply`, `account refresh`, `sync`, the
existing `agent invoke`). Without a typed contract they will
drift.

**ADR-0025 candidate (Sprint 17 push 3): "souxmar CLI --json
output shapes — schema=1 discriminator + typed Rust mirror in
souxmar-bridge crate."** Same pattern ADR-0013 used for the
update manifest and ADR-0019 / ADR-0023 used for the proxy /
marketplace OpenAPI shapes.

## Risk register diff

- **R-013 / R-014 / R-016 closed.**
- **R-015 (external feedback):** third zero-volume week. The
  Sprint 15 amendment fired but the action behind it didn't
  happen. **Action moved to Sprint 17 push 1.** Likelihood at
  Sprint 17 entry: High (will be addressed); Impact still Med-Low.
- **R-017 (managed-AI proxy MVP):** /v1/chat works; the
  remaining 503s (/v1/account + /v1/quota) are now scheduled
  under the billing service (S16 push 3) + account portal
  (Sprint 17). **Closes** in spirit; Sprint 17 retro confirms.
- **R-018 (bridge ABI drift):** monitoring; no incidents.
- **R-019 / R-020 (ADR-0020 risks):** monitoring; no incidents.
- **R-021 / R-022 / R-023 (cloud sync):** scaffold-only; will
  surface during Sprint 16 push 1's S3 wire (actually Sprint
  17 — Sprint 16 was the marketplace sprint).
- **R-024 (Anthropic price shock):** Sprint 16's billing
  service has the mode/key check but not yet the
  hot-reloadable price table. **Action: Sprint 17 push 1
  wires the price table.**
- **R-025 / R-026 / R-027 (ADR-0023 risks):** scaffold-only;
  surface during Sprint 17 push 2's install body.
- **R-028 / R-029 / R-030 (ADR-0024 risks):** scaffold-only;
  surface during Sprint 17's account portal wire-up.
- **R-010 (velocity):** S13=30, S14=28, S15=25, S16=18. Trend
  is real — the per-flag / per-service template is cheaper
  with each repetition. Rolling median (4-sprint): 25. Sprint
  17 target stays 35 ± 15 to leave headroom for the
  marketplace install body + account portal which will be
  thicker than scaffolds.

## Capacity for Sprint 17

Sprint 16 ran ~18 pts. Sprint 17 target: 35 ± 15.

SPRINT_PLAN.md § Sprint 17 ("Hosted compute offload POC; account
portal; quota counter persisted"). Plus carry-overs:
- ADR-0025 + typed CLI `--json` shapes (~3 pts).
- Synth-load + VR baselines actual bootstrap PR (~3 pts).
- Launch-comms HN post + 5 partner emails (~2 pts operational).
- Account portal at `account.souxmar.dev` + Stripe.js iframe
  integration (~10 pts; Sprint 17 push 2).
- Plugin install body (marketplace download + verify + extract)
  (~6 pts; Sprint 17 push 3).
- Sprint 17 retro + v0.9.5 (~3 pts).

## Outcome

souxmar is at **v0.9.4** as of this commit. Three Pro-tier
service scaffolds + one billing-mediator scaffold all exist;
the architecture is ratified across ADRs 0019-0024; the CLI
side of paid-plugin install is wired (output contract +
license-key flag) waiting on Sprint 17's actual install body.

The ABI stays at v1.3 frozen final. The tool contract stays at
v1 frozen final with 18 tools. The bridge ABI stays at v3 (no
new FFI surfaces this sprint — Sprint 16 was a server-side +
CLI sprint). Six engine surfaces (CLI, Python, three desktop
panels via FFI, the four-service Pro-tier scaffold) all run on
the same core.

The Pro-tier business shape — managed-AI billed per token,
paid plugins billed via Stripe, cloud sync at three tiers — is
now nameable end-to-end. Sprint 17 turns the scaffolds into
running code; Sprint 22's public beta exercises the full path.
