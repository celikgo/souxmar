# Performance baselines

This directory holds committed Google Benchmark JSON reports the
`perf-nightly` CI workflow compares fresh runs against.

## Files

- `main.json` (not yet committed) — the canonical baseline for the
  `main` branch. When this file is absent the workflow logs a warning
  and proceeds without failing on regression. Establishing it is the
  "baseline established" exit criterion in the Sprint 5 plan.

## Update workflow

The baseline is **committed**, not auto-rotated. Performance work that
shifts numbers in the right direction (a faster algorithm, a tighter
loop) updates the baseline in the same PR; the PR title should call
out the deliberate motion so reviewers don't quietly accept a
regression as the new normal.

To regenerate locally:

```sh
cmake -B build/perf -DCMAKE_BUILD_TYPE=Release \
                    -DSOUXMAR_BUILD_BENCHMARKS=ON \
                    -DVCPKG_MANIFEST_FEATURES=tests
cmake --build build/perf --parallel

./build/perf/benchmarks/bench_mesh_construction \
    --benchmark_format=json \
    --benchmark_out=benchmarks/baselines/main.json \
    --benchmark_out_format=json \
    --benchmark_repetitions=5 \
    --benchmark_min_time=0.5s
```

A `--benchmark_repetitions=5` run on the CI hardware tier is the
target — three repetitions plus the workflow's 0.2 s `min_time` is
enough to catch order-of-magnitude regressions but too noisy for
sub-10% comparisons; five reps + 0.5 s settles enough on idle hosts.

## Regression threshold

The workflow defaults to a 10% regression threshold (an environment
variable in `perf-nightly.yml`). Tighter thresholds are tempting but
fight the noise floor on shared CI runners — bump to 0.15 or 0.20
when a benchmark proves volatile, never the other way without
matching dedicated hardware.

## What lives here vs. what doesn't

This directory holds **expected numbers** — small, committed, diffable.
Per-run benchmark artifacts uploaded by the workflow are NOT
committed; they live as workflow artifacts (30-day retention,
configurable in `perf-nightly.yml`).
