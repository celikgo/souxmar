---
name: auditing-determinism
description: Use when investigating a determinism gate failure or auditing a new code path for cross-platform reproducibility. The same souxmar pipeline must produce byte-identical output on Linux, macOS, and Windows. Triggers on "determinism", "determinism gate", "non-reproducible", "different on Mac", "byte-identical", "reproducibility".
---

# Auditing pipeline determinism

souxmar's determinism guarantee: **the same pipeline file, run on the same inputs, produces byte-identical outputs on Linux, macOS, and Windows, on every supported architecture.** This is verified by the `Cross-platform determinism` gate in `.github/workflows/ci.yml`, which is blocking. See `docs/CI.md`.

This skill walks through diagnosing and fixing a determinism failure.

## When to use this skill

- The determinism CI gate failed on a PR.
- Adding a new solver, mesher, or numerical algorithm.
- Investigating a user report of "different results on different machines."
- Auditing a parallel reduction or any RNG-using code.

## When NOT to use this skill

- Performance work that does not change output. Determinism is preserved by construction.
- Pure refactors of non-numerical code.

## The determinism contract

Determinism applies to:

- The cantilever-beam canonical pipeline (`examples/cantilever-beam/`) — the gate.
- All in-tree solvers' output for fixed-input tests.
- Mesh output for the native mesher and Gmsh adapter on a fixed seed.
- VTU file content (excluding embedded timestamps, which are zeroed for tests).

Determinism does **not** apply to:

- Wall-clock timing in any output (timestamps explicitly zeroed in test mode).
- Log output ordering across threads.
- Plugin discovery order (manifests are sorted, but file system mtimes differ).
- Anything from a third-party tool we do not control (e.g. raw OpenFOAM `simpleFoam` log lines).

## Diagnosing a failure

The gate lives in `.github/workflows/ci.yml` as `Cross-platform determinism`. Each platform in the
engine matrix runs `scripts/ci/determinism-fingerprint.sh`, which executes every example pipeline
under a fixed `TZ` and `LC_ALL`, pipes the output through the synth-load normaliser
(`scripts/synth-load/golden/normalize.py`), and hashes it. The per-platform manifests are uploaded
with the engine artifact and diffed against each other in a downstream job.

**There is no golden file** — the platforms are each other's reference, so nothing can drift by
having an expected value quietly updated. A pipeline that fails identically everywhere is still
deterministic, so its exit code is part of the fingerprint rather than an abort; what fails the gate
is one platform disagreeing with another.

The failure is a unified diff of two manifests:

```
--- artifacts/engine-linux-gcc/build/ci-linux-gcc/determinism.txt
+++ artifacts/engine-macos/build/ci-macos/determinism.txt
-cantilever-beam  efbce55197134e6ad3a60a964f48a2985601f17010df51dec595d4b9cf7d2947
+cantilever-beam  b2e1a0c4...
```

### Step 1 — Reproduce locally

Run the same script against your own build and compare the line for the named pipeline:

```bash
scripts/ci/determinism-fingerprint.sh \
  --engine build/dev/src/cli/souxmar \
  --plugin-path "$PWD/build/dev/examples/plugins"
```

It is stable across repeated runs on one machine, so a difference between two of your own runs is
itself a finding. If you have access to the divergent platform, run it there too. If not, the
manifests in the CI artifacts are the comparison — note that they carry the *hash*, not the output,
so reproducing the underlying file means re-running the pipeline on that platform.

### Step 2 — Identify what diverged

```bash
# Strip ASCII headers; binary-diff the rest.
xxd build1/cantilever.vtu > b1.hex
xxd build2/cantilever.vtu > b2.hex
diff b1.hex b2.hex | head
```

Or use `vtkpython` to read both files and compare cell-by-cell, point-by-point.

Common patterns:
- **Last few decimal places of float values differ** → floating-point nondeterminism (parallel reduction order, fma vs non-fma, denormals).
- **Cell ordering differs** → unstable sort, hash-map iteration, parallel work-stealing without canonicalisation.
- **A single tag/ID is permuted** → unstable ID assignment, often from `std::unordered_map` or a `set` on pointer values.
- **Whole sections missing/extra** → conditional code paths firing differently (e.g. a SIMD path on one OS, scalar on another).

