#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generate `propeller-blade.obj` — one blade of a 0.6 m diameter NAB propeller.

Deterministic, Python-3-stdlib-only, no numpy. Re-running it must reproduce
the committed `propeller-blade.obj` byte-for-byte; the repo's determinism gate
applies to fixtures as well as to solver output.

    python3 generate-geometry.py            # writes propeller-blade.obj here
    python3 generate-geometry.py --check    # verify only, do not write

What it builds
--------------
A single skewed, twisted, cambered blade lofted through 21 radial sections,
wrapped onto the cylinders they actually live on (a propeller section is a
curve on a cylinder of radius r, not a flat rib), closed with a root cap and a
tip cap so the surface is a closed 2-manifold.

Coordinate frame (metres, right-handed):

    +X   shaft axis, pointing forward (the thrust direction)
    +Z   the blade reference line at the root — the blade grows outward in +Z
    +Y   tangential; skew displaces the sections toward +Y

so the part sits root-down for a +Z build direction, which is how you would
actually deposit it: root on the plate, tip last.

Blade definition (a B-series-flavoured wide-bladed workboat propeller):

    D          0.600 m     diameter
    r/R root   0.30        blade starts at the hub fillet
    P/D        0.90        constant-pitch reference, so twist = atan(P/2*pi*r)
    skew       22 deg      cumulative, at the tip
    rake       12 mm       aft, linear in r
    chord      tabulated c/D, peak 0.255 at r/R = 0.6..0.7
    thickness  tabulated t/c, NACA-4-digit closed-trailing-edge form
    camber     tabulated f/c, parabolic mean line

