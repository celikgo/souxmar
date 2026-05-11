# Sprint 12 retro — Public alpha: triage workflow + souxmar-bridge skeleton + docs site live + v0.9.0 cut

**Closed:** 2026-05-11. **Pushes:** 4. **Theme:** "make souxmar findable, reportable, and consumable from outside the team."

Sprint 12 is the inflection point. Up to v0.9.0-beta5 the project was a *contributor*-facing surface — engineers checking the repo out and building from source. Sprint 12 lands the three pieces that make it a *consumer*-facing surface: an external bug-triage workflow, the FFI-boundary contract subsequent UI work flows through, and a public docs site at docs.souxmar.dev.

The dropped story from Sprint 11 ("dogfood week") is reframed: **the public alpha cut by this sprint is the dogfood**, with external users as the dogfooders. Bug reports will flow into the triage workflow this sprint built; the Sprint 13 retro will measure how that first week went.

## What landed

| Push | Deliverable                                                                              | Lines  | Frozen-header impact |
| ---- | ---------------------------------------------------------------------------------------- | ------ | -------------------- |
| 1    | **Bug-triage workflow + COMMUNITY.md.** New `docs/COMMUNITY.md` with response SLAs (P0 24h, P1 48h, P2 5bd, P3 2w) + priority definitions + maintainer rotation table + public-alpha expectations. New `.github/workflows/triage.yml` that auto-acknowledges every new issue with the matching SLA and auto-labels by surface keyword. Closes R-014. | ~190   | none                 |
| 2    | **souxmar-bridge FFI skeleton + BridgeFeatureSet contract (ADR-0016).** New Rust crate `src/desktop/src-tauri/souxmar-bridge/` exposing a single struct that names which workbench surfaces are wired vs. scaffolding. React-side hook + TypeScript mirror + fallback for `vite preview` / Playwright. Closes R-013; opens the path for Sprint 13's first real FFI calls to flip individual flags on. | ~550   | none — contract is in `souxmar-bridge::BridgeFeatureSet`, tier-2 from this push onward |
| 3    | **Vitepress docs site scaffold at docs.souxmar.dev.** New `docs-site/` directory with Vitepress 1.5 config + landing page + install guide + first-pipeline tutorial + agent reference + plugin authoring + business-model page. `.github/workflows/docs-site.yml` builds + publishes to GitHub Pages on every master push touching `docs-site/`. | ~880   | none                 |
| 4    | **Sprint 12 retro + v0.9.0 public alpha cut.** This commit.                                 | this   | none — v0.9.0 = first non-`-beta`-suffixed tag in the project's history |

**Total LOC: ~1,800.** Smaller than Sprint 11's ~2,400 because the surfaces this sprint lands are *infrastructure* — workflow YAML, a skeleton Rust crate, Vitepress pages — not engine code. The next sprint's LOC will scale with the volume of external feedback the alpha generates.

## What to keep

