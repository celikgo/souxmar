# Sprint 15 retro — Pro-tier services lit up + third FFI + ADR-0017 launch-comms amendment fires

**Closed:** 2026-05-14. **Pushes:** 5. **Theme:** "the Pro-tier
surfaces (proxy upstream + cloud-sync scaffold) move from
architecture to running code, the third real FFI lands, and the
ADR-0017 launch-comms trigger fires after two zero-volume weeks."

## What landed

| Push | Deliverable                                                                                              | Lines | ABI impact |
| ---- | -------------------------------------------------------------------------------------------------------- | ----- | ---------- |
| 1    | **services-build CI + INFRA_STATUS.md.** Compiles every Rust service under `services/*/` on linux + macos. Cross-cutting status doc names which gates are wired-but-empty + the "bootstrap PR" procedure. | ~200 | none |
| 2    | **ADR-0020 + per-project provider lookup.** Sibling `project.ai.toml` (gitignored by default) selects per-project AI provider. Engine-side `load_provider_config` + bridge integration; Ollama config now real, BYOK-anthropic/openai surface honest NOT_CONFIGURED until forwarders land. | ~685 | none — new optional file |
| 3    | **proxy Anthropic forwarder + cloud-sync MVP scaffold (ADR-0021).** Real reqwest-backed POST to api.anthropic.com replacing the proxy's /v1/chat 503. ADR-0021 + cloud-sync scaffold (axum binary, openapi.yaml, 503-on-everything MVP). | ~760 | none |
| 4    | **Third real FFI: `auto_updater_menu` (bridge ABI v3).** `updater.h` exports a read-only status surface; Inspector + Settings menu can render current + available version. ABI bumps 2 → 3; cross-check via existing per-call byte. | ~520 | bridge ABI v2 → v3 (additive — old clients refuse cleanly) |
| 5    | **Sprint 15 retro + v0.9.3 + launch-comms amendment.** This commit.                                     | this  | none |

Total LOC ~2,500 — between Sprint 13 (3,400) and Sprint 14 (2,000),
inside the rolling band.

## What to keep

- **The "scaffold the contract, stub the implementation"
  pattern repeats.** Sprint 14 ratified it (ADR-0019 +
  honest-503 proxy); Sprint 15 push 3 applied it again
  (ADR-0021 + honest-503 cloud-sync). Same shape, same
  user-experience-load-bearing artefact: an honest empty
  state, not a silent fallback. Pattern now reflexive enough
  that future service additions (Sprint 17's account
  portal) inherit without per-ADR rationale.
- **The "shell out to the CLI" decision for state-machine
  surfaces.** `auto_updater_menu` exposes *read* through FFI
  but not apply / rollback. Re-implementing the state machine
  through FFI would duplicate the v1.3-frozen surface; the
  shell-out keeps the source-of-truth single. Generalises to
  Sprint 16's plugin marketplace install path — the desktop's
  "Install plugin" click shells out to `souxmar plugin install`
  rather than re-implementing the registry-write side through
  FFI.
- **Bridge ABI version cadence.** Three sprints, three
  surfaces, three ABI versions (1 / 2 / 3) — one version
  per added surface, the per-call cross-check catches
  every partial-upgrade combo cleanly. Pattern is settled.
- **`.gitignore`-by-default for secrets-adjacent config.**
  Sprint 15 push 2's `project.ai.toml` joins
  `scripts/release/dev-signing-key.local.txt` (Sprint 10) +
  Tauri build outputs in the "gitignored by default; user
  opts in to share" category. The pattern is now applied
  consistently enough to be a project-wide convention.

## What to fix

