# Sprint 14 retro — Managed-AI proxy scaffold + second real FFI + Sprint 13 carry-overs drained

**Closed:** 2026-05-13. **Pushes:** 5. **Theme:** "infrastructure
push — the Pro-tier business shape gets a scaffold, the second of
six BridgeFeatureSet flags flips structural, and every Sprint 13
carry-over closes."

Sprint 13 left three open items: synth-load corpus seeding (push 1
of this sprint), release-CI C-bridge integration (push 2),
auto-generated agent docs first real render (Sprint 14+ ongoing —
gated by first green CI). Sprint 14 lands the first two as
infrastructure that enables but doesn't require the third.
Headline deliverable: managed-AI proxy MVP scaffold + ADR-0019
ratifying the Pro-tier architecture.

## What landed

| Push | Deliverable                                                                                              | Lines | Frozen-header impact |
| ---- | -------------------------------------------------------------------------------------------------------- | ----- | -------------------- |
| 1    | **synth-load `--bootstrap` mode + per-platform VR matrix.** The harness ships with the mechanism to seed itself in one maintainer-reviewed pass; new `.github/workflows/visual-regression.yml` runs the Playwright suite on ubuntu-22.04 + macos-14 + windows-2022, each producing its own snapshot directory. | ~270 | none |
| 2    | **desktop-ffi CI workflow + COMMUNITY.md → bug-reports link.** New `.github/workflows/desktop-ffi.yml` builds `libsouxmar-c-bridge.a` + the Rust souxmar-bridge crate with `--features real-ffi` on linux + macos. R-016 mitigation. Plus the carry-over doc fix. | ~135 | none |
| 3    | **Managed-AI proxy MVP scaffold + ADR-0019.** New `services/managed-ai-proxy/` directory tree — axum + tokio binary with three v1 endpoints returning honest 503s. ADR-0019 names the five architectural decisions: stateless proxy, opaque souxmar tokens (not upstream keys), pre-charge per token with in-flight holds, separate binary outside the workspace, mutual coexistence with BYOK. | ~895 | none — new `services/` dir |
| 4    | **Second real FFI: `provider_call` (bridge ABI v2).** New `include/souxmar-c-bridge/provider.h` + `src/c-bridge/provider.cpp`. Routes ChatRequest JSON through the engine's Provider abstraction (today: StubProvider). Bridge ABI version bumps 1 → 2; `BridgeFeatureSet::provider_call` flips structural. React Chat panel renders typed error surfaces (`(provider <name> returned <kind>: <text>)`) instead of generic failures. | ~705 | none — additive bridge ABI growth (ADR-0018 § "adding a function is non-breaking") |
| 5    | **Sprint 14 retro + v0.9.2 cut.** This commit.                                                          | this  | none — `v0.9.2` |

**Total LOC: ~2,000.** Smaller than Sprint 13's ~3,400 because
the second FFI surface re-used Sprint 13's template (header +
impl + binding + wrapper + tests) instead of inventing it. The
expected pattern as we work through the remaining four
BridgeFeatureSet flags: each subsequent flag should cost less
than its predecessor.

## What to keep

- **The "scaffold the contract, stub the implementation" pattern.**
  ADR-0019 ratifies the proxy architecture without committing to
  the implementation timeline. The MVP scaffold returns honest
  503s; the desktop client's "Pro provider offline" empty state
  is the load-bearing user-experience artefact, not the upstream
  forwarder that lands in Sprint 15. This shape lets a sprint
  land an architectural decision *and* a working chain (config →
  bridge → desktop empty state) before the heavy implementation
  arrives. Reproducible for Sprint 17's account portal (scaffold
  the API + the empty-state copy before the database lands) and
  Sprint 21's pen-test work (scaffold the threat model before
  the assessment).
- **"The ABI byte names the surface."** Sprint 13's
  `souxmar_bridge_abi_version()` cross-check + Sprint 14's bump
  from 1 to 2 means there is one number that says "this build's
  bridge surface." Old desktop builds linked against bridge v1
  cross-check this byte and refuse the call cleanly. Pattern
  generalises — the plugin C ABI v1.3 + the agent tool contract
  v1 follow the same shape, but the bridge is the first surface
  where the byte advances *during the v0.9.x window*. The
  forwarders pattern from ADR-0015 + the additive growth pattern
  from ADR-0013 fold in naturally.
- **Cargo features as the FFI gating mechanism.** Sprint 14's
  desktop-ffi workflow validates `cargo build --features real-ffi`
  works against a freshly built static archive. The Rust
  workspace continues to `cargo check` cleanly without the
  feature on, so developers / IDE intellisense don't need the
  C++ build available. Sprint 17+'s viewport FFI (Three.js wiring
  the third real FFI flag) follows the same template.
