#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generate `fairing.obj` — the nose fairing shell of a 0.2 m diameter AUV.

Deterministic, Python-3-stdlib-only, no numpy. Re-running it reproduces the
committed `fairing.obj` byte-for-byte.

    python3 generate-geometry.py            # writes fairing.obj here
    python3 generate-geometry.py --check    # verify only, do not write

What it builds
--------------
A closed thin-walled shell of revolution:

    hull diameter at the base   0.200 m
    fairing length              0.294 m  (semi-ellipse truncated at the nose)
    wall thickness              0.003 m
    nose window diameter        0.040 m  (the truncation)

The outer profile is a semi-ellipse of semi-axes 0.300 m (axial) x 0.100 m
(radial), truncated at z = 0.294 m so the nose ends in a flat annulus — which
is what an AUV nose actually looks like when it carries a forward-looking
sonar or altimeter window. The inner profile is a true normal offset of the
outer profile by the wall thickness, not a radial offset, so the wall is 3 mm
everywhere including through the nose curvature (the local radius of curvature
at the nose vertex is b^2/a = 33 mm, comfortably larger than the 3 mm offset,
so the offset does not self-intersect).

Frame (metres): the axis of revolution is +Z, base at z = 0, nose at z =
0.294. That is the print orientation the FFF example uses — base flange down
on the bed, nose up — which is why the outer skin's overhang angle gets worse
the closer you get to the tip. That progression is the point of the example.