Every one of those numbers is a plausible round number for a propeller of this
size, not a hydrodynamic design. It is geometry for a manufacturing-simulation
demo: the section shapes are aerofoil-like so that distortion, overhang and
printability produce interesting fields, but nothing here was lifted from a
real open-water series and no thrust/torque claim is implied.
"""

from __future__ import annotations

import math
import sys

# --------------------------------------------------------------------------
# Blade parameters. All lengths in metres, all angles in degrees.
# --------------------------------------------------------------------------
DIAMETER = 0.600
R_TIP = DIAMETER / 2.0
R_ROOT_FRACTION = 0.30
PITCH_RATIO = 0.90            # P/D
SKEW_TIP_DEG = 22.0
RAKE_TIP = -0.012             # aft, i.e. -X
N_RADIAL = 21                 # radial loft stations
N_CHORD = 17                  # chordwise points per side (cosine spaced)

# Radial distributions, tabulated at r/R and linearly interpolated onto the
# 21 loft stations. Kept as tables because that is how a real propeller
# drawing carries them, and because a table is auditable.
#            r/R     c/D     t/c     f/c
SECTIONS = [
    (0.300, 0.180, 0.180, 0.012),
    (0.400, 0.215, 0.150, 0.018),
    (0.500, 0.240, 0.125, 0.022),
    (0.600, 0.255, 0.105, 0.024),
    (0.700, 0.255, 0.088, 0.024),
    (0.800, 0.235, 0.075, 0.022),
    (0.900, 0.180, 0.065, 0.018),
    (0.950, 0.135, 0.070, 0.014),
    (1.000, 0.045, 0.090, 0.006),
]

OBJ_PATH = "propeller-blade.obj"


# --------------------------------------------------------------------------
# Small geometry helpers.
# --------------------------------------------------------------------------
def lerp_table(table: list[tuple[float, ...]], x: float, col: int) -> float:
    """Linear interpolation of column `col` of `table` at abscissa `x`."""
    if x <= table[0][0]:
        return table[0][col]
    if x >= table[-1][0]:
        return table[-1][col]
    for i in range(len(table) - 1):
        x0, x1 = table[i][0], table[i + 1][0]
        if x0 <= x <= x1:
            f = (x - x0) / (x1 - x0)
            return table[i][col] + f * (table[i + 1][col] - table[i][col])
    return table[-1][col]


def naca_half_thickness(u: float, t_over_c: float) -> float:
    """NACA 4-digit thickness form, closed-trailing-edge variant.

    y_t/c = 5t(0.2969*sqrt(u) - 0.1260u - 0.3516u^2 + 0.2843u^3 - 0.1036u^4)

    The -0.1036 last coefficient (rather than -0.1015) closes the trailing
    edge exactly, which we need for a watertight loft.
    """
    u = min(max(u, 0.0), 1.0)
    return 5.0 * t_over_c * (
        0.2969 * math.sqrt(u)
        - 0.1260 * u
        - 0.3516 * u * u
        + 0.2843 * u * u * u
        - 0.1036 * u * u * u * u
    )


def parabolic_camber(u: float, f_over_c: float) -> float:
    """Parabolic mean line, max camber f at mid-chord."""
    return 4.0 * f_over_c * u * (1.0 - u)


def cosine_spacing(i: int, n: int) -> float:
    """Chordwise station i of n, clustered at the leading and trailing edge."""
    return 0.5 * (1.0 - math.cos(math.pi * i / (n - 1)))


# --------------------------------------------------------------------------
# Blade construction.
# --------------------------------------------------------------------------
def build_blade() -> tuple[list[tuple[float, float, float]], list[tuple[list[int], str]]]:
    """Return (vertices, faces). Faces are (index_list, group_name)."""
    r_root = R_ROOT_FRACTION * R_TIP
    pitch = PITCH_RATIO * DIAMETER
    loop_len = 2 * N_CHORD - 2          # unique points around a closed section

    verts: list[tuple[float, float, float]] = []
    for k in range(N_RADIAL):
        span = k / (N_RADIAL - 1)                       # 0 at root, 1 at tip
        r = r_root + span * (R_TIP - r_root)
        r_over_R = r / R_TIP

        chord = lerp_table(SECTIONS, r_over_R, 1) * DIAMETER
        t_over_c = lerp_table(SECTIONS, r_over_R, 2)
        f_over_c = lerp_table(SECTIONS, r_over_R, 3)

        # Twist from the constant-pitch reference: phi = atan(P / (2*pi*r)).
        phi = math.atan2(pitch, 2.0 * math.pi * r)
        sin_phi, cos_phi = math.sin(phi), math.cos(phi)

        # Skew as a cumulative angular displacement about the shaft axis,
        # converted to arc length at this radius. Rake is a pure -X shift.
        skew_angle = math.radians(SKEW_TIP_DEG) * (span ** 1.8)
        arc_skew = r * skew_angle
        x_rake = RAKE_TIP * span

        # Closed section polyline: leading edge -> back (suction) -> trailing
        # edge -> face (pressure) -> back to the leading edge.
        section: list[tuple[float, float]] = []          # (chordwise s, normal n)
        for i in range(N_CHORD):                         # back / suction side
            u = cosine_spacing(i, N_CHORD)
            n = parabolic_camber(u, f_over_c) + naca_half_thickness(u, t_over_c)
            section.append(((u - 0.5) * chord, n * chord))
        for i in range(N_CHORD - 2, 0, -1):              # face / pressure side
            u = cosine_spacing(i, N_CHORD)
            n = parabolic_camber(u, f_over_c) - naca_half_thickness(u, t_over_c)
            section.append(((u - 0.5) * chord, n * chord))
        assert len(section) == loop_len

        for s, n in section:
            # Chord line lies at pitch angle phi; the section normal is
            # perpendicular to it in the (axial, tangential) plane.
            axial = x_rake + s * sin_phi + n * cos_phi
            arc = arc_skew + s * cos_phi - n * sin_phi
            theta = arc / r                              # wrap onto radius r
            verts.append((axial, r * math.sin(theta), r * math.cos(theta)))

    def vid(station: int, j: int) -> int:
        return station * loop_len + (j % loop_len)

    faces: list[tuple[list[int], str]] = []

    # Lofted quads. Loop edges 0..N_CHORD-2 lie on the suction side ("back"),
    # the rest on the pressure side ("face") — propeller nomenclature. Faces
    # are emitted grouped by side so the file carries one `usemtl` per group
    # rather than one per loft station.
    for group, j_range in (
        ("blade_back", range(0, N_CHORD - 1)),
        ("blade_face", range(N_CHORD - 1, loop_len)),
    ):
        for k in range(N_RADIAL - 1):
            for j in j_range:
                faces.append((
                    [vid(k, j), vid(k, j + 1), vid(k + 1, j + 1), vid(k + 1, j)],
                    group,
                ))

    # Root and tip caps: triangle fans over the (non-planar, helically
    # pitched) closed section polylines. Topologically closed, which is all
    # the watertightness check needs.
    for j in range(1, loop_len - 1):
        faces.append(([vid(0, 0), vid(0, j + 1), vid(0, j)], "blade_root"))
    last = N_RADIAL - 1
    for j in range(1, loop_len - 1):
        faces.append(([vid(last, 0), vid(last, j), vid(last, j + 1)], "blade_tip"))

    return verts, faces


# --------------------------------------------------------------------------
# Verification: index range, oriented 2-manifoldness, enclosed volume.
# --------------------------------------------------------------------------
def fan_triangulate(idx: list[int]) -> list[tuple[int, int, int]]:
    """Exactly what obj-reader does: fan from the first vertex."""
    return [(idx[0], idx[i], idx[i + 1]) for i in range(1, len(idx) - 1)]


def verify(verts, faces) -> dict[str, object]:
    n = len(verts)
    tris: list[tuple[int, int, int]] = []
    for idx, _group in faces:
        for a, b, c in fan_triangulate(idx):
            for v in (a, b, c):
                if not 0 <= v < n:
                    raise SystemExit(f"face index {v} out of range [0, {n})")
            if a == b or b == c or a == c:
                raise SystemExit(f"degenerate triangle {(a, b, c)}")
            tris.append((a, b, c))

    # Oriented manifold: every directed edge exactly once, every undirected
    # edge exactly twice.
    directed: dict[tuple[int, int], int] = {}
    for a, b, c in tris:
        for e in ((a, b), (b, c), (c, a)):
            directed[e] = directed.get(e, 0) + 1
    bad_directed = sum(1 for v in directed.values() if v != 1)
    open_edges = sum(1 for (a, b) in directed if (b, a) not in directed)

    # Enclosed volume via the divergence theorem. Positive => outward normals.
    volume = 0.0
    for a, b, c in tris:
        ax, ay, az = verts[a]
        bx, by, bz = verts[b]
        cx, cy, cz = verts[c]
        volume += (
            ax * (by * cz - bz * cy)
            - ay * (bx * cz - bz * cx)
            + az * (bx * cy - by * cx)
        ) / 6.0

    xs = [v[0] for v in verts]
    ys = [v[1] for v in verts]
    zs = [v[2] for v in verts]
    radii = [math.hypot(v[1], v[2]) for v in verts]
    return {
        "vertices": n,
        "faces": len(faces),
        "triangles": len(tris),
        "bad_directed_edges": bad_directed,
        "open_edges": open_edges,
        "volume_m3": volume,
        "bbox": ((min(xs), min(ys), min(zs)), (max(xs), max(ys), max(zs))),
        "radius_range": (min(radii), max(radii)),
    }


# --------------------------------------------------------------------------
# OBJ emission.
# --------------------------------------------------------------------------
def fmt(x: float) -> str:
    """Fixed 6-decimal formatting; collapse -0.000000 to 0.000000."""
    s = f"{x:.6f}"
    return "0.000000" if s == "-0.000000" else s


def write_obj(path: str, verts, faces, stats) -> None:
    (xlo, ylo, zlo), (xhi, yhi, zhi) = stats["bbox"]
    lines = [
        "# propeller-blade.obj — one blade of a 0.6 m diameter NAB propeller.",
        "#",
        "# GENERATED FILE. Do not hand-edit: regenerate with",
        "#   python3 examples/am-marine-propeller/generate-geometry.py",
        "# which is deterministic and reproduces this file byte-for-byte.",
        "#",
        "# Frame (metres): +X shaft axis / thrust, +Z blade reference line at",
        "# the root (the blade grows outward in +Z), +Y tangential (skew).",
        "#",
        f"# Diameter {DIAMETER:.3f} m, root at r/R = {R_ROOT_FRACTION:.2f}, "
        f"P/D = {PITCH_RATIO:.2f}, skew {SKEW_TIP_DEG:.0f} deg,",
        f"# rake {abs(RAKE_TIP) * 1000.0:.0f} mm aft, {N_RADIAL} radial sections "
        f"x {2 * N_CHORD - 2} section points.",
        "#",
        f"# {stats['vertices']} vertices, {stats['faces']} faces "
        f"({stats['triangles']} Tri3 cells after obj-reader fan-triangulation).",
        f"# Enclosed volume {stats['volume_m3'] * 1e6:.1f} cm^3, "
        f"radius range {stats['radius_range'][0]:.4f}..{stats['radius_range'][1]:.4f} m.",
        f"# Bounding box ({xlo:.4f}, {ylo:.4f}, {zlo:.4f}) .. "
        f"({xhi:.4f}, {yhi:.4f}, {zhi:.4f}) m.",
        "#",
        "# Surfaces are named with `g` groups, which obj-reader ignores:",
        "#   blade_back  — suction side",
        "#   blade_face  — pressure side",
        "#   blade_root  — hub interface (the printed part's first layer)",
        "#   blade_tip   — tip cap",
        "#",
        "# They are deliberately NOT `usemtl` groups. obj-reader maps usemtl",
        "# names to per-cell tags, and every AM capability reads a non-negative",
        "# cell tag as a LAYER INDEX (contract § 2.2 rule 1) — a usemtl-tagged",
        "# OBJ would be simulated as a four-layer part with the suction side in",
        "# layer 1. Untagged cells (tag -1) send every AM capability down its",
        "# documented fallback: bin the cell centroid along build_direction by",
        "# layer_height, which is what this example wants.",
        "#",
        "# Faces are quads wound CCW seen from outside, so normals point out",
        "# of the solid (the enclosed volume above is positive).",
        "",
    ]
    for x, y, z in verts:
        lines.append(f"v {fmt(x)} {fmt(y)} {fmt(z)}")
    lines.append("")

    current = None
    for idx, group in faces:
        if group != current:
            lines.append("")
            lines.append(f"g {group}")
            current = group
        lines.append("f " + " ".join(str(i + 1) for i in idx))
    lines.append("")

    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(lines))


def main(argv: list[str]) -> int:
    verts, faces = build_blade()
    stats = verify(verts, faces)
    if stats["bad_directed_edges"] or stats["open_edges"]:
        raise SystemExit(
            f"surface is not an oriented 2-manifold: "
            f"{stats['bad_directed_edges']} repeated directed edges, "
            f"{stats['open_edges']} open edges"
        )
    if stats["volume_m3"] <= 0.0:
        raise SystemExit("enclosed volume is not positive — winding is inverted")

    print(f"vertices            {stats['vertices']}")
    print(f"faces (OBJ f lines) {stats['faces']}")
    print(f"Tri3 cells          {stats['triangles']}")
    print(f"open edges          {stats['open_edges']}")
    print(f"volume              {stats['volume_m3'] * 1e6:.2f} cm^3")
    print(f"mass @ 7600 kg/m^3  {stats['volume_m3'] * 7600.0:.3f} kg")
    print(f"bbox min            {stats['bbox'][0]}")
    print(f"bbox max            {stats['bbox'][1]}")
    print(f"radius range        {stats['radius_range']}")

    if "--check" in argv:
        print("check only; not writing")
        return 0
    write_obj(OBJ_PATH, verts, faces, stats)
    print(f"wrote {OBJ_PATH}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
