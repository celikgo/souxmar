# Sprint 11 retro — Internal-alpha foundations: crypto extraction + HTTPS fetcher + eval suite expansion + workbench shell scaffold

**Closed:** 2026-05-11. **Pushes:** 5. **Theme:** "close Sprint 10's carry-overs, scaffold the workbench, set up the v2 eval gate — *minus* the dogfood week that the SPRINT_PLAN.md theme called for."

Sprint 11 was the carry-over sprint, with a substantial caveat: the headline story in `SPRINT_PLAN.md` ("Dogfood week 1: every engineer runs a real analysis using only the desktop app + chat") is unreproducible without a team. This retro is honest about that — Sprint 11 closes most of the planned work and explicitly carries the dogfood XL forward to Sprint 12 (where the public alpha makes external users *the* dogfooders).

The honest framing: **Sprint 11 paid down the Sprint 10 retro's debt and scaffolded the surfaces Sprint 12 will fill in.** Five substantive pushes:

## What landed

| Push | Deliverable                                                                            | Lines  | Frozen-header impact |
| ---- | -------------------------------------------------------------------------------------- | ------ | -------------------- |
| 1    | **libsouxmar-crypto extraction (ADR-0015) + Playwright visual-regression harness.** Closes Sprint 10 carry-over: split the verifier + sha256 + hex helpers out of souxmar_update into souxmar_crypto so plugin marketplace (S16) + cloud sync (S15) don't each grow their own copy. Non-breaking — `souxmar::update::*` forwarders preserved. Plus the Playwright harness for the onboarding wizard (closes R-012). | ~870   | none                 |
| 2    | **`souxmar update fetch` HTTPS downloader + auto-regen dev signing key.** Closes Sprint 10 carry-over: the previously-deferred network fetch for the auto-updater (curl-as-subprocess, same pattern OllamaProvider validated). Plus the CMake configure-time auto-regen of the in-tree dev keypair when missing — closes the "developer hits the placeholder pubkey with no helpful hint" foot-gun. | ~630   | none                 |
| 3    | **Agent eval suite expansion + `--min-pass-rate` gate.** 30 → 43 tasks across 6 new categories (export, listing, cfd, multistep, recovery, screenshot); new flag wired into the nightly eval workflow at 0.90. The 17 remaining tasks toward the 60-task target ride into Sprint 12 as the catalogue stabilises against external feedback. | ~360   | none                 |
| 4    | **Desktop workbench shell scaffold (viewport + chat + inspector).** Three-panel layout under `src/desktop/src/workbench/` + the new `src/desktop/src/chat/` panel. Viewport + inspector are honest "no-project" empty states (no fake-mesh rendering); the chat panel is fully exercisable in `tauri dev` via a stubbed `chat_send` command. Two new Playwright baselines (workbench-empty, workbench-after-message). | ~530   | none                 |
| 5    | **Sprint 11 retro + v0.9.0-beta5 release.** This commit.                                 | this   | none                 |

**Total LOC: ~2,400 across the sprint.** Markedly lower than Sprint 10's ~11,700 — partly because the auto-updater XL is closed, partly because the SPRINT_PLAN.md headline (dogfood week) couldn't land. The next sprint's actual LOC will depend on how much external feedback the public alpha generates.

## What to keep

- **The pattern of extracting shared primitives at second-consumer-discovery time, not first-use time.** ADR-0015 extracts `souxmar::crypto::*` from `souxmar::update::*` *before* the plugin marketplace + cloud sync each duplicate the wrapper. The Sprint 10 retro flagged this as an ADR-worthy decision and Sprint 11 push 1 ratified it. Cost of doing it now: one push (3 hours). Cost of doing it after three consumers had independently grown it: ~3 pushes plus the migration risk of three trees diverging on a security-relevant surface. The "factor early, but only at the second use" heuristic generalises — queueing it for the on-disk pipeline format + the agent runtime message protocol when their second consumers land in S13+.
- **The "honest stub" empty-state pattern.** Sprint 11 push 4's viewport refuses to render a fake mesh; the inspector tells the user that "pipeline introspection arrives in Sprint 12+"; the chat surface clearly labels the scaffolding reply. This trades user-perceived completeness for user-perceived honesty. Confirmed in the Sprint 10 retro's "what to keep" as the dim-theme "structure through hierarchy, not borders" philosophy applied to UX *truth-telling*: a half-built feature should look half-built, not pretend to work.
- **Non-breaking forwarder migrations for module splits.** Push 1's crypto extraction added `souxmar::crypto::*` *while* keeping the original `souxmar::update::*` entry points as thin forwarders. Zero downstream code needed re-cutting; the migration is opportunistic over the next several sprints. This is the right shape for every future "lift this primitive into a sibling module" event.
- **Curl-subprocess as the project's HTTP client pattern.** Sprint 10 push 9 introduced it for the OllamaProvider; Sprint 11 push 2 extended it to the auto-updater fetch path. Both modules stay HTTP-client-dep-free; both reuse the Sprint 8 push 1 subprocess harness. The pattern's ~5 ms / call overhead is invisible against typical use cases (seconds-scale LLM inference; minutes-scale artifact downloads). Sprint 14's managed-AI proxy may re-evaluate if sub-100ms calls land in scope, but the pattern is the right default for the project's "single-binary, small-dep" stance.

