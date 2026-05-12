# Visual-regression baselines — when to commit, when to refuse

Sprint 11 retro flagged that CI baselines were not yet committed.
Sprint 12 retro carried it over. Sprint 13 push 2 lands the
**policy** for committing baselines — the actual baseline PNGs
land with the first post-v0.9.0 CI run that succeeds end-to-end.

## The policy

Baselines live under
`tests/visual/specs/<spec-name>.spec.ts-snapshots/`. Each spec's
first reference render is committed on the **first green CI run
after a deliberate token / layout / copy change**.

### Who commits

The PR author who made the change reviews the CI artefact
(`visual-regression` upload), confirms the new render is
*intentional*, and commits the regenerated PNGs in the same PR.

### When NOT to commit

- **The PR title contains "wip", "draft", or has [no-baseline] tag.**
  The baseline survives across PRs; only ratify when the surface
  is final.
- **The diff says "added: 17 baselines" with no apparent source
  change.** The reviewer pushes back; baseline-flurries without a
  surface change usually mean the renderer / font / Chromium
  changed underneath us — that needs a separate ADR-style
  conversation, not a silent baseline bump.
- **Multiple unrelated panels' baselines change in one PR.** The
  PR is decomposed; one panel's worth of baselines per PR
  maximum, so a future bisect can identify which surface
  shipped which visual.

### When to commit promptly

- A token change in `src/desktop/src/ui/tokens.css` that
  intentionally shifts the dim-theme palette. The author
  re-bless's once, and every downstream PR's CI run is gated
  against the new baseline.
- A new spec file added in the same PR — the spec's first
  baseline lives with the spec it tests.
- A copy update reviewed by docs that re-renders the wizard's
  heading text.

## How to land the first batch (Sprint 13+)

The Sprint 12 retro's open action: commit the first round of
baselines from the first green CI run after v0.9.0. The procedure:

1. Trigger eval-nightly + the visual-regression workflow on a
   no-op PR (e.g. a typo fix in this very document).
2. Download the `visual-regression` artefact from the run.
3. Extract the PNGs to
   `tests/visual/specs/onboarding.spec.ts-snapshots/`.
4. Commit them with the message
   `tests(visual): initial CI baselines from Sprint 13 post-v0.9.0 run`.
5. Subsequent PRs are gated against these baselines.

This procedure is **not** automated because the first batch is the
load-bearing one — a maintainer eye on the artefact is the gate
against accidentally committing a broken render as the reference.

After the first batch, the second and onwards follow the "PR
author re-bless's intentional changes in the same PR" pattern
above.

## Cross-platform variance

Sprint 14 push 1 wired the per-platform baseline plumbing:

- Per-platform snapshot directories via Playwright's
  `snapshotPathTemplate`:
  `*.spec.ts-snapshots-linux/`, `-darwin/`, `-win32/`.
- New matrix workflow `.github/workflows/visual-regression.yml`
  runs the suite on ubuntu-22.04 + macos-14 + windows-2022 on
  every PR that touches `src/desktop/**` or `tests/visual/**`.
- Each platform produces its own `visual-regression-<os>`
  artefact for review.

**Initial-baselines flow (Sprint 14+ valid):**

1. Trigger the workflow (auto on a relevant PR, or manually via
   `workflow_dispatch`).
2. Three artefacts produce: one per OS. Download all three.
3. For each artefact, extract the `test-results/<spec>/<test>-actual.png`
   renders.
4. Confirm by eye that every render is the *intended* visual.
5. Move each render to its matching per-platform snapshot dir:
   - Linux artefact → `*-snapshots-linux/`
   - macOS artefact → `*-snapshots-darwin/`
   - Windows artefact → `*-snapshots-win32/`
6. Commit all three platforms' baselines in a single PR titled
   `tests(visual): initial CI baselines for Sprint 14`.
7. Sprint 15 push 1 flips the workflow's
   `continue-on-error: true` to `false` — the gate goes live
   once the corpus is seeded.

Until step 6 lands, do not run the visual suite locally and
commit a baseline. Trust the CI artefact only.

## Related

- `tests/visual/README.md` — the suite itself + harness rationale.
- Sprint 11 retro § "What to fix" — the original "visual baselines
  pending" call-out.
- Sprint 12 retro § "What to fix" — the carry-over that landed as
  this document in Sprint 13 push 2.

— Sprint 13 push 2.