- **synth-load + per-platform VR baselines still empty.**
  The Sprint 14 carry-over remains — the bootstrap requires
  a maintainer-eye on the CI artefact (per
  ADR-0017's first-batch-is-load-bearing argument). Three
  sprints have ratcheted around this without action. **Action:
  Sprint 16 push 1 commits the bootstrap PR.** The INFRA_STATUS
  dashboard adds a "stale-for-N-sprints" counter so the gap
  doesn't recede into invisibility.
- **Two consecutive zero-volume weeks** of external bug
  reports. ADR-0017's two-week trigger fires.
  **Action: launch-comms amendment** ratified this push.
  Sprint 16 push 1 schedules a Show HN post + partner
  outreach pass. This is **not** a strategy pivot — the
  external-first half of ADR-0017's bug-discovery model
  remains the headline.
- **Sprint 12 retro's operational follow-ups still open.**
  DNS CNAME (4 sprints), Discord server (4 sprints), on-call
  rotation table (4 sprints). All non-coding tasks; parked
  outside the sprint cadence. **Status: still parked.**
- **The Anthropic forwarder isn't end-to-end testable in
  CI** without an upstream key. The proxy's
  `cargo test`-able surface (via the `services-build`
  workflow) only covers the shape of the request building +
  the response parsing path on synthetic mocks. **Action:
  Sprint 17 push 1 (alongside the account portal) adds an
  Anthropic-mock service that the integration suite can
  point at — same pattern as the StubProvider on the engine
  side.**
- **The desktop menu UI for the new
  `auto_updater_menu` flag hasn't shipped a UI yet.** Push 4
  landed the bridge + the contract; the actual "About" or
  "Settings → Updates" panel in the React UI is queued.
  **Action: Sprint 16 push 2 lands the menu panel + the
  visual-regression coverage.**

## One ADR-worthy decision surfaced

**Sprint 15 push 4's `auto_updater_menu` deliberately
exposes only the *read* surface through FFI; apply / rollback
shell out to the CLI.** The decision is correct for the auto-
updater (state machine is frozen v1.3); the *pattern* needs an
ADR before Sprint 16's plugin marketplace install ratchets the
same shape.

The question: **when is a state-machine surface "off limits"
to FFI re-implementation?** Three candidate rules:

1. **Whenever the C++ side is on a frozen Tier-2+ contract.**
   Apply this to plugin install (ABI v1.3), updater state
   (state file schema=1), agent dispatcher (tool contract
   v1).
2. **Whenever the C++ side has a single-process invariant
   the FFI would break.** The updater's rollback log can
   only be safely appended-to from one process; concurrent
   apply from FFI + CLI would corrupt it.
3. **Whenever the user-visible outcome doesn't differ
   between FFI and CLI.** "Apply update" looks the same
   either way; the user clicks a button; the CLI runs in a
   helper process; the desktop polls for completion. The
   shell-out costs nothing user-visible.

ADR-0022 (queued for Sprint 16 push 1) folds all three rules
into the principle: **the C++ side's state-machine surfaces
expose READ through FFI + APPLY through subprocess shell-out.
This is the "MVC-via-subprocess" pattern, named.**

## Risk register diff

- **R-013, R-014, R-016 closed.** No change.
- **R-015 (external feedback signal)** — two consecutive
  zero-volume weeks. Launch-comms amendment fires this
  sprint. **Status: monitoring; Sprint 16 push 1's HN +
  outreach is the next event.**
- **R-017 (managed-AI proxy MVP incomplete)** — Sprint 15
  push 3's Anthropic forwarder addresses the chat surface;
  /v1/account + /v1/quota still 503 (Sprint 16 / 17). Risk
  partial-resolves; Likelihood drops from High to Med.
- **R-018 (bridge ABI version drift)** — bumped 2 → 3 this
  sprint as expected. The per-call cross-check would catch
  the partial-upgrade case; no incidents. **Status:
  monitoring.**
- **R-019, R-020 (ADR-0020 risks — silent stub fallback,
  config-file secret leakage)** — both stayed Low/Med after
  one sprint. Bridge surfaces config errors loudly per the
  ADR; .gitignore default prevents the leak by default.
  **Status: monitoring.**
- **R-021, R-022, R-023 (ADR-0021 risks — cross-tenant,
  binary cap, conflict UX)** — none materialised yet
  because the cloud-sync service is scaffold-only. Will
  surface during Sprint 16's S3-backed implementation. **Status:
  monitoring.**
- **R-010 (velocity)** — Sprint 15 ran ~25 pts. Rolling
  median: S13=30, S14=28, S15=25. Trending slightly down;
  the trend is "the second/third FFI surface is cheaper than
  the first" per the per-flag template costing less than its
  predecessor (Sprint 14's retro prediction). **Status:
  monitoring; the rolling-median model holds.**
- **New R-024 — Anthropic upstream cost shock.** The
  proxy's pre-charge model in ADR-0019 § 3 assumes a
  stable Anthropic price/token. A 2x price shift breaks
  the Pro-tier unit economics. **Likelihood: Low (Anthropic
  prices have been stable through 2025-2026); Impact: High
  (every Pro user underbilled).** **Mitigation:** Sprint 16's
  Stripe POC reads token prices from a config table the
  proxy hot-reloads.

## Capacity for Sprint 16

Sprint 15 ran ~25 pts:

| Push | Effort (pts) |
| ---- | ------------ |
| 1    | 3            |
| 2    | 6            |
| 3    | 8            |
| 4    | 6            |
| 5    | 2            |

Sprint 16 target: 35 pts ± 15. Themes per SPRINT_PLAN.md §
Sprint 16 ("Plugin marketplace v1: paid-plugin hosting,
Stripe integration, license-key flow").

Likely pushes:
- ADR-0022 + corpus / VR baseline bootstrap PR (~4 pts).
- Plugin marketplace v1 server-side ratchet from Sprint 10's
  index (~8 pts).
- Stripe-billing integration POC (proxy quota + plugin
  payments, shared Stripe customer record) (~10 pts).
- Plugin install side ratchet (CLI + desktop menu shell-out)
  (~6 pts).
- Sprint 16 retro + v0.9.4 + launch-comms result report
  (~3 pts).

## Outcome

souxmar is at **v0.9.3** as of this commit. Third dot-release
in 8 days. Three of six BridgeFeatureSet flags are structural;
the bridge ABI advances to v3. The Pro-tier proxy can forward
a real chat request to Anthropic (modulo the operator
configuring the upstream key); the cloud-sync service exists
as an addressable HTTP endpoint with an honest 503 surface
ready for Sprint 16's S3 wire.

The ABI stays at v1.3 frozen final. The tool contract stays
at v1 frozen final with 18 tools. Same engine, six peer
surfaces (CLI, Python library, desktop inspector via FFI,
desktop chat via FFI, desktop updater-menu via FFI, managed-
AI proxy via shared types).

ADR-0017's launch-comms amendment fires; Sprint 16 push 1
schedules the HN + outreach pass. The synthetic-load harness
+ VR matrix continue to ship the gate without the data —
Sprint 16 push 1's bootstrap PR closes that gap as part of
the same comms-event PR.
