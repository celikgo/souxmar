# CI

Where each promise in [`ENGINEERING_PRACTICES.md`](ENGINEERING_PRACTICES.md) is actually enforced.
That document is the specification; this one is the implementation map, so a gate that is described
but not wired is visible rather than assumed.

## Workflows

| Workflow | Trigger | Purpose |
| --- | --- | --- |
| [`ci.yml`](../.github/workflows/ci.yml) | PR, push to default, merge queue | The pre-merge gate. |
| [`security.yml`](../.github/workflows/security.yml) | PR, push, weekly | CodeQL, SCA, licence inventory. |
| [`visual-regression.yml`](../.github/workflows/visual-regression.yml) | PR touching the desktop | Per-platform screenshot diffs. |
| [`nightly.yml`](../.github/workflows/nightly.yml) | 03:00 UTC daily | Sanitizers, fuzzing, synth-load, LLM evals. |
| [`release.yml`](../.github/workflows/release.yml) | `v*` tag | Build, sign, publish a draft release. |

## Definition of Done, mapped

| # | Promise | Enforced by | Blocking |
| --- | --- | --- | --- |
| 2 | Tests written or updated | `engine` (ctest, all four platforms) | Yes |
| 2 | Coverage ≥ 85 % line / 70 % branch on changed files | **Not wired** — see Gaps | No |
| 3 | Docs updated with user-facing changes | `docs-generated` (agent tool reference only) | No — see below |
| 4 | Performance gates | `benchmarks` | No — see below |
| 5 | No new dependency without an ADR | `dependency-review` | Yes |
| 5 | No secret-shaped strings | `governance` → `check-secrets.sh` | Yes |
| 5 | SCA clean | `cargo-audit`, `npm-audit`, `dependency-review` | Yes |
| 5 | Licence inventory complete | `license-inventory` | Yes |
| 6 | Determinism gate | `determinism` | Yes |
| 7 | Conformance suite | `conformance` | Yes |
| 8 | No nightly regression | `nightly.yml` | Reported, not blocking |
| 10 | DCO sign-off on every commit | `governance` | Yes |
| — | Frozen v1 ABI (ADR-0008) | `governance` → `check-frozen-headers.sh` | Yes |
| — | Frozen tool contract (ADR-0011) | `governance` → `check-tool-contract.sh` | Yes |
| — | Agent eval pass rate | `agent-evals` | Yes |

## Repository prerequisites

Two settings have to be on before the pipeline is fully green. Neither is in the tree, so neither
can be fixed by a PR:

- **Dependency graph** (Settings → Code security). Without it `dependency-review` fails with
  "Dependency review is not supported on this repository", and the SCA half of Definition-of-Done
  item 5 is unenforced.
- **`SOUXMAR_EVAL_ANTHROPIC_KEY`** (Settings → Secrets → Actions), for the nightly LLM-driven eval.
  Absent, that job skips itself with a notice rather than failing — a missing key is an
  infrastructure state, not a regression.

## Branch protection

Require exactly one check: **`CI`** — the `ci-ok` job at the end of `ci.yml`.

It aggregates every required job, so adding, renaming or splitting a job never means editing
branch-protection settings. It treats `skipped` as a pass (the `changes` filter legitimately skips
whole areas on a docs-only PR) and everything else as a failure, which is the distinction a raw
"required checks" list gets wrong: a skipped job reports success to GitHub, so listing jobs
individually lets a filtered-out job silently satisfy a requirement it never ran.

## Determinism

Three platforms run `scripts/ci/determinism-fingerprint.sh`, which executes every example pipeline
under a fixed `TZ` and `LC_ALL`, pipes the output through the synth-load normaliser, and hashes it.
The manifests are then diffed against each other.

There is no golden file. **The platforms are each other's reference**, so nothing can drift by
having its expected value quietly updated. A pipeline that fails identically everywhere is still
deterministic, so the exit code is part of the fingerprint rather than an abort — what fails the
gate is one platform disagreeing with another.

