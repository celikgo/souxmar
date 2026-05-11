#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compare two Google Benchmark JSON reports and exit non-zero on regression.

Usage:
    compare.py --baseline <path> --current <path> [--threshold 0.10]

Exit code 0 if every benchmark is within `threshold` (default 10%) of the
baseline; non-zero otherwise. Prints a per-benchmark table to stdout in
either case so the GitHub Actions log carries the diff regardless.

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


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--baseline", type=Path, required=True)
    p.add_argument("--current",  type=Path, required=True)
    p.add_argument("--threshold", type=float, default=0.10,
                   help="Fail when current/baseline > 1 + threshold (default 0.10)")
    args = p.parse_args(argv)

    if not args.baseline.exists():
        print(f"error: baseline {args.baseline} does not exist", file=sys.stderr)
        return 2
    if not args.current.exists():
        print(f"error: current {args.current} does not exist", file=sys.stderr)
        return 2

    baseline = load_benchmarks(args.baseline)
    current  = load_benchmarks(args.current)
    if not baseline:
        print(f"error: no benchmarks parsed from {args.baseline}", file=sys.stderr)
        return 2
    if not current:
        print(f"error: no benchmarks parsed from {args.current}", file=sys.stderr)
        return 2

    regressed, rows = compare(baseline, current, args.threshold)
    print(render(rows, args.threshold))
    if regressed:
        print(f"\nERROR: at least one benchmark regressed beyond {args.threshold * 100:.0f}%",
              file=sys.stderr)
        return 1
    print(f"\nOK: every benchmark within {args.threshold * 100:.0f}% of baseline")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
