#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Regenerate the figures in docs/PHYSICS.md, plus the repository's social card.

Every curve here is computed from the same closed forms the C++ plugins
implement, at the plugins' own default inputs, by an independent
implementation. The numbers printed by --verify are the hand-checks written
into examples/plugins/am-thermal/am_thermal.cpp and
examples/plugins/marine/marine_loads.cpp; if this script and those headers ever
disagree, one of them is wrong.

    python3 scripts/gen-physics-figures.py            # figures + social card
    python3 scripts/gen-physics-figures.py --verify   # just the hand-checks

Requires matplotlib and numpy. Not wired into CI: the outputs are committed.
"""

from __future__ import annotations

import argparse
import math
import pathlib
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.patches import Rectangle

OUT = pathlib.Path(__file__).resolve().parent.parent / "docs" / "img"

# Dim palette, matching the desktop app's tokens.
BG, FG, MUTED = "#15202b", "#f7f9f9", "#8b98a5"
ACCENT, WARN, GOOD = "#1d9bf0", "#f4212e", "#00ba7c"

plt.rcParams.update({
    "figure.facecolor": BG, "axes.facecolor": BG, "savefig.facecolor": BG,
    "text.color": FG, "axes.labelcolor": FG, "axes.edgecolor": MUTED,
    "xtick.color": MUTED, "ytick.color": MUTED, "grid.color": "#38444d",
    "font.family": "sans-serif", "font.size": 9, "axes.titlesize": 10,
    "axes.grid": True, "grid.alpha": 0.35, "axes.spines.top": False,
    "axes.spines.right": False, "legend.frameon": False,
    "legend.labelcolor": FG,
})

# ---------------------------------------------------------------------------
# am-thermal defaults (examples/plugins/am-thermal/am_thermal.cpp)
# ---------------------------------------------------------------------------
LASER_W, ABSORB = 200.0, 0.35
SPEED = 0.8
COND, DENS, CP = 15.0, 7990.0, 500.0
T_BASE, T_MELT = 80.0, 1400.0
BEAM_R, HATCH, LAYER = 4.0e-5, 1.1e-4, 3.0e-5
ALPHA = COND / (DENS * CP)


def rosenthal(w, y, z, q=LASER_W * ABSORB, v=SPEED, k=COND, a=ALPHA, t0=T_BASE):
    """Quasi-steady rise around a point source on a half-space surface."""
    R = np.sqrt(w * w + y * y + z * z)
    R = np.where(R < 1e-9, 1e-9, R)
    return t0 + q / (2 * math.pi * k * R) * np.exp(-v * (R + w) / (2 * a))


def melt_depth(q=LASER_W * ABSORB, v=SPEED, k=COND, a=ALPHA, dT=T_MELT - T_BASE):
    """Fixed 100-step bisection, as the plugin does — no tolerance exit."""
    Rc = q / (2 * math.pi * k * dT)
    lo, hi = 1e-12, Rc
    for _ in range(100):
        mid = 0.5 * (lo + hi)
        if q / (2 * math.pi * k * mid) * math.exp(-v * mid / (2 * a)) - dT > 0:
            lo = mid
        else:
            hi = mid
    return 0.5 * (lo + hi)


def melt_width(q=LASER_W * ABSORB, v=SPEED, k=COND, a=ALPHA, dT=T_MELT - T_BASE):
    """1024-point scan then 100 ternary refinements, as the plugin does."""
    Rc = q / (2 * math.pi * k * dT)

    def y_of(R):
        w = -R - (2 * a / v) * math.log(R / Rc)
        y2 = R * R - w * w
        return math.sqrt(y2) if y2 > 0 else 0.0

    grid = [Rc * (i + 0.5) / 1024 for i in range(1024)]
    best = max(grid, key=y_of)
    lo, hi = best - Rc / 1024, best + Rc / 1024
    for _ in range(100):
        a1, a2 = lo + (hi - lo) / 3, hi - (hi - lo) / 3
        if y_of(a1) < y_of(a2):
            lo = a1
        else:
            hi = a2
    return 2 * y_of(0.5 * (lo + hi))


def norm_enthalpy(P=LASER_W, v=SPEED, A=ABSORB, sigma=BEAM_R):
    hs = DENS * CP * (T_MELT + 273.15)
    return A * P / (hs * math.sqrt(math.pi * ALPHA * v * sigma ** 3))


# ---------------------------------------------------------------------------
# marine defaults (examples/plugins/marine/marine_loads.cpp)
# ---------------------------------------------------------------------------
E_STEEL, NU, SIG_Y = 190e9, 0.28, 500e6
KN_IMP, KN_AM = 0.75, 0.90


def p_windenburg_trilling(t, D, L, E=E_STEEL, nu=NU):
    """Returns None when the geometry leaves the bracket — dropped, not clamped."""
    bracket = L / D - 0.45 * math.sqrt(t / D)
    if bracket <= 0:
        return None
    return 2.42 * E * (t / D) ** 2.5 / ((1 - nu ** 2) ** 0.75 * bracket)


def p_membrane_yield(t, D, sy=SIG_Y):
    return 2 * sy * t / D


def p_long_cylinder(t, D, E=E_STEEL, nu=NU):
    return 2 * E * t ** 3 / ((1 - nu ** 2) * D ** 3)


def seawater_density(S=35.0, T=10.0):
    """EOS-80 one-atmosphere form (Millero & Poisson 1981 / UNESCO 44)."""
    rw = (999.842594 + 6.793952e-2 * T - 9.095290e-3 * T ** 2
          + 1.001685e-4 * T ** 3 - 1.120083e-6 * T ** 4 + 6.536332e-9 * T ** 5)
    A = 8.24493e-1 - 4.0899e-3 * T + 7.6438e-5 * T ** 2 - 8.2467e-7 * T ** 3 + 5.3875e-9 * T ** 4
    B = -5.72466e-3 + 1.0227e-4 * T - 1.6546e-6 * T ** 2
    return rw + A * S + B * S ** 1.5 + 4.8314e-4 * S ** 2


def pitting_risk(T, pren, stagnant=False):
    """Logistic in (T - CPT), 8 K width; CPT = 2.24*PREN - 47.1."""
    cpt = 2.24 * pren - 47.1
    if stagnant:
        cpt -= 20.0
    return 1.0 / (1.0 + np.exp(-(T - cpt) / 8.0))


# ---------------------------------------------------------------------------
# Figures
# ---------------------------------------------------------------------------
def fig_melt_pool():
    D, W = melt_depth(), melt_width()
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(10, 3.9))

    # Longitudinal section through the scan centreline (y = 0).
    w = np.linspace(-900e-6, 300e-6, 700)
    z = np.linspace(0, 110e-6, 380)
    Wg, Zg = np.meshgrid(w, z)
    T = np.clip(rosenthal(Wg, 0.0, Zg), None, 2900)
    im = ax1.contourf(Wg * 1e6, Zg * 1e6, T, levels=np.linspace(T_BASE, 2900, 40), cmap="inferno")
    ax1.contour(Wg * 1e6, Zg * 1e6, T, levels=[T_MELT], colors=[GOOD], linewidths=1.8)
    ax1.invert_yaxis()
    ax1.set_xlabel("source-frame w  [µm]   (source at 0, travelling →)")
    ax1.set_ylabel("depth z  [µm]")
    ax1.set_title("Rosenthal field on the scan centreline", color=FG)
    ax1.plot(0, D * 1e6, "o", ms=6, color=GOOD)
    ax1.annotate(f"reported depth D = {D*1e6:.1f} µm\n(the isotherm at w = 0,\ndirectly under the source)",
                 xy=(0, D * 1e6), xytext=(-560, 88), color=GOOD, fontsize=8,
                 arrowprops=dict(arrowstyle="->", color=GOOD, lw=1))
    cb = fig.colorbar(im, ax=ax1, pad=0.02)
    cb.set_label("T [°C]", color=FG)
    cb.ax.tick_params(colors=MUTED)
    cb.outline.set_edgecolor(MUTED)

    # Free-surface plan view (z = 0).
    y = np.linspace(-160e-6, 160e-6, 500)
    Wg2, Yg = np.meshgrid(w, y)
    T2 = np.clip(rosenthal(Wg2, Yg, 0.0), None, 2900)
    im2 = ax2.contourf(Wg2 * 1e6, Yg * 1e6, T2, levels=np.linspace(T_BASE, 2900, 40), cmap="inferno")
    ax2.contour(Wg2 * 1e6, Yg * 1e6, T2, levels=[T_MELT], colors=[GOOD], linewidths=1.8)
    ax2.axhline(HATCH / 2 * 1e6, color=ACCENT, ls="--", lw=1)
    ax2.axhline(-HATCH / 2 * 1e6, color=ACCENT, ls="--", lw=1)
    ax2.text(-870, HATCH / 2 * 1e6 + 6, "mid-hatch line (coldest point between tracks)",
             color=ACCENT, fontsize=7.5)
    ax2.set_xlabel("source-frame w  [µm]")
    ax2.set_ylabel("transverse y  [µm]")
    ax2.set_title(f"Free surface — width W = {W*1e6:.0f} µm", color=FG)
    cb2 = fig.colorbar(im2, ax=ax2, pad=0.02)
    cb2.set_label("T [°C]", color=FG)
    cb2.ax.tick_params(colors=MUTED)
    cb2.outline.set_edgecolor(MUTED)

    fig.suptitle(
        f"solver.am.thermal.lpbf — 316L, {LASER_W:.0f} W, {SPEED} m/s, absorptivity {ABSORB}",
        color=FG, y=1.0)
    fig.tight_layout()
    fig.savefig(OUT / "rosenthal-melt-pool.png", dpi=170, bbox_inches="tight")
    plt.close(fig)


def fig_process_map():
    powers = np.linspace(80, 400, 150)
    speeds = np.linspace(0.2, 2.0, 150)
    Pg, Vg = np.meshgrid(powers, speeds)
    lof = np.zeros_like(Pg)
    key = np.zeros_like(Pg)
    for i in range(Pg.shape[0]):
        for j in range(Pg.shape[1]):
            q, v = Pg[i, j] * ABSORB, Vg[i, j]
            D = melt_depth(q=q, v=v)
            W = melt_width(q=q, v=v)
            lof[i, j] = (HATCH / W) ** 2 + (LAYER / D) ** 2 if W > 0 and D > 0 else 99
            key[i, j] = norm_enthalpy(P=Pg[i, j], v=v)

    risk = np.maximum(np.clip((lof - 1) / 3, 0, 1), np.clip((key - 30) / 30, 0, 1))

    fig, ax = plt.subplots(figsize=(7.2, 4.4))
    im = ax.contourf(Pg, Vg, risk, levels=np.linspace(0, 1, 41), cmap="magma")
    ax.contour(Pg, Vg, lof, levels=[1.0], colors=[GOOD], linewidths=2)
    ax.contour(Pg, Vg, key, levels=[30.0], colors=[WARN], linewidths=2)
    ax.plot(LASER_W, SPEED, "o", ms=9, mfc="none", mec=ACCENT, mew=2)
    ax.annotate("plugin defaults\n200 W, 0.8 m/s", xy=(LASER_W, SPEED),
                xytext=(120, 1.12), color=ACCENT, fontsize=8,
                arrowprops=dict(arrowstyle="->", color=ACCENT, lw=1))
    # Anchor each label to the curve it names, at a power where the two
    # boundaries are well separated, so neither caption can be read as
    # belonging to the other line.
    def speed_at(field, level, power):
        col = np.argmin(np.abs(powers - power))
        series = field[:, col]
        idx = np.argmin(np.abs(series - level))
        return speeds[idx]

    ax.annotate("Tang et al. (2017)\nlack-of-fusion boundary\n(above the line, fusion is incomplete)",
                xy=(140, speed_at(lof, 1.0, 140)), xytext=(150, 1.30),
                color=GOOD, fontsize=8,
                arrowprops=dict(arrowstyle="->", color=GOOD, lw=1))
    ax.annotate("King et al. (2014)\nkeyhole onset ΔH/hₛ = 30\n(below the line, the pool goes into keyhole mode)",
                xy=(330, speed_at(key, 30.0, 330)), xytext=(396, 0.95),
                color=WARN, fontsize=8, ha="right",
                arrowprops=dict(arrowstyle="->", color=WARN, lw=1))
    ax.text(255, 0.44, "process window", color=FG, fontsize=9.5, alpha=0.9)
    ax.set_xlabel("laser power  [W]")
    ax.set_ylabel("scan speed  [m/s]")
    ax.set_title("postproc.am.melt_pool — porosity risk = max(lack-of-fusion, keyhole)", color=FG)
    cb = fig.colorbar(im, ax=ax, pad=0.02)
    cb.set_label("porosity risk  [0–1]", color=FG)
    cb.ax.tick_params(colors=MUTED)
    cb.outline.set_edgecolor(MUTED)
    fig.text(0.5, -0.05,
             "Rosenthal under-predicts LPBF melt-pool depth by 1.5–2×, so this map "
             "over-predicts lack-of-fusion.\nCalibrate absorptivity against one measured "
             "single-track cross-section before trusting it.",
             ha="center", color=MUTED, fontsize=7.5)
    fig.tight_layout()
    fig.savefig(OUT / "lpbf-process-map.png", dpi=170, bbox_inches="tight")
    plt.close(fig)


def fig_hull_collapse():
    D, L = 1.0, 0.5
    rho = seawater_density()

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(11.2, 4.4))

    # -- left: which mode governs the DEFAULT ring-stiffened hull ----------
    # hull_type defaults to ring_stiffened_cylinder, for which the plugin
    # evaluates modes 0 and 1 only. Mode 2 (long cylinder) is the collapse of
    # a hull whose frames contribute nothing, so it is evaluated for
    # hull_type: cylinder and is NOT part of this governing minimum.
    ts = np.linspace(2e-3, 40e-3, 400)
    wt, yld, lng = [], [], []
    for t_ in ts:
        p_ = p_windenburg_trilling(t_, D, L)
        wt.append(np.nan if p_ is None else p_ * KN_IMP * KN_AM / 1e6)
        yld.append(p_membrane_yield(t_, D) * KN_AM / 1e6)
        lng.append(p_long_cylinder(t_, D) * KN_IMP * KN_AM / 1e6)
    gov = np.nanmin(np.vstack([wt, yld]), axis=0)

    ax1.fill_between(ts * 1e3, 0, gov, color=ACCENT, alpha=0.13)
    ax1.plot(ts * 1e3, wt, color=ACCENT, lw=2, label="mode 0 — interframe shell (Windenburg–Trilling)")
    ax1.plot(ts * 1e3, yld, color=WARN, lw=2, label="mode 1 — membrane yield  p = 2σᵧt/D")
    ax1.plot(ts * 1e3, gov, color=FG, lw=1.4, ls="--", label="governing (minimum after knockdown)")
    ax1.plot(ts * 1e3, lng, color=MUTED, lw=1.3, ls=":",
             label="mode 2 — long cylinder (unstiffened hulls only,\nnot in this minimum)")

    for depth, style in ((300, "-"), (1000, ":")):
        pd_ = rho * 9.80665 * depth / 1e6
        ax1.axhline(pd_, color=GOOD, lw=0.9, ls=style, alpha=0.7)
        ax1.text(39.5, pd_ + 0.5, f"{depth} m design pressure", color=GOOD, fontsize=7.5, ha="right")

    ax1.plot(12, 10.8, "o", ms=9, mfc="none", mec=FG, mew=2)
    ax1.annotate("defaults t = 12 mm:\nyield governs, 10.80 MPa", xy=(12, 10.8),
                 xytext=(15, 3.2), color=FG, fontsize=8,
                 arrowprops=dict(arrowstyle="->", color=FG, lw=1))
    ax1.set_xlabel("shell thickness t  [mm]")
    ax1.set_ylabel("collapse pressure after knockdown  [MPa]")
    ax1.set_title(f"Ring-stiffened cylinder, D = {D:.1f} m, L = {L*1e3:.0f} mm, 316L", color=FG)
    ax1.set_ylim(0, 30)
    ax1.set_xlim(2, 40)
    ax1.legend(loc="upper left", fontsize=7.5, bbox_to_anchor=(0.0, 1.0))
    ax1.set_facecolor(BG)

    # -- right: the mode is DROPPED, not clamped, outside its bracket ------
    t_fixed = 0.012
    lods = np.linspace(0.02, 1.2, 600)
    valid, naive = [], []
    for lod in lods:
        p_ = p_windenburg_trilling(t_fixed, D, lod * D)
        valid.append(np.nan if p_ is None else p_ * KN_IMP * KN_AM / 1e6)
        bracket = lod - 0.45 * math.sqrt(t_fixed / D)
        naive.append(2.42 * E_STEEL * (t_fixed / D) ** 2.5
                     / ((1 - NU ** 2) ** 0.75 * bracket) * KN_IMP * KN_AM / 1e6
                     if bracket != 0 else np.nan)

    lim = 0.45 * math.sqrt(t_fixed / D)
    ax2.axvspan(0.0, lim, color=WARN, alpha=0.16)
    ax2.plot(lods, valid, color=ACCENT, lw=2.2, label="what the plugin reports")
    ax2.axvline(lim, color=WARN, lw=1.6)
    ax2.axhline(0, color=MUTED, lw=0.8)
    naive_at = naive[0]
    ax2.annotate(
        f"L/D < 0.45·√(t/D) = {lim:.3f}\n\n"
        f"Here the bracket L/D − 0.45·√(t/D) turns negative and the\n"
        f"formula returns a negative collapse pressure ({naive_at:.0f} MPa at\n"
        f"L/D = 0.02). The plugin checks the bracket before dividing\n"
        f"and DROPS the mode. It does not clamp it to zero, and it\n"
        f"does not report the absolute value — a mode outside its\n"
        f"derivation has no answer to give.",
        xy=(lim, 30), xytext=(0.30, 30), color=WARN, fontsize=8, va="center",
        arrowprops=dict(arrowstyle="->", color=WARN, lw=1.2))
    ax2.plot(L / D, p_windenburg_trilling(t_fixed, D, L) * KN_IMP * KN_AM / 1e6,
             "o", ms=8, mfc="none", mec=FG, mew=2)
    ax2.text(L / D + 0.035, 11.5, "defaults\nL/D = 0.5", color=FG, fontsize=8)
    ax2.set_xlabel("frame spacing ratio  L/D          (t = 12 mm)")
    ax2.set_ylabel("mode-0 collapse pressure  [MPa]")
    ax2.set_title("Outside its validity bracket, the mode is dropped", color=FG)
    ax2.set_ylim(-5, 60)
    ax2.set_xlim(0.0, 1.2)
    ax2.legend(loc="lower right", fontsize=8)

    fig.suptitle("solver.marine.hull_collapse — which mode governs, and where it stops being valid",
                 color=FG, y=1.02)
    fig.text(0.5, -0.05,
             "Preliminary sizing arithmetic, not a classification-society calculation. General instability of a "
             "ring-stiffened hull needs the frame\narea, the frame second moment of inertia and the bulkhead "
             "spacing; the input contract supplies none of them, it is frequently the governing\nmode for a real "
             "framed hull, and it is not evaluated here at all.",
             ha="center", color=MUTED, fontsize=7.5)
    fig.tight_layout()
    fig.savefig(OUT / "hull-collapse-modes.png", dpi=170, bbox_inches="tight")
    plt.close(fig)


def fig_corrosion():
    T = np.linspace(0, 60, 300)
    alloys = [("316L", 24.7, ACCENT), ("2205 duplex", 34.6, GOOD), ("2507 super duplex", 42.5, "#ffd400")]
    fig, ax = plt.subplots(figsize=(7.2, 4.0))
    for name, pren, colour in alloys:
        ax.plot(T, pitting_risk(T, pren), color=colour, lw=2, label=f"{name}  (PREN {pren})")
        ax.plot(T, pitting_risk(T, pren, stagnant=True), color=colour, lw=1.2, ls=":", alpha=0.8)
        cpt = 2.24 * pren - 47.1
        ax.plot(cpt, 0.5, "o", ms=6, color=colour)
    ax.axhline(0.5, color=MUTED, lw=0.8, ls="--")
    ax.text(1, 0.53, "reads 0.5 exactly at the critical pitting temperature", color=MUTED, fontsize=7.5)
    ax.text(59, 0.055, "dotted = stagnant water\n(CPT shifted toward the crevice temperature, −20 K)",
            color=MUTED, fontsize=7.5, ha="right")
    ax.set_xlabel("seawater temperature  [°C]")
    ax.set_ylabel("pitting_risk  [0–1]")
    ax.set_title("solver.marine.corrosion — PREN = %Cr + 3.3%Mo + 16%N,  CPT = 2.24·PREN − 47.1", color=FG)
    ax.set_ylim(0, 1.02)
    ax.set_xlim(0, 60)
    ax.legend(loc="upper left", bbox_to_anchor=(0.42, 0.42), fontsize=8)
    fig.text(0.5, -0.05,
             "A scaled indicator for comparing alloys, not a probability and not design data. "
             "The CPT–PREN regression is fitted\nto ASTM G48 (6 % FeCl₃) data; real service "
             "adds crevices, deposits, biofilm and weld metallurgy.",
             ha="center", color=MUTED, fontsize=7.5)
    fig.tight_layout()
    fig.savefig(OUT / "corrosion-pren.png", dpi=170, bbox_inches="tight")
    plt.close(fig)


def fig_social_card():
    """1280x640 card for the repository's social preview."""
    fig = plt.figure(figsize=(12.8, 6.4), dpi=100)
    fig.patch.set_facecolor(BG)

    # Plan view of the free surface: the elongated teardrop with its long
    # trailing thermal tail is what a melt pool actually looks like, and it
    # fits a wide panel without distorting the aspect ratio.
    ax = fig.add_axes([0.50, 0.0, 0.50, 1.0])
    w = np.linspace(-700e-6, 150e-6, 900)
    y = np.linspace(-215e-6, 215e-6, 460)
    Wg, Yg = np.meshgrid(w, y)
    T = np.clip(rosenthal(Wg, Yg, 0.0), None, 2900)
    ax.contourf(Wg * 1e6, Yg * 1e6, T, levels=np.linspace(T_BASE, 2900, 60), cmap="inferno")
    ax.contour(Wg * 1e6, Yg * 1e6, T, levels=[T_MELT], colors=[GOOD], linewidths=2.2)
    ax.set_xticks([])
    ax.set_yticks([])
    ax.grid(False)
    for s in ax.spines.values():
        s.set_visible(False)
    ax.text(0.5, 0.045, "LPBF melt pool  ·  Rosenthal (1946)  ·  316L, 200 W, 0.8 m/s",
            transform=ax.transAxes, ha="center", color="#ffffff", fontsize=10.5, alpha=0.9)

    fig.text(0.055, 0.735, "souxmar", color=FG, fontsize=62, fontweight="bold", va="center")
    fig.patches.append(Rectangle((0.056, 0.632), 0.062, 0.008,
                                 transform=fig.transFigure, facecolor=ACCENT, zorder=5))
    fig.text(0.056, 0.565,
             "An open-source CAE platform: parametric CAD,\n"
             "meshing, FEM and CFD, post-processing — with an\n"
             "agentic AI that can drive the whole pipeline.",
             color=FG, fontsize=15, va="top", linespacing=1.6)
    fig.text(0.056, 0.245,
             "C++20 core  ·  stable C plugin ABI  ·  25 in-tree plugins\n"
             "Tauri + React desktop app  ·  Apache-2.0",
             color=MUTED, fontsize=12.5, va="top", linespacing=1.7)
    fig.savefig(OUT / "social-preview.png", facecolor=BG)
    plt.close(fig)


