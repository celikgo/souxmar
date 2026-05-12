# scripts/synth-load — public-alpha regression harness

Sprint 13 push 1 — implements the **synthetic-load-augmented external**
arm of ADR-0017.

The harness runs every `examples/*/pipeline.yaml` end-to-end and every
`evals/v1/*.yaml` agent task, then diffs the result against a committed
golden fingerprint. Drift emits a non-zero exit code + a JSON report.

It is **not** a substitute for external bug reports; it is breadth
coverage that does not depend on which external workflows happen to
get exercised first.

## Layout

```
scripts/synth-load/
├── run.sh                 # main driver (bash, POSIX-ish)
├── README.md              # this file
└── golden/
    ├── corpus.toml        # registry of golden fingerprints
    └── normalize.py       # output normaliser (strips timestamps,
                           # pointer addresses, plugin-discovery
                           # order — anything non-deterministic
                           # that would false-positive)
```

## Invocation

```sh
# Full run (every example + every eval task):
scripts/synth-load/run.sh

# JSON report to a file:
scripts/synth-load/run.sh --json-out reports/synth-load-$(date +%Y-%m-%d).json

# Just the examples (skip evals):
scripts/synth-load/run.sh --skip-evals

# Refresh the golden corpus (use with care — only after intentional
# behaviour change is reviewed):
scripts/synth-load/run.sh --refresh-golden

# Bootstrap: record hashes only for corpus entries currently empty,
# leave non-empty entries alone. Used once per added target to seed
# the corpus. Sprint 14 push 1 added this flag.
scripts/synth-load/run.sh --bootstrap
```

## Initialising the corpus (Sprint 14+ first run)

The corpus that Sprint 13 push 1 shipped has placeholder hashes.
To seed it on the first green CI after v0.9.1:

1. Trigger the eval-nightly workflow (push to master / open PR /
   manual `workflow_dispatch`).
2. Wait for green; download the `synth-load-report` artefact.
3. Locally:

   ```sh
   scripts/synth-load/run.sh --bootstrap
   git diff scripts/synth-load/golden/corpus.toml
   ```

   Review the diff — every empty `sha256 = ""` becomes a real
   hash. Commit if and only if the rendered output for every
   target was the intended behaviour.

4. Sprint 14 push 2 flips the eval-nightly workflow's
   `continue-on-error: true` to `false` — gate goes live.

## Exit codes

Aligned with `tools/eval/main.cpp` conventions:

| Code | Meaning                                                |
| ---- | ------------------------------------------------------ |
| 0    | All targets matched golden                             |
| 1    | At least one example diverged                          |
| 2    | At least one eval task diverged                        |
| 3    | Both diverged                                          |
| 4    | Harness misconfigured (missing binary, missing golden) |
| 5    | `--refresh-golden` requested but corpus dirty in git   |

## How drift becomes a triage issue

When run on CI (`.github/workflows/eval-nightly.yml`, wired in Sprint
13 push 1), a non-zero exit code from `run.sh` triggers the same
`triage.yml` auto-acknowledge path used for external bug reports
(`label: type/synth-load-regression`, `priority: P1`, SLA: 48h). Per
ADR-0017 the rotation does not distinguish between external and
synthetic at triage time.

## Adding a new golden

1. Run the workflow against the new target with `--refresh-golden`
   off — confirm it fails (or skips with "no golden recorded").
2. Manually verify the output is the *intended* behaviour by hand.
3. Re-run with `--refresh-golden`; commit `golden/corpus.toml`.
4. The next CI run is the gate that confirms the fingerprint sticks.

## Why bash and not C++

The harness is intentionally a thin shell driver, not part of
`tools/`. Reasons:

- It composes existing CLI binaries (`souxmar run`, `souxmar-eval`)
  rather than re-implementing pipeline execution. Treating the
  binaries as black boxes is the regression contract — if the
  harness is C++ linked against the same engine, it can't catch
  build-system or binary-layer regressions.
- The harness's golden corpus is human-readable TOML; the
  comparison logic is `diff -u`. The full toolchain to read this
  is a POSIX shell — no compile-time dependency on the engine
  shipping correctly.
- The harness must run *before* a release tag exists, including
  on broken builds. Shell + python normaliser survives a broken
  C++ build; a C++ harness does not.

## Limits (named explicitly, per ADR-0017)

- **Deterministic outputs only.** Golden-hash comparison cannot
  catch ordering/timing/agent-reasoning drift. Those need a
  separate strategy — likely Sprint 16+ (see ADR-0017 §
  "Alternatives reconsidered later").
- **Single-platform-per-run.** macOS, Linux, Windows each have
  their own golden corpus. Cross-platform discrepancies must be
  golden-normalised (the normaliser strips line-ending + path-
  separator variance) or accepted (rare, called out in the
  per-platform corpus comment).
- **Plugin set is fixed at harness time.** New plugins do not
  retroactively join the corpus — adding `golden/<plugin>/…`
  entries is a manual step gated by intentional inclusion.

— Sprint 13 push 1.
