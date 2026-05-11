# Performance baselines

This directory holds committed Google Benchmark JSON reports the `Perf`
CI workflow compares fresh runs against. The gate fails any PR that
regresses a tracked benchmark by more than the threshold (5% from
Sprint 9 push 6, was 10% under the Sprint 5 nightly-only regime — see
[`docs/ENGINEERING_PRACTICES.md`](../../docs/ENGINEERING_PRACTICES.md)
§ Performance budgets).

## File layout (Sprint 9 push 6)

One JSON file per benchmark binary, named after the binary's source
file stem. The workflow's `compare.py --baseline-dir / --current-dir`
mode matches baselines to fresh reports by file name.

| Baseline file                  | Benchmark binary             | Covered surface                                                  |
| ------------------------------ | ---------------------------- | ---------------------------------------------------------------- |
| `bench_mesh_construction.json` | `bench_mesh_construction`    | Per-element vs. bulk mesh construction (`souxmar_mesh_from_buffers`). |
| `bench_mmap_buffer.json`       | `bench_mmap_buffer`          | Heap vs. mmap buffer round-trip (ADR-0006 v2).                   |
| `bench_face_tag.json`          | `bench_face_tag`             | Per-face-tag sparse-map throughput (ADR-0012, ABI v1.3).         |

Files absent from this directory are reported by `compare.py` as
"(new — no baseline yet; skipping)" and do **not** fail the gate.
This is deliberate: the first run after a new benchmark binary lands
always sees an empty baseline for it, and we don't want that to block
the PR that introduces the benchmark. The next baseline rotation
captures the new file along with everything else.

A baseline file is also allowed to disappear without failing the gate
(the workflow reports "(removed — no current report)") — removing a
benchmark is a deliberate motion handled at PR review.

## Update workflow

Baselines are **committed**, not auto-rotated. Performance work that
shifts numbers in the right direction (a faster algorithm, a tighter
loop) updates the relevant baseline in the same PR; the PR title
should call out the deliberate motion so reviewers don't quietly
accept a regression as the new normal.

To regenerate every baseline locally:

```sh
cmake -B build/perf -DCMAKE_BUILD_TYPE=Release \
                    -DSOUXMAR_BUILD_BENCHMARKS=ON \
                    -DVCPKG_MANIFEST_FEATURES=tests
cmake --build build/perf --parallel

for bench in bench_mesh_construction bench_mmap_buffer bench_face_tag; do
  ./build/perf/benchmarks/$bench \
      --benchmark_format=json \
      --benchmark_out=benchmarks/baselines/$bench.json \
      --benchmark_out_format=json \
      --benchmark_repetitions=5 \
      --benchmark_min_time=0.5s
done
```

A `--benchmark_repetitions=5` run on the CI hardware tier is the
target — three repetitions plus the workflow's 0.2 s `min_time` is
enough to catch order-of-magnitude regressions but too noisy for
sub-5% comparisons (the new threshold); five reps + 0.5 s settles
enough on idle hosts. Rotate the full suite at once unless you're
deliberately leaving one binary's baseline at a known-good earlier
revision.

## Regression threshold

The workflow defaults to a 5% regression threshold (Sprint 9 push 6;
`REGRESSION_THRESHOLD` env var in `perf-nightly.yml`) — matches
[`docs/ENGINEERING_PRACTICES.md`](../../docs/ENGINEERING_PRACTICES.md)
§ Performance budgets. A tighter threshold fights the noise floor on
shared CI runners; when a benchmark proves volatile in practice the
right move is to dedicate hardware (Sprint 9 carry-over) rather than
loosen the threshold. Any threshold change to `perf-nightly.yml`
requires a matching note in the PR description naming the noise-floor
data that motivated it.

## What lives here vs. what doesn't

This directory holds **expected numbers** — small, committed, diffable.
Per-run benchmark artifacts uploaded by the workflow are NOT
committed; they live as workflow artifacts (30-day retention,
configurable in `perf-nightly.yml`).