- **Per-platform snapshot directories.** Sprint 14 push 1's
  Playwright `snapshotPathTemplate` change is small but
  load-bearing — macOS font rendering does not need to false-
  positive against the Linux baseline. The matrix workflow
  produces three independent artefacts; the maintainer reviews
  three sets of renders before committing the initial baselines.
- **Typed error surfaces in user-visible copy.** The Chat panel's
  new "(provider stub returned BadRequest: …)" text is one of
  the first user-visible places where a typed engine error
  becomes a user-readable string. Pattern generalises — when
  Sprint 15's real Anthropic provider returns RateLimited, the
  user sees "via anthropic: RateLimited (Retry-After 5s)" not
  "(provider call failed)".

## What to fix

- **The synth-load corpus is still empty.** Sprint 14 push 1
  shipped the `--bootstrap` mechanism but didn't run it — that
  requires the first green CI of the post-v0.9.1 era plus a
  maintainer hand on the artefact. **Action: Sprint 15 push 1
  ratchet — run the bootstrap on a no-op PR, review the
  corpus.toml diff, commit, flip
  `continue-on-error: false` in eval-nightly.**
- **VR baselines are still empty per-platform.** Same shape as
  synth-load; Sprint 15 push 1 runs the matrix workflow on a
  no-op PR, downloads the three platform artefacts, commits.
- **No external bug reports landed this sprint.** Volume still
  zero in the second week of v0.9.0/v0.9.1. ADR-0017's first
  amendment trigger (two weeks of zero) fires. **Action: Sprint
  15 push 5 (retro) names the launch-comms amendment — *not*
  a strategy pivot; a focused acknowledgement that the
  external-first half is not yet generating signal because
  visibility is the constraint, and a queued action to
  schedule a Show HN post + a partner outreach pass.**
- **The managed-AI proxy doesn't build in CI yet.** Sprint 14
  push 3 ships the binary scaffold but no workflow exercises
  `cargo build` against it. **Action: Sprint 15 push 1 adds a
  small `services-build.yml` workflow that compiles every
  service in `services/*/` on linux + macos.**
- **The Sprint 12 retro's operational follow-ups are still
  open** — DNS CNAME, Discord, on-call rotation table.
  Unmoved again because they're not coding tasks. **Status:
  parked outside the sprint cadence. Sprint 15 retro reports
  status; no action item.**
- **`docs-site/agents/tools.md` is still the placeholder.**
  The generator + the CI check are wired; the actual first
  render is gated by a green CI run. **Action: Sprint 15 push 1
  commits the first generated content as part of the synth-load
  + VR bootstrap PR.**

## One ADR-worthy decision surfaced

**The chat panel's per-project provider routing.** Sprint 14
push 3's ADR-0019 § 5 names the `ai.provider` field on
`project.souxmar.toml` ("managed" / "byok-anthropic" /
"byok-openai" / "ollama") + the rule that the two are mutually
exclusive per-project but coexist across the project list. The
desktop client today doesn't *read* this field; the bridge's
`chat_send` returns whatever the engine's StubProvider returns
regardless of project.

The decision worth recording in a future ADR: **what's the
per-project provider override format on disk?** Two viable
shapes:

1. **Inline in `project.souxmar.toml`** under a top-level
   `[ai]` section, alongside the project's other config. Pro:
   one file. Con: the provider config might carry secrets
   (BYOK key references); a project file is normally checked
   into git.

2. **Separate `project.ai.toml` sibling.** Pro: cleanly
   separates secrets-adjacent config from the project's
   pipeline definition; the sibling is `.gitignore`d by
   default. Con: two files where one would do.

ADR-0019 deferred this. Sprint 15 push 2's per-project provider
lookup forces the decision — it needs to read *something*
off disk. The ADR (-0020 candidate) writes the answer down
before the engine-side code commits to a path.

## Risk register diff

- **R-013, R-014** — closed in Sprint 12; no change.
- **R-015 (external feedback signal)** — still starves
  (volume = 0). Trigger condition (two weeks of zero) fires.
  ADR-0017's "launch-comms amendment" path activates; Sprint
  15 retro is the documenting moment. **Likelihood: High at
  Sprint 15 entry; Impact: Med-Low (signal absence is itself
  signal — see Sprint 15 retro plan).**
- **R-010 (velocity model)** — Sprint 14 ran ~28 pts measured,
  inside the 35±15 band (second consecutive). The rolling
  median moves: Sprint 13 = 30, Sprint 14 = 28. After Sprint
  15: median 28-30, narrow. **Status: monitoring — the
  rolling-median model is the right one, and the absolute
  number is settling.**
