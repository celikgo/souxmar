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

## What happened to the old workflows

This repository had twelve workflows until `c7f6214` (2026-05-24) removed all of them — 1709 lines,
no reason recorded in the message. Documentation written before that still names them, which is why
several files reference `.github/workflows/*.yml` paths that do not exist.

The five workflows here replace most of that surface, under different names:

| Removed | Covered now by |
| --- | --- |
| `ci.yml` | `ci.yml` — rewritten, four-platform matrix |
| `dco.yml` | `ci.yml` → `governance` |
| `services-build.yml` | `ci.yml` → `rust` (clippy + tests across every crate) |
| `desktop-ffi.yml` | `ci.yml` → `rust` (builds the frontend, then the bridge) |
| `eval-nightly.yml` | `nightly.yml` → `agent-eval-llm` + `synth-load` |
| `perf-nightly.yml` | `ci.yml` → `benchmarks` (advisory) |
| `nightly.yml` | `nightly.yml` |
| `release.yml` | `release.yml` — rewritten against `scripts/release/` |
| `visual-regression.yml` | `visual-regression.yml` |

Three are **not** restored, and nothing here covers them:

- **`docs-site.yml`** — published `docs-site/` to GitHub Pages on every master push. The README
  still advertises the site.
- **`plugin-index.yml`** — PR-gated validation of new marketplace listings, running
  `souxmar plugin validate-index` and `souxmar-conformance` against each submitted binary. The
  `publishing-plugin-marketplace` skill still instructs authors to expect it.
- **`triage.yml`** — auto-acknowledged new issues with the matching SLA and auto-labelled by
  surface. `CONTRIBUTING.md` still promises it.

Each of those is a real capability the project documents and does not currently have. Restoring
them is a decision, not a doc fix, so they are listed here rather than quietly reinvented.

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

> **Not enabled today.** `gh api repos/celikgo/souxmar/branches/master/protection` returns 404 and
> `gh api repos/celikgo/souxmar/rulesets` returns `[]`. Every gate described in this document is
> therefore advisory by omission, and a direct push to `master` bypasses all of them. The two
> aggregator jobs below exist and report; nothing requires them yet.

Require exactly two checks: **`CI`** (the `ci-ok` job at the end of `ci.yml`) and **`Security`**
(the `security-ok` job at the end of `security.yml`).

Two, not one, because `needs:` cannot cross workflows: `ci-ok` can only aggregate jobs declared in
`ci.yml`, so CodeQL, the SCA scans, dependency review and the licence inventory sit structurally
outside it. Requiring only `CI` would mean none of them can ever block a merge no matter what they
report — which contradicts the "Blocking: Yes" rows earlier in this file.

Each aggregator covers every job in its own workflow, so adding, renaming or splitting a job never
means editing branch-protection settings. Both treat `skipped` as a pass (the `changes` filter
legitimately skips whole areas on a docs-only PR; `dependency-review` only runs on pull requests)
and everything else as a failure, which is the distinction a raw "required checks" list gets wrong:
a skipped job reports success to GitHub, so listing jobs individually lets a filtered-out job
silently satisfy a requirement it never ran.

Both workflows carry a `merge_group:` trigger, without which a required check never reports inside
a merge queue and the queue blocks rather than gates.

Enabling it, once the aggregators have been observed reporting on a real PR:

```sh
gh api -X PUT repos/celikgo/souxmar/branches/master/protection --input - <<'JSON'
{
  "required_status_checks": { "strict": false,
    "checks": [ { "context": "CI" }, { "context": "Security" } ] },
  "enforce_admins": true,
  "required_pull_request_reviews": null,
  "restrictions": null
}
JSON
```

`required_pull_request_reviews` is null deliberately — the repository has one maintainer, and a
review requirement nobody can satisfy is a gate that gets disabled the first time it is
inconvenient. `enforce_admins` is the load-bearing field for a solo repository. `strict` stays
false so a merge does not require every PR to be rebased onto the tip. `required_linear_history`
is deliberately absent: `allow_merge_commit` is on and the last six merges to `master` are all
two-parent merge commits, so requiring linear history would reject the project's own workflow.

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

