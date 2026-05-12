# Infrastructure status — corpus + baseline bootstraps pending

Sprint 15 push 1 — running tally of the infrastructure that's
wired but waiting on its first real data. Each item below has a
named "what unblocks it" + a sprint-level action item.

The doc exists because these gates are *real* even while empty.
A new contributor looking at `synth-load/golden/corpus.toml` and
seeing empty sha256s should land here, not file an issue.

## Status overview

| Surface                                | State (2026-05-13)                                                 | Unblock                                                                                       | Owner sprint |
| -------------------------------------- | ------------------------------------------------------------------ | --------------------------------------------------------------------------------------------- | ------------ |
| synth-load golden corpus               | Wired; placeholder hashes; `--bootstrap` mechanism ready           | One green eval-nightly run after v0.9.2 → maintainer runs bootstrap locally → reviews → commits | Sprint 15+   |
| Per-platform VR baselines (linux)      | Wired (matrix workflow); zero PNGs in tree                          | One green visual-regression run on the linux runner → maintainer downloads artefact → commits  | Sprint 15+   |
| Per-platform VR baselines (darwin)     | Same as linux                                                       | Same as linux, macos-14 runner                                                                | Sprint 15+   |
| Per-platform VR baselines (win32)      | Same as linux                                                       | Same as linux, windows-2022 runner                                                            | Sprint 15+   |
| docs-site `/agents/tools` content      | Wired (placeholder); generator + `--check-only` gate live          | Same PR as the synth-load bootstrap; the maintainer's first run of `gen-agent-tools.py` against the v0.9.2 binary replaces the placeholder | Sprint 15+   |
| DNS CNAME for docs.souxmar.dev         | Not wired                                                          | Operational; out-of-band registrar work                                                       | Operational  |
| Discord server + invite redirect       | Not wired                                                          | Operational; community-launch coordination                                                    | Operational  |
| On-call rotation table (COMMUNITY.md)  | Placeholder ("TBA")                                                 | Team grows past N=1; rotation can be filled in then                                            | When-team    |

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

1. Wait for a green eval-nightly run *and* a green visual-
   regression matrix run on master post-v0.9.2.
2. Download the relevant artefacts:
   - `synth-load-report` (eval-nightly run) → contains the
     JSON report with computed fingerprints in `bootstrap`
     status.
   - `visual-regression-{ubuntu-22.04,macos-14,windows-2022}`
     (visual-regression workflow) → contains three sets of
     reference PNGs.
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

   # Docs-site agent tools:
   scripts/docs-site/gen-agent-tools.py \
     --engine build/dev/tools/souxmar/souxmar \
     --out    docs-site/agents/tools.md
   ```

4. Review every diff by eye. Confirm:
   - Synth-load hashes correspond to *intentional* behaviour;
     no unexpected output drift.
   - VR baselines render the dim-theme palette correctly on
     each platform.
   - The generated agent tool docs match the 18-tool catalogue
     ADR-0011 names.
5. Commit all in one PR titled
   `infra: bootstrap corpora + baselines after v0.9.2 (Sprint X push N)`.
6. The same PR flips `continue-on-error: true` to `false` in
   the gating workflows: `eval-nightly.yml` (synth-load step)
   and `visual-regression.yml` (Playwright step). Gates go
   live.

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