# ---------------------------------------------------------------------------
def verify() -> int:
    """Reproduce the hand-checks written into the plugin headers."""
    D, W = melt_depth(), melt_width()
    cool = 2 * math.pi * COND * SPEED * (T_MELT - T_BASE) ** 2 / (LASER_W * ABSORB)
    lof = (HATCH / W) ** 2 + (LAYER / D) ** 2
    phi = math.erf(1.0e-3 / (2 * math.sqrt(ALPHA * 10.0)))
    rho = seawater_density()
    p_wt = p_windenburg_trilling(0.012, 1.0, 0.5)
    p_y = p_membrane_yield(0.012, 1.0)
    gov = min(p_wt * KN_IMP * KN_AM, p_y * KN_AM)
    p_des = 1025 * 9.80665 * 300

    cases = [
        ("am-thermal  cooling rate [K/s]",      cool,                   1.9e6,  0.05),
        ("am-thermal  ΔH/hₛ @200 W 0.8 m/s",    norm_enthalpy(),        13.5,   0.02),
        ("am-thermal  ΔH/hₛ @400 W 0.5 m/s",    norm_enthalpy(400, 0.5), 34.0,  0.02),
        ("am-thermal  I_lof",                   lof,                    1.93,   0.02),
        ("am-thermal  φ(interlayer)",           phi,                    0.092,  0.02),
        ("marine      ρ(35 PSU, 10 °C)",        rho,                    1026.95, 1e-4),
        ("marine      p_wt unfactored [MPa]",   p_wt / 1e6,             17.11,  0.01),
        ("marine      p_y  unfactored [MPa]",   p_y / 1e6,              12.00,  0.01),
        ("marine      governing [MPa]",         gov / 1e6,              10.80,  0.01),
        ("marine      margin vs 1.5×p_design",  gov / (p_des * 1.5),    2.39,   0.01),
    ]
    bad = 0
    for label, got, want, tol in cases:
        ok = abs(got - want) <= tol * abs(want)
        bad += not ok
        print(f"  {'ok  ' if ok else 'FAIL'} {label:38s} {got:12.4g}  header says {want:g}")
    print(f"\n{len(cases) - bad}/{len(cases)} header hand-checks reproduced.")
    return 1 if bad else 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--verify", action="store_true",
                    help="only reproduce the plugin headers' hand-checks")
    args = ap.parse_args()

    rc = verify()
    if args.verify:
        return rc

    OUT.mkdir(parents=True, exist_ok=True)
    for fn in (fig_melt_pool, fig_process_map, fig_hull_collapse, fig_corrosion, fig_social_card):
        fn()
        print(f"  wrote a figure from {fn.__name__}")
    print(f"\nfigures in {OUT}")
    return rc


if __name__ == "__main__":
    sys.exit(main())
