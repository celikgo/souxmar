# Infrastructure status — corpus + baseline bootstraps pending

Sprint 15 push 1 — running tally of the infrastructure that's
wired but waiting on its first real data. Each item below has a
named "what unblocks it" + a sprint-level action item.

The doc exists because these gates are *real* even while empty.
A new contributor looking at `synth-load/golden/corpus.toml` and
seeing empty sha256s should land here, not file an issue.

## Status overview

| Surface                                | State (2026-08-25)                                                 | Unblock                                                                                       | Stale-for-N-sprints |
| -------------------------------------- | ------------------------------------------------------------------ | --------------------------------------------------------------------------------------------- | ------------------- |
| synth-load golden corpus               | Wired; placeholder hashes; harness now produces real, run-to-run-stable fingerprints for all four targets (it produced none until 2026-08-24 — see below) | The harness no longer blocks it: download `synth-load-report` from any nightly, review, commit | 3 sprints (S13-S15)  |
| Per-platform VR baselines (linux)      | Wired (matrix workflow); zero PNGs in tree                          | One linux-runner run where every spec reaches its screenshot assertion (not a *green* run — see "The bootstrap PR") → maintainer downloads artefact → commits | 2 sprints (S14-S15)  |
| Per-platform VR baselines (darwin)     | Same as linux                                                       | Same as linux, macos-14 runner                                                                | 2 sprints (S14-S15)  |
| Per-platform VR baselines (win32)      | Same as linux                                                       | Same as linux, windows-2022 runner                                                            | 2 sprints (S14-S15)  |
| docs-site `/agents/tools` content      | Wired (placeholder); generator + `--check-only` gate live          | Same PR as the synth-load bootstrap; the maintainer's first run of `gen-agent-tools.py` against the v0.9.3 binary replaces the placeholder | 3 sprints (S13-S15) |
| Custom domain for the docs site        | Not needed — the site is live at the default GitHub Pages URL, https://celikgo.github.io/souxmar/ | Would only matter if a domain is ever registered | n/a |
| Discord server                         | Dropped — never created, and the references to it have been removed | n/a | n/a |
| On-call rotation table (COMMUNITY.md)  | Placeholder ("TBA")                                                 | Team grows past N=1; rotation can be filled in then                                            | 4 sprints (S12-S15) |

## The nightly (resolved 2026-08-25)

This section exists because for three months this document did not mention
the nightly at all, while the repository's docs cited it as evidence of
rigour. It had **never had a green run** — every scheduled run from at least
2026-05-11 concluded `failure`.

It is green now. Verified, not assumed: run
[32814521723](https://github.com/celikgo/souxmar/actions/runs/32814521723),
dispatched deliberately rather than waited for, concluded `success` with
`asan`, `tsan`, both fuzz targets and the LLM-driven agent evals all passing.

Three independent causes, fixed in that order:

1. **The toolchain.** `gcc-13` is not in jammy's archive, so every Linux job
   in every workflow got its compiler from `add-apt-repository
   ppa:ubuntu-toolchain-r/test` — a Launchpad REST call on the critical path,
   with no cache and no retry. It answered HTTP 500 on 2026-08-24 and took the
   whole run out. Fixed by moving to `ubuntu-24.04`, which preinstalls GCC
   13.3.0 and Clang 17.0.6.
2. **The sanitizer presets named no compiler**, so `asan` and `tsan` built
   with `/usr/bin/c++` (GCC 11) while every other job used gcc-13, and died on
   `-Werror=useless-cast` diagnostics GCC 13 does not emit. Fixed by
   `ci-linux-asan` / `ci-linux-tsan`, which pin the compiler.
3. **Then the sanitizers found real bugs**, which is the entire point and had
   never happened before: a use-after-free in `src/plugin-host/subprocess.cpp`
   (the child's environment array pointed at a reallocated vector's freed
   buffer), and two `HeapAccountant` tests measuring a glibc arena counter that
   a sanitizer's replacement allocator no longer moves.

**`synth-load` is the one job still reporting `failure`**, and that is the
ADR-0017 bootstrap state rather than a defect. It runs all four targets, each
reports `no-golden` because `golden/corpus.toml` still carries placeholder
hashes, and the harness exits 3. The job is `continue-on-error: true` and is
exempted **by name** in `nightly.yml`'s summary aggregator, with the exemption's
end condition stated there — so the run's conclusion is honest either way.

What changed on 2026-08-24 is that the harness now works at all. It previously
produced *no* fingerprints: a relative `--engine` path stopped resolving after
the example loop `pushd`'d into its scratch directory (rc=127), `souxmar-eval`
was handed a file where it requires a directory, `--plugin-path` was given leaf
plugin directories where discovery scans a search path's subdirectories, and
`corpus_lookup`'s awk returned every hash twice. The fingerprints are now real
and byte-identical across repeat runs, so the bootstrap this document has
described since Sprint 13 is finally possible.

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