## What to fix

- **Dogfood week didn't happen.** The SPRINT_PLAN.md theme presumed a team running real analyses through the desktop app. Without that, Sprint 11's "bug-fix bundle" push (planned push 5) collapsed into the close-out. **Action: Sprint 12 (public alpha) inherits the dogfood XL — the alpha *is* the dogfood, with external users as the dogfooders. Bug triage workflow lands in S12 push 1 alongside the alpha cut so feedback has a defined receiving surface.**
- **Three Sprint 10 carry-overs landed; one remains.** The "VTU consumer conformance check" (compare.py XML-shape coupling) noted in the Sprint 10 retro § "what to fix" did NOT close in Sprint 11. **Action: this is now a Sprint 12+ thing — the writer.vtu adapters need a small conformance test that locks the emission shape against the in-tree consumer (compare.py + the desktop viewport once Three.js wires up).**
- **Visual-regression baselines aren't committed.** Pushes 1 + 4 ship the Playwright harness + the spec files + the workflow plumbing, but the baseline PNGs land on first CI run. The README documents the bootstrap procedure (commit baselines from CI artefacts), but until they're committed the harness can't gate. **Action: Sprint 12 push 1 commits baselines from the first CI artefact run.**
- **The workbench's three panels are all "Sprint 12+"-deferred for their primary content.** Viewport needs Three.js wiring, inspector needs pipeline introspection, chat needs a real provider call. All three depend on the same souxmar-bridge FFI crate. **Action: Sprint 12 push 2 ships the souxmar-bridge skeleton + the first FFI surface (the pipeline-introspection one — smallest, no rendering); pushes 3+ fill in viewport + chat against it.**
- **Sprint 11 ran light (~25 pts vs 50 forecast).** Two consecutive sprints have now varied from the forecast in opposite directions — Sprint 10 overshot by ~46% (~95 pts vs 65), Sprint 11 undershot by ~50% (~25 pts vs 50). Variance is bigger than the median. **Action: Sprint 12 forecast stays at 50 pts; if the third sprint also misses by > 30 %, the trend warrants modelling rather than re-forecasting. R-010 (hiring + velocity) stays open but unchanged.**

## One ADR-worthy decision surfaced

**The desktop app's "real provider calls vs. stub" boundary needs a named contract.** Sprint 11 push 4 wired the chat panel to a `chat_send` Tauri command that today returns a deterministic acknowledgement; the real provider call routes through the souxmar-bridge FFI crate which doesn't yet exist. Three months from now there will be at least four places this boundary matters: chat (real provider) vs. viewport (real mesh data) vs. inspector (real pipeline state) vs. settings (managed-AI proxy config). Without a named contract for "what does Tauri-bridge-to-FFI return today and what does it return at v1.0?", each one will reinvent its own scaffolding-vs-real toggle.

The decision worth ADR'ing is the **FFI boundary contract: a single `BridgeFeatureSet` struct returned at app startup that names which surfaces are wired** (`viewport_renderer`, `pipeline_introspection`, `provider_call`, `keychain_write`, …). The chat panel + inspector + viewport each query the set and render accordingly. New surfaces opt into the contract by adding a flag; old code never has to guess at availability.

This is straightforward (~half a push) and prevents three sprints of ad-hoc "is this real yet?" toggles. **ADR-0016 candidate; Sprint 12 push 2 lands it alongside the souxmar-bridge skeleton.**

## Risk register diff

