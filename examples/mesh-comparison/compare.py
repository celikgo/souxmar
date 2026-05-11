#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""
Sprint 10 push 11 — mesh-algorithm comparison driver.

Runs `souxmar run pipeline-grid.yaml` and `souxmar run
pipeline-gmsh.yaml`, parses the per-cell quality fields out of the
resulting VTU files, and writes a markdown report comparing the two.

Stdlib-only by default. If matplotlib is importable, histograms are
rendered inline as base64 PNGs; otherwise we render ASCII-bar
histograms so the report is still readable without the optional dep.

Usage:
    python compare.py \\
      --souxmar    <path-to-souxmar-binary> \\
      --plugin-dir <path-to-built-examples-plugins>

Exit codes:
    0  both pipelines ran + the report was written
    1  at least one pipeline failed (the script keeps going to render
       a partial report; exit 1 just gates CI loops)
    2  usage error
"""

import argparse
import base64
import io
import json
import os
import re
import statistics
import subprocess
import sys
import time
from pathlib import Path
from typing import Dict, List, Optional, Tuple

try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    _HAS_MPL = True
except ImportError:  # pragma: no cover
    _HAS_MPL = False


HERE = Path(__file__).parent.resolve()


def run_pipeline(souxmar: Path, plugin_dir: Path, yaml_path: Path) -> Tuple[bool, float, str]:
    """Run one pipeline. Returns (ok, wallclock_seconds, captured_output)."""
    started = time.perf_counter()
    cmd = [
        str(souxmar), "run", str(yaml_path),
        "--plugin-path", str(plugin_dir),
    ]
    proc = subprocess.run(
        cmd, cwd=str(HERE), capture_output=True, text=True,
    )
    elapsed = time.perf_counter() - started
    captured = proc.stdout + proc.stderr
    return (proc.returncode == 0, elapsed, captured)


_QUALITY_FIELDS = ("jacobian", "aspect_ratio", "dihedral_min", "skewness")


def parse_vtu_quality(vtu_path: Path) -> Dict[str, List[float]]:
    """Extract per-cell quality arrays from a VTU file.

    souxmar's writer.vtu emits CellData arrays named after the
    postproc.mesh_quality outputs. We're hand-rolling the XML
    extraction (vs. linking VTK) because the comparison script
    should run on a stock Python — vtk-python is a heavy install
    not every user has.
    """
    out: Dict[str, List[float]] = {f: [] for f in _QUALITY_FIELDS}
    if not vtu_path.exists():
        return out
    body = vtu_path.read_text(errors="replace")
    # Each DataArray we care about looks like:
    #   <DataArray type="Float64" Name="jacobian" ...>0.91 0.88 0.95</DataArray>
    pattern = re.compile(
        r'<DataArray[^>]*\bName="(?P<name>[^"]+)"[^>]*>(?P<body>[^<]*)</DataArray>',
        re.DOTALL,
    )
    for m in pattern.finditer(body):
        name = m.group("name")
        if name not in _QUALITY_FIELDS:
            continue
        tokens = m.group("body").split()
        values: List[float] = []
        for t in tokens:
            try:
                values.append(float(t))
            except ValueError:
                continue
        out[name] = values
    return out


def cell_count(quality: Dict[str, List[float]]) -> int:
    for v in quality.values():
        if v:
            return len(v)
    return 0


def summarise(quality: Dict[str, List[float]]) -> Dict[str, Dict[str, float]]:
    s: Dict[str, Dict[str, float]] = {}
    for name, values in quality.items():
        if not values:
            s[name] = {}
            continue
        sv = sorted(values)
        def pct(p: float) -> float:
            i = max(0, min(len(sv) - 1, int(p * len(sv))))
            return sv[i]
        s[name] = {
            "min":    sv[0],
            "p01":    pct(0.01),
            "p25":    pct(0.25),
            "median": statistics.median(sv),
            "p75":    pct(0.75),
            "p99":    pct(0.99),
            "max":    sv[-1],
            "mean":   statistics.fmean(sv),
        }
    return s


def render_histogram_mpl(values_grid: List[float],
                         values_gmsh: List[float],
                         field_name: str) -> str:
    fig, ax = plt.subplots(figsize=(6, 3), dpi=120)
    bins = 30
    if values_grid:
        ax.hist(values_grid, bins=bins, alpha=0.6, label="grid", color="#1D9BF0")
    if values_gmsh:
        ax.hist(values_gmsh, bins=bins, alpha=0.6, label="gmsh", color="#00BA7C")
    ax.set_title(field_name)
    ax.set_xlabel(field_name)
    ax.set_ylabel("count")
    ax.legend()
    ax.grid(True, axis="y", alpha=0.3)
    buf = io.BytesIO()
    fig.savefig(buf, format="png", bbox_inches="tight")
    plt.close(fig)
    return "data:image/png;base64," + base64.b64encode(buf.getvalue()).decode("ascii")


def render_histogram_ascii(values_grid: List[float],
                           values_gmsh: List[float]) -> str:
    """Fallback for environments without matplotlib. 10-bin ASCII bars."""
    if not values_grid and not values_gmsh:
        return "_(empty)_"
    all_v = (values_grid or []) + (values_gmsh or [])
    lo, hi = min(all_v), max(all_v)
    if hi - lo < 1e-12:
        return f"_(degenerate — all values ~= {lo:.3g})_"
    nbins = 10
    width = (hi - lo) / nbins
    def bucketise(vs: List[float]) -> List[int]:
        b = [0] * nbins
        for v in vs:
            idx = min(nbins - 1, max(0, int((v - lo) / width)))
            b[idx] += 1
        return b
    bg = bucketise(values_grid)
    bm = bucketise(values_gmsh)
    peak = max(max(bg, default=0), max(bm, default=0), 1)
    rows = ["bucket           grid                          gmsh"]
    for i in range(nbins):
        bound_lo = lo + i * width
        bound_hi = bound_lo + width
        bar_g = "#" * round(20 * bg[i] / peak)
        bar_m = "#" * round(20 * bm[i] / peak)
        rows.append(f"[{bound_lo:6.3f},{bound_hi:6.3f}) "
                    f"{bar_g:<20}({bg[i]:>4})  {bar_m:<20}({bm[i]:>4})")
    return "```\n" + "\n".join(rows) + "\n```"


def render_report(grid_q: Dict[str, List[float]],
                  gmsh_q: Dict[str, List[float]],
                  grid_runtime: float,
                  gmsh_runtime: float,
                  grid_ok: bool,
                  gmsh_ok: bool) -> str:
    s_grid = summarise(grid_q)
    s_gmsh = summarise(gmsh_q)
    out = []
    out.append("# Mesh comparison: grid vs gmsh\n")
    out.append("_Auto-generated by `examples/mesh-comparison/compare.py`._\n")
    out.append("## Caveats\n")
    out.append("* Same `target_size` does *not* mean same cell count — Gmsh\n"
               "  interprets it as a feature-aware characteristic length.\n"
               "* Quality metric definitions are souxmar's convention\n"
               "  (Jacobian normalised to [0, 1], etc.) — numbers diverge\n"
               "  from Gmsh GUI's native reporting.\n"
               "* Gmsh runtime depends on `OMP_NUM_THREADS`; set it before\n"
               "  re-running for a parallelism-aware comparison.\n")

    out.append("## Summary\n")
    out.append("| metric                | grid | gmsh |")
    out.append("| --------------------- | ---- | ---- |")
    out.append(f"| Pipeline succeeded?   | {'✓' if grid_ok else '✗'} | {'✓' if gmsh_ok else '✗'} |")
    out.append(f"| Cell count            | {cell_count(grid_q)} | {cell_count(gmsh_q)} |")
    out.append(f"| Wall-clock (s)        | {grid_runtime:.2f} | {gmsh_runtime:.2f} |")
    for f in _QUALITY_FIELDS:
        g = s_grid.get(f, {}).get("p01")
        m = s_gmsh.get(f, {}).get("p01")
        out.append(f"| {f} (1st percentile) | {g if g is not None else '—'} | {m if m is not None else '—'} |")
    out.append("")

    out.append("## Per-field detail\n")
    for f in _QUALITY_FIELDS:
        out.append(f"### {f}\n")
        if not grid_q.get(f) and not gmsh_q.get(f):
            out.append("_(no data)_\n")
            continue
        if _HAS_MPL:
            data_uri = render_histogram_mpl(
                grid_q.get(f, []), gmsh_q.get(f, []), f)
            out.append(f"![{f} histogram]({data_uri})\n")
        else:
            out.append(render_histogram_ascii(
                grid_q.get(f, []), gmsh_q.get(f, [])) + "\n")

    return "\n".join(out) + "\n"


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--souxmar", type=Path, required=True,
                   help="path to the built souxmar CLI binary")
    p.add_argument("--plugin-dir", type=Path, required=True,
                   help="path to the built examples/plugins directory")
    p.add_argument("--out", type=Path, default=HERE / "report.md")
    args = p.parse_args()

    if not args.souxmar.exists():
        print(f"error: souxmar binary not found at {args.souxmar}", file=sys.stderr)
        return 2
    if not args.plugin_dir.is_dir():
        print(f"error: --plugin-dir not a directory: {args.plugin_dir}", file=sys.stderr)
        return 2

    print(f"[mesh-comparison] running grid mesher...")
    grid_ok, grid_t, grid_log = run_pipeline(
        args.souxmar, args.plugin_dir, HERE / "pipeline-grid.yaml")
    print(f"  ok={grid_ok}  elapsed={grid_t:.2f}s")
    if not grid_ok:
        print("--- grid output ---")
        print(grid_log)

    print(f"[mesh-comparison] running gmsh mesher...")
    gmsh_ok, gmsh_t, gmsh_log = run_pipeline(
        args.souxmar, args.plugin_dir, HERE / "pipeline-gmsh.yaml")
    print(f"  ok={gmsh_ok}  elapsed={gmsh_t:.2f}s")
    if not gmsh_ok:
        print("--- gmsh output ---")
        print(gmsh_log)

    grid_q = parse_vtu_quality(HERE / "cube_grid.vtu")
    gmsh_q = parse_vtu_quality(HERE / "cube_gmsh.vtu")

    (HERE / "metrics-grid.json").write_text(json.dumps(grid_q, indent=2))
    (HERE / "metrics-gmsh.json").write_text(json.dumps(gmsh_q, indent=2))

    report = render_report(grid_q, gmsh_q, grid_t, gmsh_t, grid_ok, gmsh_ok)
    args.out.write_text(report)
    print(f"[mesh-comparison] wrote {args.out}")

    return 0 if (grid_ok and gmsh_ok) else 1


if __name__ == "__main__":
    sys.exit(main())
