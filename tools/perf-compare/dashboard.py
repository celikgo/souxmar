#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Render Google Benchmark JSON reports into a self-contained HTML dashboard.

Sprint 9 push 8 — the DX-team "Benchmark dashboard published per release"
story from SPRINT_PLAN.md. Reads `--input-dir` (one Google Benchmark JSON
per binary, the same `perf-report/` shape the Perf workflow produces) and
emits one HTML file with per-benchmark cards, inline-SVG bar charts, and
regression colour coding against the optional `--baseline-dir`.

Design notes:

- **Self-contained.** No external CSS, no JavaScript, no font CDNs. The
  HTML is one file you can attach to a GitHub Release, view offline, or
  email to a reviewer. Inline SVG is enough for the workload-bar charts;
  inline CSS handles the dim-mode theme so it slots into the souxmar
  visual identity.
- **Stdlib only.** Same constraint as `compare.py`: this script runs on
  CI runners that don't get extra `pip install` steps. `json`, `html`,
  `pathlib`, `argparse`, `datetime`, `os` are all we need.
- **Defensive against partial input.** New benchmarks without a baseline
  produce a "no baseline yet" card rather than a missing entry; removed
  baselines surface as "removed". Same carve-outs `compare.py` honors.
- **Threshold is the same as the gate's.** 0.05 is the Sprint 9 push 6
  default, matching `docs/ENGINEERING_PRACTICES.md` § Performance
  budgets. Anything beyond the threshold is rendered in red; anything
  better than -threshold in green; within-noise is muted.

Usage:

    dashboard.py --input-dir perf-report --output perf-report/index.html \\
                 [--baseline-dir benchmarks/baselines] \\
                 [--threshold 0.05] \\
                 [--git-ref <commit-sha-or-tag>] \\
                 [--title "souxmar v0.9.0-rc1 benchmark report"]
