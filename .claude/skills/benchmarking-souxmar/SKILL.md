---
name: benchmarking-souxmar
description: Use when running performance benchmarks against the souxmar baseline, validating perf budgets in ENGINEERING_PRACTICES.md, or investigating a performance regression caught in CI. Triggers on "benchmark", "performance regression", "perf budget", "perf gate".
---

# Benchmarking souxmar

souxmar enforces hard performance budgets in `docs/ENGINEERING_PRACTICES.md`. CI gates merges on regressions > 5 % against the baseline. This skill walks through running benchmarks locally, interpreting results, and triaging a regression.

## When to use this skill

- A PR has been flagged with a perf regression in CI.
- A change was made to a hot path (assembly, mesh refinement, viewport rendering) and the author needs to confirm the change.
- Establishing a new benchmark for a feature being added.
- Investigating a "feels slow" report.

## When NOT to use this skill

- Micro-optimising code that is not on a hot path. Profile first; if it is not measurably hot, do not optimise.
- Benchmarking against an unreleased baseline. Use the released baseline pinned in `benchmarks/baseline.json`.

## Reference machine

Performance budgets are calibrated to:

- macOS arm64: M2 Pro, 16 GB RAM
- Linux x86_64: Ryzen 7 7700X, 32 GB RAM
- Windows x86_64: same Ryzen, dual-boot

If your local machine is meaningfully faster or slower, run benchmarks in CI against the reference runners — not locally. CI results are authoritative.

## Running benchmarks locally

```bash
cmake -B build -DSOUXMAR_BENCHMARKS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --target benchmarks
./build/benchmarks/souxmar-bench --output=json --output-file=results.json
```

For a specific benchmark suite:

```bash
./build/benchmarks/souxmar-bench --benchmark_filter='Cantilever.*'
./build/benchmarks/souxmar-bench --benchmark_filter='Viewport.*'
./build/benchmarks/souxmar-bench --benchmark_filter='Assembly.*'
```

For desktop perf (cold launch, frame time):

```bash
pnpm -C src/desktop bench
```

For agent latency:

```bash
souxmar agent bench --provider anthropic --tasks tests/agent-eval/canonical.yaml
```

## Comparing to baseline

```bash
tools/bench-compare.sh results.json benchmarks/baseline.json
```

Output is per-benchmark delta as % change. Anything > 5 % regression on a tracked benchmark is a CI block.

## Tracked benchmarks (current set)

| Benchmark                                    | Budget          | Owner                |
| -------------------------------------------- | --------------- | -------------------- |
| `Cantilever.Mesh50k`                         | < 2.5 s         | Adapters             |
| `Cantilever.Solve50k`                        | < 1.0 s         | Adapters (solver)    |
| `Cantilever.EndToEnd`                        | < 4.0 s         | Adapters + Pipeline  |
| `Pipeline.CacheHit`                          | < 50 ms         | Core                 |
| `Pipeline.Discovery10`                       | < 60 ms         | Plugin Host          |
| `PluginCall.NoOp.Warm`                       | < 20 µs         | Plugin Host          |
| `Viewport.Open1M`                            | < 2.0 s         | Desktop              |
| `Viewport.Rotate5M.FrameP95`                 | < 16.7 ms       | Desktop              |
| `Desktop.ColdLaunch`                         | < 1.5 s         | Desktop              |
| `Agent.FirstToken.BYOK.Anthropic`            | < 800 ms p95    | AI                   |
| `Agent.FirstToken.Managed`                   | < 1200 ms p95   | AI (post-S14)        |
| `Agent.PromptCacheHitRate`                   | > 70 %          | AI                   |

The full set lives in `benchmarks/registry.json`.

## Triaging a regression

When CI reports a regression:

1. **Identify the benchmark.** The CI report names the specific benchmark and the % delta.
2. **Reproduce locally.** Build at the PR's HEAD; run the affected benchmark; confirm the regression.
3. **Bisect.** If the PR has multiple commits, bisect to find which commit introduced the regression.
4. **Profile.** Linux: `perf record` + `perf report`. macOS: Instruments → Time Profiler. Windows: WPA / xperf.
5. **Form a hypothesis.** What changed in the hot path? New allocation? Cache miss? Branch prediction? Lock contention?
6. **Fix or document.** Either resolve the regression in the same PR, or update the baseline via an RFC if the change is intentional (e.g. trading speed for correctness).

We do **not** silently slip budgets. Either the regression is fixed or the budget is renegotiated explicitly.

## Establishing a new benchmark

When adding a feature on a hot path:

1. Add a benchmark to `benchmarks/<area>/`.
2. Use `google-benchmark` for C++; `vitest --bench` for TypeScript.
3. Choose a budget conservatively — start at 1.5× the measured value on the reference machine, tighten over time as the implementation matures.
4. Add the benchmark to `benchmarks/registry.json` with its budget.
5. Submit the new baseline via PR; the Platform team approves.

## Common mistakes

- Benchmarking debug builds. Always `-DCMAKE_BUILD_TYPE=Release` and `-O3` (or RelWithDebInfo for profiling).
- Comparing runs across different machines without reference-runner adjustment.
- Reporting "average" instead of "p95" or "median". Latency budgets are p95.
- Benchmarking once. Run at least 5 iterations; report median + p95.
- Forgetting to disable CPU frequency scaling: `cpupower frequency-set -g performance` on Linux.
- Confusing user-time with wall-clock time in parallel benchmarks.

## Reference

- `docs/ENGINEERING_PRACTICES.md` — performance budget table.
- `benchmarks/registry.json` — full tracked set.
- `benchmarks/baseline.json` — current baseline (per release).
- `tools/bench-compare.sh` — comparison harness.