## Why the performance gate is advisory

`ENGINEERING_PRACTICES.md` says regressions > 5 % block the merge, and the budgets there are
explicitly reference-machine numbers (M2 Pro / Ryzen 7 7700X). GitHub-hosted runners are shared,
throttled and heterogeneous; run-to-run variance on them routinely exceeds 5 % with no code change
at all. Making that blocking would produce a gate whose most common outcome is a false positive,
and the practical response to a flaky gate is to re-run until it passes — which is worse than not
having it, because it also teaches people to dismiss the real ones.

So the job runs, compares against `benchmarks/baselines/`, and uploads its numbers on every PR
without blocking. Closing this properly needs a self-hosted runner pinned to the reference
hardware; until then the honest state is "measured, not enforced", and this paragraph exists so
nobody reads the passing check as proof of the documented budget.

## Gates that ship without their data

Three jobs carry `continue-on-error: true` under ADR-0017's non-blocking-on-first-run pattern —
the mechanism lands before the first green data exists, because the data cannot be produced
without the mechanism:

- `nightly.yml` → `synth-load`: the committed corpus still holds placeholder hashes.
- `visual-regression.yml` → `visual`: there are no reference PNGs in the tree.
- `nightly.yml` → `fuzz`: `tests/fuzz/` does not exist yet; the job no-ops until it does.
- `ci.yml` → `docs-generated`: `docs-site/agents/tools.md` is still the committed placeholder that
  points at its own regeneration command, so the check is red until the bootstrap runs. Regenerating
  it is a one-command change, deliberately kept out of the PR that added CI so a large generated
  diff does not ride along with workflow review.

[`INFRA_STATUS.md`](INFRA_STATUS.md) tracks each one and the bootstrap PR that removes the flag.
The artifact names in those jobs are load-bearing — that runbook tells a maintainer to download
`synth-load-report` and `visual-regression-<os>` by name.

## Format is a ratchet, not a sweep

`clang-format` and `rustfmt` are checked **only on the files a PR changes**. 54 of 251 C++ files and
seven of eight Rust crates predate any formatting enforcement. Reformatting them wholesale in the
change that introduces CI would bury the workflow review under a five-figure diff and conflict with
everything in flight, so instead new and touched code is held to the standard and the rest converts
as it is edited.

The `format-debt` job reports the remaining count on every run, so the ratchet has a visible finish
line rather than quietly never completing.

## Gaps

Named here rather than left for someone to discover:

- **Coverage gate is not wired.** Item 2 of Done specifies 85 % line / 70 % branch on changed
  files. Implementing it needs a coverage build (`--coverage` / `llvm-cov`), a diff-aware reporter,
  and a decision about whether generated code counts. It is real work, not a checkbox.
- **End-to-end CLI and desktop suites are not wired.** The test pyramid budgets 8 and 12 minutes
  for them. The desktop half needs Tauri WebDriver, which needs a packaged app in CI.
- **`docs-generated` only checks the agent tool reference.** The practice is broader: "code
  examples in docs are extracted and built in CI".
- **The fuzz targets do not exist.** The harness is wired and no-ops until `tests/fuzz/` lands.

## Running the gates locally

Everything blocking in `ci.yml` runs on a laptop, and should before you push:

```sh
scripts/check-secrets.sh
scripts/check-frozen-headers.sh
scripts/check-tool-contract.sh

cmake --preset dev && cmake --build --preset dev
ctest --preset dev --output-on-failure

build/dev/tools/conformance/souxmar-conformance build/dev/examples/plugins
build/dev/tools/eval/souxmar-eval evals/v1 \
  --plugin-path build/dev/examples/plugins --min-pass-rate 1.0

scripts/ci/determinism-fingerprint.sh \
  --engine build/dev/src/cli/souxmar \
  --plugin-path "$PWD/build/dev/examples/plugins"
```

Install the hooks so the cheap ones run automatically:

```sh
pip install pre-commit && pre-commit install
```
