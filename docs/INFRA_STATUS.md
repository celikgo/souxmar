# Infrastructure status — corpus + baseline bootstraps pending

Sprint 15 push 1 — running tally of the infrastructure that's
wired but waiting on its first real data. Each item below has a
named "what unblocks it" + a sprint-level action item.

The doc exists because these gates are *real* even while empty.
A new contributor looking at `synth-load/golden/corpus.toml` and
seeing empty sha256s should land here, not file an issue.

## Status overview

| Surface                                | State (2026-05-14)                                                 | Unblock                                                                                       | Stale-for-N-sprints |
| -------------------------------------- | ------------------------------------------------------------------ | --------------------------------------------------------------------------------------------- | ------------------- |
| synth-load golden corpus               | Wired; placeholder hashes; `--bootstrap` mechanism ready           | One green eval-nightly post-v0.9.3 → maintainer runs bootstrap locally → reviews → commits     | 3 sprints (S13-S15)  |
| Per-platform VR baselines (linux)      | Wired (matrix workflow); zero PNGs in tree                          | One linux-runner run where every spec reaches its screenshot assertion (not a *green* run — see "The bootstrap PR") → maintainer downloads artefact → commits | 2 sprints (S14-S15)  |
| Per-platform VR baselines (darwin)     | Same as linux                                                       | Same as linux, macos-14 runner                                                                | 2 sprints (S14-S15)  |
| Per-platform VR baselines (win32)      | Same as linux                                                       | Same as linux, windows-2022 runner                                                            | 2 sprints (S14-S15)  |
| docs-site `/agents/tools` content      | Wired (placeholder); generator + `--check-only` gate live          | Same PR as the synth-load bootstrap; the maintainer's first run of `gen-agent-tools.py` against the v0.9.3 binary replaces the placeholder | 3 sprints (S13-S15) |
| Custom domain for the docs site        | Not needed — the site is live at the default GitHub Pages URL, https://celikgo.github.io/souxmar/ | Would only matter if a domain is ever registered | n/a |
| Discord server                         | Dropped — never created, and the references to it have been removed | n/a | n/a |
| On-call rotation table (COMMUNITY.md)  | Placeholder ("TBA")                                                 | Team grows past N=1; rotation can be filled in then                                            | 4 sprints (S12-S15) |

When the stale-for counter passes 5 sprints, the line escalates
in the next sprint's retro under "what to fix" — at that point
the gate-without-data has aged enough that the gate-mechanism
might be a worse status quo than reverting it. None of the
above have hit that threshold yet; the visibility is the goal.

## Why these gates ship empty

Each gate above follows ADR-0017's "non-blocking-on-first-run
harness pattern" — ship the gate's *mechanism* before its first
green data. The pattern protects against the chicken-and-egg
problem of "we can't have a corpus before there's a binary to
seed it; we can't gate on a corpus we don't have." The cost is
one sprint of `continue-on-error: true` between the gate
landing and the first real data.

The pattern is described once in the Sprint 13 retro
("non-blocking-on-first-run harness pattern" as a "what to
keep"); future sprints reference it inline.

## The bootstrap PR

When a maintainer is ready to land the first real data:

1. Wait for a green eval-nightly run on master post-v0.9.2, and
   for a visual-regression matrix run in which **every spec
   reaches its `toHaveScreenshot` assertion**.

   That second condition used to read "a green visual-regression
   matrix run", which no run could ever satisfy. With zero PNGs
   in the tree Playwright fails every spec with `A snapshot
   doesn't exist at …, writing actual` *by definition* — so
   waiting for green was waiting for the state this very PR
   exists to create, and the baselines sat unharvested for two
   sprints behind a gate that could not open.

   What is achievable, and what actually gates the harvest, is a
   run whose *only* failures are that message. A spec that dies
   earlier — `element(s) not found` from a selector the app
   outgrew, or `mock: unknown command` from the Tauri shim in
   `tests/visual/mocks/tauri.ts` — rendered nothing, so there is
   nothing to harvest for it. Fix the spec first; a partial
   harvest bakes in a hole that only shows up as a missing
   baseline months later.
2. Download the relevant artefacts:
   - `synth-load-report` (eval-nightly run) → contains the
     JSON report with computed fingerprints in `bootstrap`
     status.
   - `visual-regression-{ubuntu-24.04,macos-14,windows-2022}`
     (visual-regression workflow) → contains three sets of
     `*-actual.png` renders, one set per platform. They are
     "actual", not "reference", precisely because the run that
     produced them had no reference to compare against — that is
     what makes them harvestable.
3. Locally:

   ```sh
   # Synth-load corpus:
   scripts/synth-load/run.sh --bootstrap
   git diff scripts/synth-load/golden/corpus.toml

   # VR baselines (one per platform):
   #   For each downloaded artefact, copy the PNGs into the
   #   matching tests/visual/specs/*.spec.ts-snapshots-<platform>/
   #   directory. The artefact's layout mirrors the directory
   #   structure under tests/visual/test-results/.

   # Docs-site agent tools — DONE, the page is generated from the
   # binary and the CI gate now blocks. Re-run after any tool change:
   scripts/docs-site/gen-agent-tools.py \
     --engine build/dev/src/cli/souxmar \
     --out    docs-site/agents/tools.md
   ```

4. Review every diff by eye. Confirm:
   - Synth-load hashes correspond to *intentional* behaviour;
     no unexpected output drift.
   - VR baselines render the dim-theme palette correctly on
     each platform.
   - The generated agent tool docs match the catalogue the binary
     reports — 24 tools, ADR-0011's frozen 18 plus the six additive
     ratchets through ADR-0045.
5. Commit all in one PR titled
   `infra: bootstrap corpora + baselines after v0.9.2 (Sprint X push N)`.
6. The same PR flips `continue-on-error: true` to `false` in
   the gating workflows: `nightly.yml` (synth-load job; this was
   `eval-nightly.yml` before `c7f6214` removed it)
   and `visual-regression.yml` (Playwright step). Gates go
   live.

   The bootstrap PR's own visual-regression run is the first one
   with baselines in the tree, and therefore the first that can
   come back green — the harvest run before it could not. Read
   that run as the verification of step 4: if it is still red
   with real pixel diffs, the harvested PNGs are wrong for their
   runner and the flip comes back out rather than the diffs being
   re-blessed.

## What this document is NOT

- Not a substitute for the per-surface README. Each entry
  above links to its own document for the full procedure;
  this is the cross-cutting status board.
- Not a roadmap. The "Owner sprint" column is the *next*
  sprint that can plausibly unblock the gate, not a
  commitment.
- Not a TODO list. Items here are *expected* states for the
  current point in the v0.9.x window. If you see "wired;
  placeholder" you should not file an issue.

— Sprint 15 push 1.