`clang-format` is checked **only on the lines a PR changes**, via `clang-format-diff`. 54 of 251
C++ files predate `.clang-format`; a whole-file check would mean that touching one line of a legacy
file forces reformatting all of it, which turns every small fix into an unreviewable diff.

The `format-debt` job reports the remaining count on every run, so the ratchet has a visible finish
line rather than quietly never completing.

**There is no `rustfmt` gate.** Seven of the eight Rust crates fail `cargo fmt --check`, and the
reason is that the codebase aligns struct fields deliberately:

```rust
pub error:      Option<ChatErrorSummary>,
pub tokens_in:  i64,
```

rustfmt has no option that expresses that, so enforcing it — even as a ratchet — would mean
reformatting the Rust tree against a style its authors chose on purpose. Adopting rustfmt is a
project decision, not something a CI change should impose. `clippy -D warnings` and `cargo test`
are blocking, and those are the gates that catch defects rather than taste.

**`ruff format` is off for the same reason.** The Python sources align consecutive assignments
identically (`loader   = ...` / `report   = ...`), and ruff format collapses it. `ruff check` is
blocking — unused imports, undefined names and import ordering are defects, not taste.

`clang-format` is the exception, and only because `.clang-format` is already committed: the project
has chosen a C++ formatter, so enforcing it on changed lines is applying the project's decision
rather than making one.

## Rust advisories that are reported, not enforced

`cargo audit` fails the build on **vulnerabilities** and **yanked** crates. It reports
`unmaintained` and `unsound` without failing, because every advisory in that category today comes
from one place — the GTK3 stack Tauri depends on for the Linux desktop shell, which gtk-rs
deprecated in favour of GTK4:

    atk  atk-sys  gdk  gdk-sys  gdkwayland-sys  gdkx11  gdkx11-sys
    glib  gtk  gtk-sys  gtk3-macros  proc-macro-error  unic-*

17 advisories, zero of them vulnerabilities, and none reachable from code this project controls.
`--deny warnings` would make the job permanently red over an upstream ecosystem migration, and a
job that is always red is one nobody reads — which costs more than the advisories do.

It clears when Tauri's Linux backend moves to GTK4. Until then a real vulnerability still fails the
build, which is the property worth protecting.

## How Windows plugins resolve the host ABI

A souxmar plugin leaves the host ABI undefined and lets the loader bind it to whichever process
`dlopen`s it. ELF permits that; Mach-O opts in with `-undefined dynamic_lookup`; Windows permits it
not at all — every symbol a DLL references must be bound at link time to a *named module*.

There is no module to name. The provider is whichever host executable loads the plugin, and souxmar
has several: the CLI, both test runners, the conformance tool, the desktop bridge. A DLL that
imports from `souxmar.exe` cannot be loaded by `souxmar_unit_tests.exe`.

So on Windows `souxmar_add_plugin()` links `souxmar::plugin_shim`, a static library in which every
ABI function is a thunk that resolves itself on first call through
`GetProcAddress(GetModuleHandle(NULL), …)`. `GetModuleHandle(NULL)` is the running executable —
exactly the rule the other two platforms apply for free — and the hosts already export those
symbols via `CMAKE_EXECUTABLE_ENABLE_EXPORTS`, whose comment in `cmake/SouxmarOptions.cmake` calls
this out as the missing Windows half.

**The ABI does not change.** Same symbol names, signatures and semantics; `include/souxmar-c/**` is
untouched and so is every plugin's source. This is a link-time detail of one platform, not an ABI
revision, so it needs no ratchet marker and the frozen-header gate stays quiet.

`src/plugin-shim/shim_win32.c` is generated by `scripts/gen-windows-plugin-shim.py` and committed,
so the thunks are reviewable. The `governance` job re-runs the generator with `--check-only`: if a
header gains a function and nobody regenerates, Windows plugins would fail to link on that one
symbol, on the platform least likely to be built locally.

The shim is never linked into a host — the host defines those symbols for real, and the thunks
would shadow them with lookups that resolve straight back.

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

# clang-format, the way CI checks it — changed *lines*, not whole files.
# Diff against the merge base, not `master...HEAD`: the three-dot form
# compares commits only, so uncommitted work reports clean and then fails
# in CI.
git diff -U0 "$(git merge-base master HEAD)" -- '*.cpp' '*.h' \
  | clang-format-diff-17 -p1

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