What this is NOT: a hydrodynamic form. A real AUV nose is a Myring or
semi-ellipsoidal form fitted to a drag target, and it carries mounting bosses,
a window seat, penetrator holes and an internal frame. This is the outer mould
line plus a constant wall, which is all a process-simulation demo needs.
"""

from __future__ import annotations

import math
import sys

# --------------------------------------------------------------------------
# Fairing parameters (metres).
# --------------------------------------------------------------------------
BASE_RADIUS = 0.100           # ellipse semi-minor axis => 0.2 m hull diameter
NOSE_SEMI_AXIS = 0.300        # ellipse semi-major axis, along the +Z axis
TRUNCATION_Z = 0.294          # cut the nose here, leaving a flat window
WALL_THICKNESS = 0.003
N_THETA = 36                  # circumferential facets
N_AXIAL = 13                  # profile stations, base to nose inclusive

OBJ_PATH = "fairing.obj"


def station_angles() -> list[float]:
    """Ellipse angles of the profile stations, base (0) to nose truncation.

    Parameterising by the ellipse angle rather than by z bunches the stations
    towards the nose, which is where the curvature is.
    """
    psi_tip = math.asin(TRUNCATION_Z / NOSE_SEMI_AXIS)
    return [psi_tip * i / (N_AXIAL - 1) for i in range(N_AXIAL)]


def outer_profile() -> list[tuple[float, float]]:
    """Outer meridian as (z, r) pairs, base first, nose last."""
    return [(NOSE_SEMI_AXIS * math.sin(p), BASE_RADIUS * math.cos(p))
            for p in station_angles()]


def inner_profile() -> list[tuple[float, float]]:
    """Outer meridian offset by the wall thickness along its inward normal.

    The meridian tangent at ellipse angle psi is analytic:
    d(z, r)/dpsi = (a*cos psi, -b*sin psi). A normal to it is (-dr, dz), which
    at the base (psi = 0) is exactly radial and at the nose tips forward as
    well as outward — the outward normal of the shell. Inner point = outer
    point - t * outward_normal. Using the analytic tangent rather than a
    finite difference keeps the base annulus exactly planar at z = 0, which
    matters: that face is the print bed contact.
    """
    out = []
    for psi in station_angles():
        z = NOSE_SEMI_AXIS * math.sin(psi)
        r = BASE_RADIUS * math.cos(psi)
        dz = NOSE_SEMI_AXIS * math.cos(psi)
        dr = -BASE_RADIUS * math.sin(psi)
        nz, nr = -dr, dz                       # outward normal, (z, r) parts
        mag = math.hypot(nz, nr)
        out.append((z - WALL_THICKNESS * nz / mag, r - WALL_THICKNESS * nr / mag))
    return out


def build_fairing():
    """Return (vertices, faces). Faces are (index_list, group_name)."""
    outer = outer_profile()
    inner = inner_profile()

    verts: list[tuple[float, float, float]] = []
    for profile in (outer, inner):
        for z, r in profile:
            for j in range(N_THETA):
                angle = 2.0 * math.pi * j / N_THETA
                verts.append((r * math.cos(angle), r * math.sin(angle), z))

    stride = N_AXIAL * N_THETA

    def o(k: int, j: int) -> int:
        return k * N_THETA + (j % N_THETA)

    def i_(k: int, j: int) -> int:
        return stride + k * N_THETA + (j % N_THETA)

    faces: list[tuple[list[int], str]] = []

    # Outer skin — the wetted, sanded, painted surface. Normals point out.
    for k in range(N_AXIAL - 1):
        for j in range(N_THETA):
            faces.append(([o(k, j), o(k, j + 1), o(k + 1, j + 1), o(k + 1, j)],
                          "skin_outer"))
    # Inner skin — normals point into the bay, i.e. out of the laminate.
    for k in range(N_AXIAL - 1):
        for j in range(N_THETA):
            faces.append(([i_(k, j), i_(k + 1, j), i_(k + 1, j + 1), i_(k, j + 1)],
                          "skin_inner"))
    # Base flange annulus at z = 0 (normal -Z): the bolted joint to the hull
    # tube and the first printed layer.
    for j in range(N_THETA):
        faces.append(([o(0, j), i_(0, j), i_(0, j + 1), o(0, j + 1)], "base_flange"))
    # Nose window annulus at the truncation (normal roughly +Z).
    last = N_AXIAL - 1
    for j in range(N_THETA):
        faces.append(([o(last, j), o(last, j + 1), i_(last, j + 1), i_(last, j)],
                      "nose_window"))
    return verts, faces


# --------------------------------------------------------------------------
# Verification (identical checks in all four AM examples' generators).
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

    directed: dict[tuple[int, int], int] = {}
    for a, b, c in tris:
        for e in ((a, b), (b, c), (c, a)):
            directed[e] = directed.get(e, 0) + 1
    bad_directed = sum(1 for v in directed.values() if v != 1)
    open_edges = sum(1 for (a, b) in directed if (b, a) not in directed)

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
    radii = [math.hypot(v[0], v[1]) for v in verts]
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


def steepest_overhang_deg() -> float:
    """Worst outer-skin tilt from the build plate, for the README.

    For a build direction b = +Z and an outward facet normal n, the facet is a
    downskin when n.b < 0 and its tilt from the plate is acos(|n.b|) — the
    definition solver.am.overhang uses. Every outer-skin facet of a nose cone
    printed base-down has n.b > 0, so none of them are downskins; what limits
    the print is the *self-supporting* angle, i.e. the wall's tilt from
    vertical. This returns the smallest tilt-from-plate over the outer skin,
    which is what an overhang check on this part will report at the tip.
    """
    outer = outer_profile()
    worst = 90.0
    for i in range(len(outer) - 1):
        z0, r0 = outer[i]
        z1, r1 = outer[i + 1]
        # Facet normal in the (r, z) half-plane: (-dr, dz) normalised.
        dz, dr = z1 - z0, r1 - r0
        mag = math.hypot(dz, dr)
        n_z = -dr / mag
        tilt = math.degrees(math.acos(min(abs(n_z), 1.0)))
        worst = min(worst, tilt)
    return worst


def fmt(x: float) -> str:
    s = f"{x:.6f}"
    return "0.000000" if s == "-0.000000" else s


def write_obj(path: str, verts, faces, stats) -> None:
    (xlo, ylo, zlo), (xhi, yhi, zhi) = stats["bbox"]
    nose_r = outer_profile()[-1][1]
    lines = [
        "# fairing.obj — nose fairing shell of a 0.2 m diameter AUV.",
        "#",
        "# GENERATED FILE. Do not hand-edit: regenerate with",
        "#   python3 examples/am-polymer-auv-fairing/generate-geometry.py",
        "# which is deterministic and reproduces this file byte-for-byte.",
        "#",
        f"# Semi-elliptical outer profile, semi-axes {NOSE_SEMI_AXIS:.3f} m axial x "
        f"{BASE_RADIUS:.3f} m radial,",
        f"# truncated at z = {TRUNCATION_Z:.3f} m leaving a "
        f"{2.0 * nose_r * 1000.0:.0f} mm sonar-window annulus.",
        f"# Wall {WALL_THICKNESS * 1000.0:.0f} mm, as a true normal offset of the "
        "outer meridian.",
        f"# Axis of revolution is +Z (also the print direction). {N_THETA} "
        f"circumferential",
        f"# facets x {N_AXIAL - 1} axial rows per skin.",
        "#",
        f"# {stats['vertices']} vertices, {stats['faces']} faces "
        f"({stats['triangles']} Tri3 cells after obj-reader fan-triangulation).",
        f"# Enclosed laminate volume {stats['volume_m3'] * 1e6:.1f} cm^3.",
        f"# Bounding box ({xlo:.4f}, {ylo:.4f}, {zlo:.4f}) .. "
        f"({xhi:.4f}, {yhi:.4f}, {zhi:.4f}) m.",
        "#",
        "# Vertex layout: outer skin first, N_AXIAL rows of N_THETA nodes by",
        "# increasing z then increasing theta, then the inner skin the same",
        "# way. Surfaces are named with `g` groups, which obj-reader ignores:",
        "#   skin_outer   — wetted surface",
        "#   skin_inner   — dry bay",
        "#   base_flange  — z = 0 annulus, bolted joint + first print layer",
        "#   nose_window  — the truncation annulus at the tip",
        "#",
        "# They are deliberately NOT `usemtl` groups. obj-reader maps usemtl",
        "# names to per-cell tags, and every AM capability reads a non-negative",
        "# cell tag as a LAYER INDEX (contract § 2.2 rule 1) — a usemtl-tagged",
        "# OBJ would be simulated as a four-layer part instead of the 735 print",
        "# layers this example asks for. Untagged cells (tag -1) take the",
        "# documented fallback: bin the cell centroid along build_direction by",
        "# layer_height.",
        "#",
        "# Faces are quads wound CCW seen from outside the laminate, so",
        "# normals point out of the solid (enclosed volume above is positive).",
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
    verts, faces = build_fairing()
    stats = verify(verts, faces)
    if stats["bad_directed_edges"] or stats["open_edges"]:
        raise SystemExit(
            f"surface is not an oriented 2-manifold: "
            f"{stats['bad_directed_edges']} repeated directed edges, "
            f"{stats['open_edges']} open edges"
        )
    if stats["volume_m3"] <= 0.0:
        raise SystemExit("enclosed volume is not positive — winding is inverted")

    print(f"vertices              {stats['vertices']}")
    print(f"faces (OBJ f lines)   {stats['faces']}")
    print(f"Tri3 cells            {stats['triangles']}")
    print(f"open edges            {stats['open_edges']}")
    print(f"laminate volume       {stats['volume_m3'] * 1e6:.2f} cm^3")
    print(f"mass @ 1060 kg/m^3    {stats['volume_m3'] * 1060.0:.4f} kg (PA12-CF)")
    print(f"mass @ 1300 kg/m^3    {stats['volume_m3'] * 1300.0:.4f} kg (PEKK)")
    print(f"bbox min              {stats['bbox'][0]}")
    print(f"bbox max              {stats['bbox'][1]}")
    print(f"radius range          {stats['radius_range']}")
    print(f"nose window diameter  {2.0 * outer_profile()[-1][1]:.4f} m")
    print(f"min outer-skin tilt   {steepest_overhang_deg():.1f} deg from the plate")

    if "--check" in argv:
        print("check only; not writing")
        return 0
    write_obj(OBJ_PATH, verts, faces, stats)
    print(f"wrote {OBJ_PATH}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