- **R-016 (release CI doesn't build C bridge with real-ffi)**
  — **closes** with Sprint 14 push 2. The desktop-ffi
  workflow validates the build path; release-shipped desktop
  installers are a Sprint 22+ public-beta concern. R-016 retires.
- **R-017 (managed-AI proxy MVP is incomplete by Sprint 14
  retro)** — predicted High likelihood at registration;
  resolved as expected. The scaffold landed; the
  implementation accumulates through Sprints 15-17.
  Likelihood unchanged; Impact stays Low because BYOK is the
  headline UX until Sprint 17. **Status: monitoring —
  Sprint 15 push 1 lands the Anthropic forwarder.**
- **R-003 / R-001 / R-006 / R-009 / R-011 / R-012** — no
  change.
- **New risk R-018 — bridge ABI version drift between desktop
  builds and shared C bridge.** The ABI byte now reads v2;
  any in-flight desktop build pre-Sprint-14-push-4 carries v1
  bindings. **Likelihood: Low (release pipeline rebuilds both
  together); Impact: Med (failed call with a typed
  AbiMismatch error — recoverable, not silent corruption).
  Mitigation:** the ABI cross-check on every call (Sprint 13
  push 3 ADR-0018 § 5) catches this loudly.

## Capacity for Sprint 15

Sprint 14 ran ~28 pts measured:

| Push | Effort (pts) | Note                                                             |
| ---- | ------------ | ---------------------------------------------------------------- |
| 1    | 4            | synth-load --bootstrap + per-platform VR matrix                  |
| 2    | 2            | desktop-ffi workflow + COMMUNITY.md fix                          |
| 3    | 10           | Managed-AI proxy MVP scaffold + ADR-0019                         |
| 4    | 10           | Second real FFI (provider_call) + bridge ABI v2 + tests          |
| 5    | 2            | Retro + release cut (this commit)                                |

Sprint 15 target: **35 pts ± 15** (unchanged from S13/S14;
the rolling-median model is the right one). Themes per
SPRINT_PLAN.md § Sprint 15 ("Cloud sync MVP for Pro tier;
encrypted-at-rest, end-to-end if Enterprise"):

Likely pushes:
- synth-load + VR baselines initial commit (~3 pts; the
  bootstrap PR Sprint 14 promised).
- ADR-0020: per-project provider override format (~3 pts).
- Anthropic forwarder for managed-ai-proxy (~10 pts; the
  Sprint 15 / Sprint 16 split — forwarder this sprint, Stripe
  billing-event emitter next).
- Cloud sync MVP scaffold + ADR-0021 (~8 pts; per Sprint 15
  theme).
- services-build.yml CI workflow (~2 pts).
- Sprint 15 retro + v0.9.3 — first retro to fire ADR-0017's
  launch-comms amendment trigger (~3 pts).

External-feedback volume (R-015) remains the wildcard; if
the launch-comms amendment lands during the sprint, push 5
gets shifted.

## Outcome

souxmar is at **v0.9.2** as of this commit. **Second dot-
release after the public alpha.** One day after v0.9.1.

Two of six BridgeFeatureSet flags are now structural:
`pipeline_introspection` (Sprint 13) + `provider_call` (this
sprint). Four to go (`viewport_renderer` Sprint 17;
`auto_updater_menu` Sprint 15; `keychain_write` already true;
one more potentially added for cloud sync per the additive
ADR-0016 contract).

The ABI stays at v1.3 frozen final. The tool contract stays
at v1 frozen final with 18 tools. The same C++ engine powers
five surfaces — CLI, Python library, desktop inspector (via
FFI since Sprint 13), desktop chat (via FFI since this
sprint), and indirectly the managed-AI proxy (via shared
header types since this sprint). No privileged path, as
designed.

What's **not** in v0.9.2: viewport rendering (Sprint 17), the
Anthropic forwarder in the proxy (Sprint 15), cloud sync
(Sprint 15), paid plugin marketplace (Sprint 16), account
portal (Sprint 17), security audit (Sprint 21). The honest
framing — Sprint 14's exit criterion was "second FFI flag
flipped + proxy architecture ratified + Sprint 13 carry-overs
drained" — is met *modulo* the carry-overs from this sprint
(corpus initialisation, VR baselines, services-build
workflow) named in "what to fix" above.

Sprint 15 starts measuring whether the launch-comms amendment
moves the external-feedback volume, and lands the proxy's
first real upstream call alongside the cloud-sync MVP
scaffold.
