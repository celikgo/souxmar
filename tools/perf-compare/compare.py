#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compare Google Benchmark JSON reports and exit non-zero on regression.

Two modes:

    # Single-file mode (legacy — one benchmark binary at a time):
    compare.py --baseline <path> --current <path> [--threshold 0.05]

    # Directory mode (Sprint 9 push 6 — full suite at once):
    compare.py --baseline-dir <dir> --current-dir <dir> [--threshold 0.05]

In directory mode every `<name>.json` in `--current-dir` is compared
against the same-named file in `--baseline-dir`. Files present in
current but missing in baseline are reported as "(new — no baseline)"
and do not fail the gate; files present in baseline but missing in
current are reported as "(removed)" and do not fail the gate either
(removing a benchmark is a deliberate motion handled at PR review).

Exit code 0 if every benchmark is within `threshold` (default 5%, per
docs/ENGINEERING_PRACTICES.md § Performance budgets, lowered from the
Sprint 5 nightly's 10% by Sprint 9 push 6) of the baseline; non-zero
otherwise. Prints a per-benchmark table to stdout in either case so
the GitHub Actions log carries the diff regardless.

Google Benchmark's JSON format records `cpu_time` + `real_time` per
benchmark. We compare `real_time` (wall clock) since that's what users
feel and what the bulk-vs-incremental story is about.

Forward-looking: when we add memory metrics or per-platform baselines,
this script is the single place to extend. Keep it boring and
readable — it's the gate between "perf changed" and "alarm fires," so
opacity here is expensive.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Iterable


def load_benchmarks(path: Path) -> dict[str, float]:
    """Return {benchmark_name: real_time_ns}.

    Google Benchmark's "aggregate" runs (with --benchmark_repetitions>1)
    produce one entry per (name, aggregate) pair. We prefer the `_mean`
    aggregate when present — that's the noise-reduced number — and fall
    back to the raw run if not.
    """
    raw = json.loads(path.read_text())
    benches = raw.get("benchmarks", [])
    by_name: dict[str, float] = {}
    means: dict[str, float] = {}
    for b in benches:
        name = b.get("name", "")
        # Skip aggregate entries we don't want as fallback values
        if b.get("run_type") == "aggregate":
            if b.get("aggregate_name") == "mean":
                means[name.replace("_mean", "")] = b["real_time"]
            continue
        # Raw iteration run.
        by_name.setdefault(name, b["real_time"])
    # Prefer means.
    for n, v in means.items():
        by_name[n] = v
    return by_name


def compare(baseline: dict[str, float],
            current: dict[str, float],
            threshold: float) -> tuple[bool, list[tuple[str, float, float, float]]]:
    """Returns (any_regression, rows).

    Rows are (name, baseline_ns, current_ns, ratio) where ratio is
    current/baseline. A row is a regression when ratio > 1 + threshold.
    """
    rows: list[tuple[str, float, float, float]] = []
    any_regression = False
    names: Iterable[str] = sorted(set(baseline) | set(current))
    for name in names:
        b = baseline.get(name)
        c = current.get(name)
        if b is None or c is None or b <= 0:
            ratio = float("nan")
        else:
            ratio = c / b
        if b is not None and c is not None and b > 0:
            if ratio > 1.0 + threshold:
                any_regression = True
        rows.append((name, b or float("nan"), c or float("nan"), ratio))
    return any_regression, rows


def render(rows: list[tuple[str, float, float, float]], threshold: float) -> str:
    out = ["", f"{'benchmark':<48}  {'baseline (ns)':>14}  {'current (ns)':>14}  {'delta':>8}", "  " + "-" * 84]
    for name, b, c, ratio in rows:
        if ratio != ratio:  # NaN — one side missing
            delta = "    n/a"
        else:
            pct = (ratio - 1.0) * 100.0
            marker = ""
            if ratio > 1.0 + threshold:
                marker = " ⚠"
            elif ratio < 1.0 - threshold:
                marker = " ↓"
            delta = f"{pct:+6.1f}%{marker}"
        out.append(f"  {name:<48}  {b:>14.0f}  {c:>14.0f}  {delta:>8}")
    return "\n".join(out)


def compare_single_pair(baseline_path: Path, current_path: Path,
                        threshold: float, label: str) -> tuple[bool, str]:
    """Compare one (baseline, current) JSON file pair.

    Returns (regressed, rendered_table). Errors (missing file, empty
    parse) raise ValueError so the directory-mode driver can decide
    per-file how to react.
    """
    if not baseline_path.exists():
        raise ValueError(f"baseline {baseline_path} does not exist")
    if not current_path.exists():
        raise ValueError(f"current {current_path} does not exist")
    baseline = load_benchmarks(baseline_path)
    current  = load_benchmarks(current_path)
    if not baseline:
        raise ValueError(f"no benchmarks parsed from {baseline_path}")
    if not current:
        raise ValueError(f"no benchmarks parsed from {current_path}")
    regressed, rows = compare(baseline, current, threshold)
    header = f"\n=== {label} ==="
    return regressed, header + "\n" + render(rows, threshold)


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    # Single-file mode (legacy, one binary at a time).
    p.add_argument("--baseline", type=Path,
                   help="Baseline JSON file (single-file mode)")
    p.add_argument("--current",  type=Path,
                   help="Current JSON file (single-file mode)")
    # Directory mode (Sprint 9 push 6 — multi-binary suite).
    p.add_argument("--baseline-dir", type=Path,
                   help="Directory of baseline JSON files (directory mode)")
    p.add_argument("--current-dir",  type=Path,
                   help="Directory of current JSON files (directory mode)")
    p.add_argument("--threshold", type=float, default=0.05,
                   help="Fail when current/baseline > 1 + threshold "
                        "(default 0.05 — matches "
                        "docs/ENGINEERING_PRACTICES.md § Performance budgets)")
    args = p.parse_args(argv)

    single_mode = args.baseline is not None or args.current is not None
    dir_mode    = args.baseline_dir is not None or args.current_dir is not None
    if single_mode == dir_mode:
        print("error: pick exactly one of --baseline/--current or "
              "--baseline-dir/--current-dir", file=sys.stderr)
        return 2

    if single_mode:
        if args.baseline is None or args.current is None:
            print("error: --baseline and --current must both be set",
                  file=sys.stderr)
            return 2
        try:
            regressed, table = compare_single_pair(
                args.baseline, args.current, args.threshold,
                label=args.current.name)
        except ValueError as e:
            print(f"error: {e}", file=sys.stderr)
            return 2
        print(table)
        if regressed:
            print(f"\nERROR: at least one benchmark regressed beyond "
                  f"{args.threshold * 100:.0f}%", file=sys.stderr)
            return 1
        print(f"\nOK: every benchmark within {args.threshold * 100:.0f}% of baseline")
        return 0

    # Directory mode.
    if args.baseline_dir is None or args.current_dir is None:
        print("error: --baseline-dir and --current-dir must both be set",
              file=sys.stderr)
        return 2
    if not args.current_dir.is_dir():
        print(f"error: --current-dir {args.current_dir} is not a directory",
              file=sys.stderr)
        return 2
    if not args.baseline_dir.is_dir():
        print(f"warning: --baseline-dir {args.baseline_dir} is not a "
              "directory — no comparisons will run", file=sys.stderr)

    any_regression = False
    any_compared   = False
    skipped: list[str] = []
    current_files = sorted(args.current_dir.glob("*.json"))
    for cur in current_files:
        base = args.baseline_dir / cur.name
        if not base.exists():
            skipped.append(cur.name)
            print(f"\n=== {cur.name} ===\n  (new — no baseline yet; skipping)")
            continue
        try:
            regressed, table = compare_single_pair(
                base, cur, args.threshold, label=cur.name)
        except ValueError as e:
            print(f"\n=== {cur.name} ===\n  error: {e}", file=sys.stderr)
            return 2
        any_compared = True
        if regressed:
            any_regression = True
        print(table)

    # Files in baseline that aren't in current (removed benchmarks).
    if args.baseline_dir.is_dir():
        baseline_names = {p.name for p in args.baseline_dir.glob("*.json")}
        current_names  = {p.name for p in current_files}
        for missing in sorted(baseline_names - current_names):
            print(f"\n=== {missing} ===\n  (removed — no current report)")

    if not any_compared:
        if skipped:
            print(f"\nNOTE: no comparisons ran — {len(skipped)} new "
                  "benchmark(s) without a baseline. The first run after a "
                  "baseline rotation always lands in this state.")
        else:
            print("\nNOTE: no current reports found.")
        return 0
    if any_regression:
        print(f"\nERROR: at least one benchmark regressed beyond "
              f"{args.threshold * 100:.0f}%", file=sys.stderr)
        return 1
    print(f"\nOK: every benchmark within {args.threshold * 100:.0f}% of baseline")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