### Step 3 — Hypothesise, then bisect

If multiple commits sit between the last passing run and the failing one, bisect with the determinism gate as the predicate.

### Step 4 — Fix

#### Floating-point nondeterminism

- Use deterministic reduction in PETSc: `KSPSetReductionType(ksp, KSP_REDUCTION_DETERMINISTIC)`.
- Use deterministic Eigen: avoid `Eigen::ThreadPool` or pin its size to 1 in tests.
- For BLAS, pin to OpenBLAS-deterministic build or explicit single-threaded builds for tests.
- Avoid mixing fma and non-fma paths. If the compiler can choose, force one with `-ffp-contract=off`.

#### Unstable ordering

- Replace `std::unordered_map<K,V>` (iteration order unspecified) with `std::map<K,V>` or `absl::btree_map`.
- After parallel work, **always canonicalise** before serialising: sort by stable key (cell ID, node ID, etc.).
- Never iterate a `std::set<T*>` and serialise — pointer values vary across runs.

#### Conditional codepaths

- A SIMD path that fires on AVX2 but not on Apple silicon can produce different rounding. Validate that the scalar fallback produces the same result, or gate the SIMD path off in determinism-mode tests.
- Compiler intrinsics (`_mm_dp_ps`, etc.) sometimes differ across compilers. Avoid them in numerics; use portable intrinsics or scalar code with vectorisation hints.

#### Random-number generators

- All RNGs must be seedable. The pipeline file specifies the seed.
- All RNGs must use a portable algorithm (`std::mt19937` is portable across STLs; PCG is portable; `std::default_random_engine` is NOT — it varies by STL).

## Adding determinism to a new code path

When adding a new solver, mesher, or transformation:

1. **Identify nondeterminism sources.** Parallel reductions, hash-map iteration, RNGs, third-party tool calls.
2. **Pin them.** Stable iteration, canonical sorts, seeded RNGs, stable backend modes.
3. **Add the pipeline to the determinism gate.** There is no `tests/determinism/` directory: the
   gate runs every pipeline under `examples/` through
   `scripts/ci/determinism-fingerprint.sh`, so a new example is picked up automatically. What a
   regression test looks like:
   ```yaml
   - pipeline: tests/determinism/<name>.souxmar.yaml
     expected_hash:
       - file: results/<name>.vtu
         sha256_seed: 1
         tolerance: byte-identical
   ```
4. **Run it 5 times locally.** All five hashes match? Good. Then push.

## Common mistakes

- "It's only the last 3 bits — that's noise." No. Byte-identical is the contract. The reason 3 bits matter is that downstream cell-comparison code branches on them, and 3 bits become whole different mesh decisions.
- Treating determinism as the test's problem ("update the expected hash"). Do not. The determinism gate exists *because* updating the expected hash silently is what masks regressions.
- Using `std::unordered_set<int>` for "I don't care about order" — and then serialising the result into a file the determinism gate hashes.
- Calling `std::random_device` anywhere in the numerical path. Even once. The seed must come from the pipeline.
- Multi-threaded tests that read from the system clock to seed RNGs. Use the pipeline's seed.

## Cross-platform float gotchas

- macOS arm64 enables FMA by default; Linux x86_64 may not. Disable globally in our build for numerical paths: `-ffp-contract=off`.
- Windows MSVC's denormals-as-zero default differs from gcc/clang. Standardise via a startup hook that calls `_MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_OFF)` and equivalents.
- `tgamma`, `lgamma`, and other transcendentals differ in last-bit accuracy across libms. Use Boost.Math when bit-exactness matters.

## Reference

- `docs/ENGINEERING_PRACTICES.md` — determinism gate definition.
- `docs/attic/SPRINT_PLAN.md` — when the gate became enforcing (Sprint 5).
- `scripts/ci/determinism-fingerprint.sh` — the gate itself, and `examples/` for the pipelines
  it covers. (`tests/determinism/` does not exist; the gate walks `examples/`.)
- `scripts/ci/determinism-fingerprint.sh` — the comparison harness.
