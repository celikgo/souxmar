# Sprint 24 retro — v1.0.0 ship + the v0.x → v1.0 journey

**Closed:** 2026-05-22. **Pushes:** 2. **Theme:** "v1.0.0 ships.
First closeable sprint of the v0.x → v1.0 arc."

## What landed

| Push | Deliverable                                       | Lines |
| ---- | ------------------------------------------------- | ----- |
| 1    | ADR-0036 v1.0 final freeze + v1.0.0 tag           | ~120 |
| 2    | Sprint 24 retro + post-v1.0 plan brief (this commit) | this |

## The v0.x → v1.0 retrospective

This is the journey-spanning retro the project earns at v1.0.
24 sprints. ~150 pushes. ~36 ADRs. The plugin C ABI froze at v1.3
in Sprint 10; the tool contract froze at v1 with 18 tools in
Sprint 7; the on-disk pipeline format stayed at v1 throughout;
the update manifest format stayed at schema=1 throughout. Four
contracts; zero v2 breaks pre-v1.0.

Two distinct halves of the journey:

**Sprints 0-12 — the contributor-facing alpha era.**
Foundation, plugin host, pipeline runner, agent dispatcher,
adapter plugins (mesh-quality, stl-reader, swap-mesher,
elasticity-stub, cfd-stub, obj-reader), Python bindings,
parallel runner, ABI freeze candidates and finals, OpenFOAM /
Blender import, performance + scale work, distribution +
plugin marketplace v0, internal alpha, public alpha (v0.9.0).
At v0.9.0 the codebase is a *tool* a contributor checks out
and builds from source.

**Sprints 13-24 — the consumer-facing beta era.** External
bug-triage workflow + COMMUNITY.md, souxmar-bridge FFI surface
(3 of 6 BridgeFeatureSet flags structural by v1.0), six Pro-
tier service scaffolds (managed-AI proxy + cloud sync +
marketplace + billing + account portal + compute offload), the
public-alpha bug-discovery model with synthetic-load harness
(ADR-0017), the MVC-via-subprocess pattern (ADR-0022), the
typed CLI shape contract (ADR-0025), four geometry / multi-
window / SSO / security architecture ADRs (0029-0033), the
v0.99 public beta + the v1.0 contract bake + Stripe live flip.
At v1.0.0 the codebase is a *product* a consumer downloads,
runs, and pays for.

## Recurring patterns (project-wide "what to keep")

Patterns that surfaced multiple times across the sprints +
were named in retros:

1. **Typed enums over string-parsing in callers.** Every new
   error / outcome type since Sprint 6 follows this. The
   pattern lets a refactor change the error set without
   silently changing caller behaviour.
2. **Schema=1 discriminator on every new on-disk format.** ADR-
   0013 (manifest), ADR-0020 (project.ai.toml), ADR-0025 (CLI
   --json), the marketplace OpenAPI shapes. Lets v2 break
   loudly.
3. **Scaffold the contract; ship the code with its consumer.**
   First named in Sprint 18 retro; reflexive by Sprint 23.
4. **Honest-503 with per-endpoint sprint pointers.** Six
   service scaffolds; first proxy stub (Sprint 14) to last
   compute-offload stub (Sprint 17) all used the same shape.
5. **Read through FFI; APPLY through subprocess shell-out**
   (ADR-0022 / MVC-via-subprocess). State-machine surfaces
   stay single-source-of-truth.
6. **`.gitignore`-by-default for secrets-adjacent config.**
   Dev signing key + `project.ai.toml`.
7. **Bridge ABI byte cross-checked on every call.** ADR-0018.
   Partial-upgrade scenarios fail loudly, not silently.
8. **Per-retro: keep / fix / one-ADR-worthy-decision / risk
   register diff / capacity forecast.** Sprint 8 introduced
   the shape; 17 subsequent retros preserve it; the next sprint's
   priorities visibly flow from the prior retro's "what to
   fix."

## What to fix forever

- **Synth-load corpus + per-platform VR baselines never
  committed.** Stale for 12 sprints by the time v1.0 ships.
  The mechanism works; the data never landed because every
  bootstrap PR required a maintainer hand on the CI artefact.
  The pattern is correct (gate-without-data > no-gate); the
  consistent deferral is the lesson. **v1.0.1 carries the
  resolved bootstrap PR.**

## Risk register at v1.0

- **R-013, R-014, R-016, R-017, R-041, R-042, R-043, R-044
  closed.**
- **R-015 (external feedback signal):** v0.99-beta1 + the
  post-launch comms generated 14 external bug reports by
  Sprint 24 retro — below ADR-0017's "≥ 20 first month" target
  but within the launch-comms second-amendment's expected
  range. Volume monitoring continues post-v1.0.
- **R-010 (velocity / hiring):** team grew from 1 contributor
  at Sprint 0 to 3 by Sprint 24 (founder + 2 contributors).
  Closes; v1.x maintenance cadence assumes the 3-person team.
- **All remaining risks (R-018-R-040)** carry forward to
  v1.x. Tracked per-risk in `docs/SPRINT_PLAN.md`'s
  post-v1.0 rewrite.

## Post-v1.0 plan brief

Next 4 sprints (25-28):

- **Sprint 25-26 / v1.0.1:** Enterprise E2E cloud sync;
  closing pen-test deferred Medium; first paid plugin
  publisher onboarded.
- **Sprint 27 / v1.0.2:** Paid plugins live + publisher
  onboarding flow.
- **Sprint 28 / v1.1.0:** Viewport rendering — Three.js +
  VTK.js wired through the souxmar-bridge's
  `viewport_renderer` flag. 4th of 6 BridgeFeatureSet flags
  structural.

`docs/SPRINT_PLAN.md` rewritten post-v1.0 to cover the v1.x
arc; the v0.x plan is archived as `docs/SPRINT_PLAN.v0.x.md`.

## Outcome

souxmar is at **v1.0.0** as of this commit.

The plugin C ABI is **v1.3 FINAL** per ADR-0008 + ADR-0012 +
ADR-0035 + ADR-0036.
The agent tool contract is **v1 FINAL** with 18 tools per
ADR-0011 + ADR-0035 + ADR-0036.
The on-disk pipeline format is **v1 FINAL** per ADR-0035 +
ADR-0036.
The update manifest is **v1 FINAL** per ADR-0013 + ADR-0035 +
ADR-0036.
The bridge ABI is at **v3**; additive-Tier-0 evolution allowed
through v1.x.

The same C++ engine powers the CLI, the Python library, three
desktop FFI surfaces, six Pro-tier services. No privileged
path. Apache-2.0. BYOK as the default AI experience; Pro tier
as the opt-in managed alternative.

**ABI v1 lives forever** at v1.3. The next ABI break is v2.0
— post-v1.0 future-sprint scope; ADR-0036 doesn't open it.
ADR count: 36.

End of the v0.x window. The v1.x maintenance era begins.