- **The two-source-of-truth doc split.** `docs/` in the repo is contributor-facing (ADRs, governance, retros, RFCs); `docs-site/` is the public Vitepress source served at docs.souxmar.dev. The split lets each audience evolve at its own cadence — a sprint retro doesn't bloat the install guide; a Vitepress nav-bar redesign doesn't churn ADR PRs. Reproducible for future audience-specific docs (e.g. a separate `enterprise-docs/` if Sprint 20's Team/Enterprise tier needs different prose for buyers).
- **BridgeFeatureSet as a typed contract for "what's wired."** ADR-0016 ratifies the pattern + names the stability rules (additive Tier-0; rename Tier-2). Every panel queries the same struct instead of inventing its own toggle. The next time we hit a similar "this feature exists in some builds but not others" question — managed-AI proxy endpoints, cloud-sync config — there's a precedent for either extending the struct or adding a sibling (`BridgeProviderConfig`, `BridgeCloudConfig`).
- **Workflow-as-onboarding for external contributors.** The triage workflow's auto-acknowledgement comment (push 1) is the *first interaction* an external bug reporter has with the project. Naming the SLA there — "a response within 48 hours per `docs/COMMUNITY.md`" — sets the tone better than a generic "thanks for filing!" The bot is a 50-line YAML file; the user-experience signal it sends is load-bearing.
- **The "honest scaffolding" UI pattern, extended.** Sprint 11 introduced empty-state honesty in the workbench panels; Sprint 12 extends it through the BridgeFeatureSet flags. Every panel's "real vs. scaffolding" branch is a single `if (features.X)` — the user sees a coherent "this build is partly wired" story instead of three differently-worded "Sprint 12+" notes.

## What to fix

- **No auto-generated API reference.** The `/agents/tools` page on docs.souxmar.dev is hand-curated; if the tool catalogue changes (between v0.9 and v1.0 it will), the page drifts. **Action: Sprint 13 push 1 wires `souxmar agent list --json` → a Vitepress page generator** so the public docs match the binary 1:1 from build time.
- **The Sprint 11 carry-overs queued for Sprint 12 didn't all close.** VTU consumer conformance + the visual-regression baseline commits from CI are *not* in this sprint. The workbench-shell wiring took priority because Sprint 12's exit criterion was the public alpha; the two carry-overs are now Sprint 13's. **Action: Sprint 13 push 1 commits VR baselines from the first post-v0.9.0 CI run + adds a small VTU XML-shape conformance test the writer adapters run against during the standard test suite.**
- **No CNAME setup for docs.souxmar.dev.** The Vitepress site publishes to the default GitHub-Pages URL; until DNS is wired (one-time out-of-band setup), the public docs URL is the long form. **Action: configure the DNS CNAME during launch comms (push 4 followup; not gated by the alpha cut itself).**
- **The public alpha goes out without a real Discord server.** The COMMUNITY.md page references `souxmar.dev/community` as the invite redirect; the redirect target needs to be set up alongside the launch announcement. **Action: launch-comms push (queued for Sprint 13) wires the Discord + the redirect together; until then the link 404s.**
- **No on-call rotation populated.** The COMMUNITY.md maintainer-rotation table is `TBA` for v0.9.0. With one contributor today the rotation is trivially "the contributor covers everything"; once the team builds the table fills in. **Action: continue populating as the team builds; no immediate action.**
- **Sprint 12 ran light again (~20 pts vs 50 forecast).** Third consecutive sprint missing the forecast — Sprint 10 overshot by ~46 %, Sprint 11 undershot by ~50 %, Sprint 12 undershot by ~60 %. The three-sprint trend matters. **Action: the forecast model is broken; switch from "fixed pts target per sprint" to "rolling 4-sprint median (~50 pts measured)" + an explicit per-sprint variance band.** Re-baseline the next sprint at 35 pts with a ±15 pts variance band. R-010 stays open, with the action plan now "rework velocity model" rather than "observe one more sprint."

## One ADR-worthy decision surfaced

**The pattern of "the *public alpha cut* is the dogfood" — releasing a v0.9.0 to external users before the team has driven extensive internal usage — needs a decision artefact.** SPRINT_PLAN.md presumed an internal dogfood week preceding the public alpha; Sprint 11 documented that step's deferral; Sprint 12 actioned the deferral by cutting v0.9.0 anyway.

The question worth ADR'ing: **what's the bug-discovery model for souxmar's public alpha** — internal team load, external user load, or both?

Three plausible shapes for ADR-0017 (queued for Sprint 13 push 1):

1. **External-first.** v0.9.0 ships; bugs flow from real users; Sprint 13+'s primary work is fixing what comes in. Risks: the first month's reports are skewed toward whatever workflow the early adopters happen to try.
2. **Synthetic-load-augmented external.** v0.9.0 ships; *in parallel*, a synthetic-load harness generates pipeline runs across the example catalogue + records any regressions vs. golden outputs. Combines external signal with breadth coverage. Cost: ~1 sprint to build the harness.
3. **Team-first (orthodox).** Delay v0.9.0 until the team has driven the internal dogfood week. Cost: weeks of slip + the public-alpha announcement window.

Sprint 12 chose option 1 implicitly. Sprint 13's ADR-0017 ratifies that choice (or pivots — but the decision is reversible only at the cost of pulling v0.9.0). The ADR documents the rationale + the metrics we'll judge it against ("≥ 20 external bug reports in the first month; ≥ 50% of P0/P1s fixed within the SLA").

## Risk register diff

- **R-013 (souxmar-bridge FFI crate doesn't exist)** — **closes** with push 2. The skeleton exists; the contract is ratified; individual surfaces flip flags on in subsequent pushes.
- **R-014 (public-alpha bug-triage workflow doesn't exist)** — **closes** with push 1.
- **R-010 (hiring + velocity)** — three-sprint trend: model is broken. New action plan above.
- **R-003 / R-001 / R-006 / R-009 / R-011 / R-012** — no change.
- **New risk R-015 — first-week external feedback signal could overwhelm or starve the triage rotation.** **Likelihood: Sprint 13 reveals; Impact: Med.** If launch comms (HN, blog, partner outreach) lands well, the volume of external bugs may exceed the one-contributor rotation's capacity. If it lands badly, the rotation starves and we learn nothing from real users. **Mitigation:** weekly retro pulse in Sprint 13+ measuring the report-arrival rate vs. the SLA-met rate; if either tail hits, we know within a week.

## Capacity for Sprint 13

Sprint 12 ran ~20 pts measured:

| Push | Effort (pts) | Note                                                                |
| ---- | ------------ | ------------------------------------------------------------------- |
| 1    | 5            | COMMUNITY.md + triage workflow + CONTRIBUTING update                |
| 2    | 8            | souxmar-bridge crate skeleton + ADR-0016 + React-side wiring        |
| 3    | 5            | Vitepress site scaffold + 6 starter pages + publish workflow        |
| 4    | 2            | Retro + release cut (this commit)                                   |

Sprint 13 plan target (per the new rolling-median model): **35 pts ± 15 pts.** Themes from SPRINT_PLAN.md § Sprint 13 ("Feedback triage from public alpha; bug fixes; perf regressions caught by external workloads"):

Likely pushes:
- ADR-0017 (public-alpha bug-discovery model) + the synthetic-load harness if we adopt option 2 (~8 pts).
- Auto-generated `/agents/tools` page from `souxmar agent list --json` (~3 pts).
- Sprint 11/12 carry-overs: VTU consumer conformance (~3 pts), VR baselines committed (~1 pt), DNS CNAME for docs.souxmar.dev (~1 pt operational), Discord server + redirect (~2 pts operational).
- First-real-FFI call: pipeline_introspection from libsouxmar-pipeline via cbindgen (~8 pts; flips `pipeline_introspection` flag true in default BridgeFeatureSet).
- External bug-report triage backlog (variable; depends on volume).
- CFD-stub's per-patch BC routing carry-over from Sprint 10 (~3 pts).

The Sprint 13 push count depends heavily on external-feedback volume. Plan against a ±50% volume swing.

## Outcome

souxmar is at **v0.9.0** as of this commit. **First public alpha** — the first non-`-beta`-suffixed tag in the project's history. The auto-updater XL story closed in Sprint 10; the plugin marketplace v0 + the AI provider abstraction closed across Sprints 10-11; the desktop onboarding wizard + workbench shell + the BridgeFeatureSet contract closed across Sprints 10-12; the public docs site is live (default GitHub-Pages URL until DNS catches up); the bug-triage workflow is wired with the SLA contract documented in COMMUNITY.md.

The ABI stays at v1.3 frozen final. The tool contract stays at v1 frozen final with 18 tools. The same C++ engine powers the CLI, the Python library, and the still-scaffolded desktop app — three peer surfaces, no privileged path, as designed.

What's **not** in v0.9.0: real FFI from the workbench (Sprint 13 onwards lands `pipeline_introspection` first, then `provider_call`, then `viewport_renderer`); managed-AI proxy (Sprint 14); cloud sync (Sprint 15); paid plugin marketplace (Sprint 16). The honest framing — Sprint 12's exit criterion was "v0.9.0 tagged, signed installers on download page, docs site live, triage rotation handling reports within 48h" — is met *modulo* the operational follow-ups (DNS, Discord, on-call rotation population) named in "what to fix" above.

Sprint 13 starts measuring whether the external-first dogfood model is right. The first month of public-alpha feedback writes the rest of the v1.0 plan.