- **R-012 (desktop visual-regression coverage absent)** — closes with push 1's harness landing + push 4's workbench spec. The harness is wired; baselines commit on first CI run. The "wired but not yet gating" state is acceptable for the public-alpha window since the workbench is so scaffolded.
- **R-003 / R-001 / R-006 / R-009** — no change.
- **R-010 (hiring + velocity)** — Sprint 11 ran light at ~25 pts. Combined with Sprint 10's overshoot, the forecast variance is widening. No new action; observe for one more sprint before re-modelling.
- **New risk R-013 — souxmar-bridge FFI crate doesn't exist.** **Likelihood: Closes in Sprint 12 push 2 (planned); Impact: Med while open.** Every workbench surface (viewport / chat / inspector) waits on this single Rust crate. If Sprint 12 push 2 slips, the desktop app's "real functionality" milestone slips with it. **Mitigation:** the workbench's empty states are honest about the dependency; users see "(Sprint 12+)" notes rather than fabricated functionality. A slip is therefore visible-but-not-broken.
- **New risk R-014 — public-alpha bug-triage workflow doesn't exist.** **Likelihood: Closes in Sprint 12 push 1; Impact: High while open.** Sprint 12's "public alpha" exit criterion presumes a triage rotation handling external reports within 48h. None of that infrastructure exists today. **Mitigation:** Sprint 12 push 1 is dedicated to the triage workflow (Discord / GitHub Discussions / issue-template).

## Capacity for Sprint 12

Sprint 11 ran ~25 pts measured. Sprint 11's per-push effort breakdown:

| Push | Effort (pts) | Note                                                                |
| ---- | ------------ | ------------------------------------------------------------------- |
| 1    | 8            | Crypto extraction + ADR-0015 + Playwright harness (two carry-overs) |
| 2    | 8            | HTTPS fetcher + dev key auto-regen (two carry-overs)                |
| 3    | 5            | Eval suite +13 tasks + min-pass-rate gate                           |
| 4    | 8            | Workbench scaffold (3 panels + chat stub + tauri commands + specs) |
| 5    | 3            | Retro + release cut (this commit)                                   |

Sprint 12 plan target: **50 pts.** SPRINT_PLAN.md § Sprint 12 themes:
- First public release `v0.9.0`; signed installers on download page (M)
- Public docs site (Vitepress / Docusaurus); API reference auto-generated (XL)
- Discord / GitHub Discussions live; triage rotation set up (M)
- Bug-bash on candidate build, fix any P0/P1 (L)
- Launch comms (HN, blog, mailing list, partner outreach) (M)

Plus the carry-overs from this sprint's "what to fix":
- VTU consumer conformance check (3 pts)
- Commit visual-regression baselines from CI (1 pt)
- souxmar-bridge FFI crate skeleton + BridgeFeatureSet (ADR-0016) (8 pts)
- Bug-triage workflow + issue templates (5 pts)

The S12 push pace will be set by the public alpha's launch-day load (HN traffic + Discord triage + the first batch of external bug reports). Plan against 50 pts; if the alpha generates substantial external feedback, the bug-fix portion can run hot.

## Outcome

souxmar is at **v0.9.0-beta5** as of this commit. The fifth public pre-release closes most of the Sprint 10 retro's carry-overs (libsouxmar-crypto extraction, HTTPS fetcher, dev key auto-regen, Playwright visual harness), expands the agent eval suite from 30 to 43 tasks with a ≥ 90 % pass-rate gate wired into nightly CI, and scaffolds the desktop workbench shell. The ABI stays at v1.3 frozen final; the tool contract stays at v1 frozen final with 18 tools.

What's notably *not* shipped: the dogfood week from SPRINT_PLAN.md § Sprint 11. The team-running-real-analyses-through-the-app loop can't be reproduced without a team. The honest reframing — Sprint 12's public alpha is where dogfooding actually happens, with external users — moves the missing exit criterion forward by one sprint.

The next sprint cuts the public alpha. Every workbench surface (viewport / chat / inspector) is honestly empty-stated until the souxmar-bridge FFI crate lands in Sprint 12 push 2; the public alpha's docs surface (Vitepress site, API reference) + the triage rotation (Discord, GitHub Discussions, issue templates) are the other XL story. The auto-updater channel + the plugin marketplace v0 + the AI provider abstraction + the cryptographic trust paradigm are all v0.9.0-beta5 surfaces that external users will exercise from launch day. Sprint 12 is the first sprint where souxmar's surface materially leaves the internal team.