"""
from __future__ import annotations

import argparse
import html
import json
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Iterable

# ----- Reading Google Benchmark JSON ---------------------------------------


def load_runs(path: Path) -> tuple[dict, list[dict]]:
    """Return (top_level_metadata, per_benchmark_records).

    Each record is a normalised dict carrying name, args, real_time_ns,
    cpu_time_ns, time_unit, iterations, and (when present) the mean +
    stddev aggregates Google Benchmark emits with
    --benchmark_repetitions > 1.
    """
    raw = json.loads(path.read_text())
    benches: list[dict] = raw.get("benchmarks", [])

    # Bucket entries by base name: raw runs (run_type=iteration) live
    # alongside aggregates (run_type=aggregate with aggregate_name in
    # {mean, median, stddev, ...}). We attribute the aggregates back to
    # the run name they describe, then build one record per name.
    raw_runs: dict[str, dict] = {}
    aggregates: dict[str, dict[str, dict]] = {}
    for b in benches:
        name = b.get("name", "")
        if b.get("run_type") == "aggregate":
            agg_name  = b.get("aggregate_name", "")
            base      = name.removesuffix(f"_{agg_name}") if agg_name else name
            aggregates.setdefault(base, {})[agg_name] = b
            continue
        raw_runs.setdefault(name, b)

    records: list[dict] = []
    seen_names: set[str] = set()
    for name in list(raw_runs.keys()) + [n for n in aggregates if n not in raw_runs]:
        if name in seen_names:
            continue
        seen_names.add(name)
        base = raw_runs.get(name, {})
        agg  = aggregates.get(name, {})
        mean   = agg.get("mean",   base)
        stddev = agg.get("stddev", {}).get("real_time", 0.0)
        time_unit = mean.get("time_unit", base.get("time_unit", "ns"))
        records.append({
            "name":         name,
            "real_time":    mean.get("real_time",  base.get("real_time", 0.0)),
            "cpu_time":     mean.get("cpu_time",   base.get("cpu_time",  0.0)),
            "stddev_ns":    stddev,
            "iterations":   base.get("iterations", 0),
            "time_unit":    time_unit,
        })

    metadata = {
        "host_name":     raw.get("context", {}).get("host_name", ""),
        "num_cpus":      raw.get("context", {}).get("num_cpus", 0),
        "cpu_scaling":   raw.get("context", {}).get("cpu_scaling_enabled", False),
        "library_build": raw.get("context", {}).get("library_build_type", ""),
        "date":          raw.get("context", {}).get("date", ""),
    }
    return metadata, records


def collect_input_dir(input_dir: Path) -> dict[str, tuple[dict, list[dict]]]:
    """Return {binary_name: (metadata, records)} for every *.json in `input_dir`."""
    out: dict[str, tuple[dict, list[dict]]] = {}
    for p in sorted(input_dir.glob("*.json")):
        try:
            out[p.stem] = load_runs(p)
        except (json.JSONDecodeError, OSError) as e:
            # Render a placeholder record so the dashboard at least
            # surfaces the file's existence + the parse failure rather
            # than silently dropping it.
            out[p.stem] = ({"error": f"failed to parse: {e}"}, [])
    return out


# ----- Rendering helpers ----------------------------------------------------


def _fmt_time(ns: float, unit: str) -> str:
    """Render a Google-Benchmark real_time value in human-readable units.

    Google Benchmark records times in the `time_unit` it elected — `ns`
    by default, but `us` / `ms` for --Unit() overrides. We respect that
    so the displayed unit matches what an operator running the binary
    locally would see.
    """
    if ns <= 0 or ns != ns:  # zero / NaN
        return "—"
    if unit == "ns":
        if ns >= 1_000_000_000: return f"{ns / 1_000_000_000:.3f} s"
        if ns >= 1_000_000:     return f"{ns / 1_000_000:.3f} ms"
        if ns >= 1_000:         return f"{ns / 1_000:.3f} µs"
        return f"{ns:.1f} ns"
    if unit == "us":
        if ns >= 1_000_000:     return f"{ns / 1_000_000:.3f} s"
        if ns >= 1_000:         return f"{ns / 1_000:.3f} ms"
        return f"{ns:.3f} µs"
    if unit == "ms":
        if ns >= 1_000:         return f"{ns / 1_000:.3f} s"
        return f"{ns:.3f} ms"
    return f"{ns:.3f} {unit}"


def _delta_class(ratio: float, threshold: float) -> str:
    """Map a current/baseline ratio to a CSS class name."""
    if ratio != ratio:                  return "muted"   # NaN — no baseline
    if ratio > 1.0 + threshold:         return "regress"
    if ratio < 1.0 - threshold:         return "improve"
    return "within"


def _delta_label(ratio: float) -> str:
    if ratio != ratio:
        return "—"
    pct = (ratio - 1.0) * 100.0
    return f"{pct:+.1f}%"


# ----- HTML emission --------------------------------------------------------


CSS = """
:root {
  color-scheme: dark;
  --bg: #15202B;
  --panel: #1c2732;
  --panel-2: #22303C;
  --text: #E7E9EA;
  --muted: #8B98A5;
  --accent: #1D9BF0;
  --regress: #F4212E;
  --improve: #00BA7C;
  --within: #8B98A5;
  --border: #2F3336;
}
* { box-sizing: border-box; }
body {
  margin: 0;
  font-family: -apple-system, BlinkMacSystemFont, "Inter", "Segoe UI",
               sans-serif;
  background: var(--bg);
  color: var(--text);
  font-size: 14px;
  line-height: 1.5;
}
header {
  padding: 24px 32px 16px;
  border-bottom: 1px solid var(--border);
}
h1 { font-size: 22px; font-weight: 700; margin: 0 0 4px; }
.meta { color: var(--muted); font-size: 13px; }
.meta code { font-family: "JetBrains Mono", ui-monospace, monospace; }
main { padding: 24px 32px 48px; }
.bin {
  background: var(--panel);
  border: 1px solid var(--border);
  border-radius: 12px;
  padding: 20px 24px;
  margin-bottom: 20px;
}
.bin h2 {
  font-size: 16px;
  font-weight: 600;
  margin: 0 0 16px;
  font-family: "JetBrains Mono", ui-monospace, monospace;
}
.bin h2 .badge {
  display: inline-block;
  margin-left: 10px;
  padding: 2px 8px;
  font-size: 11px;
  border-radius: 6px;
  background: var(--panel-2);
  color: var(--muted);
  font-family: -apple-system, BlinkMacSystemFont, "Inter", sans-serif;
  font-weight: 500;
}
.bin h2 .badge.new     { color: var(--accent);  border: 1px solid var(--accent);  }
.bin h2 .badge.removed { color: var(--muted);   border: 1px solid var(--muted);   }
.bin h2 .badge.regress { color: var(--regress); border: 1px solid var(--regress); }
.bin h2 .badge.improve { color: var(--improve); border: 1px solid var(--improve); }
table {
  width: 100%;
  border-collapse: collapse;
  font-size: 13px;
}
th {
  text-align: left;
  font-weight: 500;
  color: var(--muted);
  padding: 6px 10px;
  border-bottom: 1px solid var(--border);
}
td {
  padding: 6px 10px;
  border-bottom: 1px solid var(--border);
  font-variant-numeric: tabular-nums;
}
tr:last-child td { border-bottom: none; }
td.num { text-align: right; font-family: "JetBrains Mono", ui-monospace, monospace; }
td.name { font-family: "JetBrains Mono", ui-monospace, monospace; }
.delta {
  display: inline-block;
  padding: 1px 8px;
  border-radius: 999px;
  font-size: 12px;
  font-weight: 600;
  font-variant-numeric: tabular-nums;
  font-family: "JetBrains Mono", ui-monospace, monospace;
}
.delta.regress { color: var(--regress); background: rgba(244, 33, 46, 0.10);  }
.delta.improve { color: var(--improve); background: rgba(0,  186, 124, 0.10); }
.delta.within  { color: var(--within);  background: rgba(139, 152, 165, 0.10); }
.delta.muted   { color: var(--muted);   }
.bar-cell { width: 30%; padding: 6px 10px; }
.bar {
  height: 6px;
  background: var(--panel-2);
  border-radius: 999px;
  position: relative;
  overflow: hidden;
}
.bar > span {
  display: block;
  height: 100%;
  background: var(--accent);
  border-radius: 999px;
}
.bar.regress > span { background: var(--regress); }
.bar.improve > span { background: var(--improve); }
.empty {
  color: var(--muted);
  padding: 16px 0;
  font-style: italic;
}
footer {
  padding: 24px 32px;
  color: var(--muted);
  font-size: 12px;
  border-top: 1px solid var(--border);
}
footer a { color: var(--accent); }
""".strip()


def render_bin_card(binary_name: str,
                    current_meta: dict,
                    current_records: list[dict],
                    baseline_records: list[dict] | None,
                    threshold: float,
                    state: str) -> str:
    """Emit one .bin <section> for a single benchmark binary."""
    parts: list[str] = []
    badge = ""
    if state == "new":
        badge = '<span class="badge new">new — no baseline yet</span>'
    elif state == "removed":
        badge = '<span class="badge removed">removed — no current report</span>'

    # Build a baseline lookup by record name (for the matched-binary path).
    baseline_by_name: dict[str, dict] = {}
    if baseline_records:
        for r in baseline_records:
            baseline_by_name[r["name"]] = r

    # Sniff whether this binary has at least one regression / improvement
    # to drive the header badge.
    has_regress = False
    has_improve = False
    for r in current_records:
        base = baseline_by_name.get(r["name"])
        if not base or base["real_time"] <= 0:
            continue
        ratio = r["real_time"] / base["real_time"]
        if ratio > 1.0 + threshold: has_regress = True
        if ratio < 1.0 - threshold: has_improve = True
    if has_regress:
        badge += ' <span class="badge regress">regression</span>'
    elif has_improve:
        badge += ' <span class="badge improve">improvement</span>'

    parts.append(f'<section class="bin"><h2>{html.escape(binary_name)}{badge}</h2>')

    if state == "removed":
        parts.append('<div class="empty">No current report — baseline retained only.</div>')
        parts.append('</section>')
        return "".join(parts)

    if "error" in current_meta:
        parts.append(f'<div class="empty">⚠ {html.escape(current_meta["error"])}</div>')
        parts.append('</section>')
        return "".join(parts)

    records = current_records
    if not records:
        parts.append('<div class="empty">No benchmark records.</div>')
        parts.append('</section>')
        return "".join(parts)

    # Maximum real_time across the binary's records, used to size the
    # per-row bar charts.
    max_rt = max((r["real_time"] for r in records), default=1.0) or 1.0

    parts.append('<table>')
    parts.append('<thead><tr>'
                 '<th>benchmark</th>'
                 '<th>iterations</th>'
                 '<th style="text-align:right">real time</th>'
                 '<th style="text-align:right">vs. baseline</th>'
                 '<th></th>'
                 '</tr></thead><tbody>')
    for r in records:
        base   = baseline_by_name.get(r["name"])
        ratio  = float("nan")
        if base and base["real_time"] > 0:
            ratio = r["real_time"] / base["real_time"]
        delta_cls = _delta_class(ratio, threshold)
        bar_pct   = max(1.0, min(100.0, 100.0 * r["real_time"] / max_rt))
        bar_cls   = (delta_cls if delta_cls in ("regress", "improve") else "")
        parts.append(
            '<tr>'
            f'<td class="name">{html.escape(r["name"])}</td>'
            f'<td class="num">{r["iterations"]:,}</td>'
            f'<td class="num">{_fmt_time(r["real_time"], r["time_unit"])}</td>'
            f'<td class="num"><span class="delta {delta_cls}">{_delta_label(ratio)}</span></td>'
            f'<td class="bar-cell"><div class="bar {bar_cls}">'
            f'<span style="width:{bar_pct:.1f}%"></span></div></td>'
            '</tr>')
    parts.append('</tbody></table>')

    # Host / build context line, when present.
    host = current_meta.get("host_name", "")
    cpus = current_meta.get("num_cpus", 0)
    build = current_meta.get("library_build", "")
    date  = current_meta.get("date", "")
    meta_bits = []
    if host:  meta_bits.append(f"<code>{html.escape(host)}</code>")
    if cpus:  meta_bits.append(f"{cpus} CPU")
    if build: meta_bits.append(html.escape(build))
    if date:  meta_bits.append(html.escape(date))
    if meta_bits:
        parts.append(f'<div class="meta" style="margin-top:10px">{" · ".join(meta_bits)}</div>')

    parts.append('</section>')
    return "".join(parts)


def render_html(*,
                title: str,
                threshold: float,
                git_ref: str,
                current: dict[str, tuple[dict, list[dict]]],
                baseline: dict[str, tuple[dict, list[dict]]]) -> str:
    parts: list[str] = []
    parts.append('<!doctype html>')
    parts.append('<html lang="en"><head><meta charset="utf-8">')
    parts.append(f'<title>{html.escape(title)}</title>')
    parts.append(f'<style>{CSS}</style>')
    parts.append('</head><body>')
    parts.append('<header>')
    parts.append(f'<h1>{html.escape(title)}</h1>')
    now = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC")
    meta_line: list[str] = [f"generated {now}"]
    if git_ref:
        meta_line.append(f"<code>{html.escape(git_ref)}</code>")
    meta_line.append(f"regression threshold {threshold * 100:.0f}%")
    parts.append(f'<div class="meta">{" · ".join(meta_line)}</div>')
    parts.append('</header>')
    parts.append('<main>')

    all_names = sorted(set(current) | set(baseline))
    if not all_names:
        parts.append('<div class="empty">No benchmark JSON files found.</div>')
    for name in all_names:
        cur = current.get(name)
        base = baseline.get(name)
        if cur and base:
            state = "compared"
            parts.append(render_bin_card(name, cur[0], cur[1], base[1], threshold, state))
        elif cur and not base:
            state = "new"
            parts.append(render_bin_card(name, cur[0], cur[1], None, threshold, state))
        elif base and not cur:
            state = "removed"
            parts.append(render_bin_card(name, base[0], base[1], None, threshold, state))

    parts.append('</main>')
    parts.append('<footer>')
    parts.append('Built by <code>tools/perf-compare/dashboard.py</code> · ')
    parts.append('see <code>docs/ENGINEERING_PRACTICES.md</code> § Performance budgets · ')
    parts.append('<a href="https://github.com/souxmar/souxmar">souxmar</a>')
    parts.append('</footer>')
    parts.append('</body></html>')
    return "\n".join(parts)


# ----- CLI ------------------------------------------------------------------


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--input-dir",    type=Path, required=True,
                   help="Directory containing the fresh perf-report/*.json files.")
    p.add_argument("--output",       type=Path, required=True,
                   help="Path to write the HTML report (created if absent).")
    p.add_argument("--baseline-dir", type=Path,
                   help="Optional directory of baseline *.json files for delta colouring.")
    p.add_argument("--threshold",    type=float, default=0.05,
                   help="Regression threshold (default 0.05 — matches the gate's).")
    p.add_argument("--git-ref",      type=str, default="",
                   help="Commit SHA or tag to show in the header (empty = omit).")
    p.add_argument("--title",        type=str, default="souxmar benchmark report",
                   help="Title displayed in the header.")
    args = p.parse_args(argv)

    if not args.input_dir.is_dir():
        print(f"error: --input-dir {args.input_dir} is not a directory",
              file=sys.stderr)
        return 2

    current  = collect_input_dir(args.input_dir)
    baseline = collect_input_dir(args.baseline_dir) if args.baseline_dir and args.baseline_dir.is_dir() else {}

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(render_html(
        title=args.title,
        threshold=args.threshold,
        git_ref=args.git_ref,
        current=current,
        baseline=baseline,
    ))
    n_binaries = len(set(current) | set(baseline))
    print(f"wrote {args.output} ({n_binaries} benchmark binaries)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
