#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generate `hull-ring.obj` — one bay of a 1.0 m diameter pressure hull.

Deterministic, Python-3-stdlib-only, no numpy. Re-running it reproduces the
committed `hull-ring.obj` byte-for-byte.

    python3 generate-geometry.py            # writes hull-ring.obj here
    python3 generate-geometry.py --check    # verify only, do not write

What it builds
--------------
A plain annular cylinder — the shell of one frame bay of a ring-stiffened
pressure hull, faceted into quads:

    outer diameter   1.000 m
    wall thickness   0.012 m   (outer R 0.500, inner R 0.488)
    bay length       0.500 m   (z from 0.0 to 0.5)

Frame (metres): the cylinder axis is +Z. That is the *manufacturing* frame —
+Z is the deposition direction, so a slicer sees 0.5 m of stacked annuli. In
service the hull axis lies horizontal, which is why the hydrostatic stage in
pipeline.yaml declares `build_direction: [0, 1, 0]`: pressure must vary over
the 1.0 m of hull diameter, not along the hull axis. That mismatch is
deliberate and is explained in README.md.

What it deliberately does NOT contain
-------------------------------------
The ring frames themselves. A ring-stiffened hull is shell + internal
T-frames, and the frames are what set the interframe collapse mode. Modelling
them geometrically would need a boolean union this generator has no business
doing, and `solver.marine.hull_collapse` is an *analytical* capability anyway
— it reads the frame spacing as the `unsupported_length` input (0.5 m here,
matching the bay length) rather than measuring it off the mesh. So the OBJ is
the shell of one bay and the frames are a number in the YAML. Read the
collapse-margin section of README.md before you believe any output.
"""

from __future__ import annotations

import math
import sys

# --------------------------------------------------------------------------
# Hull parameters (metres).
# --------------------------------------------------------------------------
OUTER_DIAMETER = 1.000
WALL_THICKNESS = 0.012
BAY_LENGTH = 0.500
N_THETA = 64                  # circumferential facets
N_AXIAL = 6                   # axial facet rows (=> N_AXIAL + 1 stations)

OBJ_PATH = "hull-ring.obj"

R_OUT = OUTER_DIAMETER / 2.0
R_IN = R_OUT - WALL_THICKNESS


def build_ring():
    """Return (vertices, faces). Faces are (index_list, group_name)."""
    verts: list[tuple[float, float, float]] = []

    # Two concentric grids of (N_AXIAL + 1) x N_THETA vertices: outer first,
    # then inner. Index layout is documented in the OBJ header.
    for radius in (R_OUT, R_IN):
        for k in range(N_AXIAL + 1):
            z = BAY_LENGTH * k / N_AXIAL
            for j in range(N_THETA):
                angle = 2.0 * math.pi * j / N_THETA
                verts.append((radius * math.cos(angle), radius * math.sin(angle), z))

    stride = (N_AXIAL + 1) * N_THETA

    def outer(k: int, j: int) -> int:
        return k * N_THETA + (j % N_THETA)

    def inner(k: int, j: int) -> int:
        return stride + k * N_THETA + (j % N_THETA)

    faces: list[tuple[list[int], str]] = []

    # Outer shell — the wetted surface. Normals point radially out.
    for k in range(N_AXIAL):
        for j in range(N_THETA):
            faces.append((
                [outer(k, j), outer(k, j + 1), outer(k + 1, j + 1), outer(k + 1, j)],
                "shell_outer",
            ))
    # Inner shell — the dry bore. Normals point radially *in*, i.e. out of
    # the solid wall.
    for k in range(N_AXIAL):
        for j in range(N_THETA):
            faces.append((
                [inner(k, j), inner(k + 1, j), inner(k + 1, j + 1), inner(k, j + 1)],
                "shell_inner",
            ))
    # Annular end faces. z = 0 is the aft frame land (normal -Z), z = L the
    # forward frame land (normal +Z).
    for j in range(N_THETA):
        faces.append((
            [outer(0, j), inner(0, j), inner(0, j + 1), outer(0, j + 1)],
            "frame_land_aft",
        ))
    for j in range(N_THETA):
        faces.append((
            [outer(N_AXIAL, j), outer(N_AXIAL, j + 1),
             inner(N_AXIAL, j + 1), inner(N_AXIAL, j)],
            "frame_land_fwd",
        ))
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


def fmt(x: float) -> str:
    s = f"{x:.6f}"
    return "0.000000" if s == "-0.000000" else s


def write_obj(path: str, verts, faces, stats) -> None:
    (xlo, ylo, zlo), (xhi, yhi, zhi) = stats["bbox"]
    # Exact annulus volume, for comparison with the faceted one.
    exact = math.pi * (R_OUT ** 2 - R_IN ** 2) * BAY_LENGTH
    lines = [
        "# hull-ring.obj — one frame bay of a 1.0 m diameter pressure hull.",
        "#",
        "# GENERATED FILE. Do not hand-edit: regenerate with",
        "#   python3 examples/am-submarine-pressure-hull/generate-geometry.py",
        "# which is deterministic and reproduces this file byte-for-byte.",
        "#",
        f"# Outer diameter {OUTER_DIAMETER:.3f} m, wall {WALL_THICKNESS * 1000.0:.0f} mm "
        f"(R {R_OUT:.3f} / {R_IN:.3f} m), bay length {BAY_LENGTH:.3f} m.",
        "# Cylinder axis is +Z (the manufacturing frame).",
        f"# {N_THETA} circumferential facets x {N_AXIAL} axial rows per surface.",
        "#",
        f"# {stats['vertices']} vertices, {stats['faces']} faces "
        f"({stats['triangles']} Tri3 cells after obj-reader fan-triangulation).",
        f"# Faceted volume {stats['volume_m3'] * 1e6:.1f} cm^3 vs exact annulus "
        f"{exact * 1e6:.1f} cm^3 ({100.0 * (1.0 - stats['volume_m3'] / exact):.2f}% low,",
        f"# which is the {N_THETA}-gon inscribed-polygon deficit — see README.md).",
        f"# Bounding box ({xlo:.4f}, {ylo:.4f}, {zlo:.4f}) .. "
        f"({xhi:.4f}, {yhi:.4f}, {zhi:.4f}) m.",
        "#",
        "# Vertex layout: outer grid first, (N_AXIAL+1) rows of N_THETA nodes",
        "# by increasing z then increasing theta, then the inner grid the same",
        "# way. Surfaces are named with `g` groups, which obj-reader ignores:",
        "#   shell_outer     — wetted surface, sees the hydrostatic load",
        "#   shell_inner     — dry bore",
        "#   frame_land_aft  — z = 0 annulus, the ring-frame seat",
        "#   frame_land_fwd  — z = L annulus, the next ring-frame seat",
        "#",
        "# They are deliberately NOT `usemtl` groups. obj-reader maps usemtl",
        "# names to per-cell tags, and every AM capability reads a non-negative",
        "# cell tag as a LAYER INDEX (contract § 2.2 rule 1) — a usemtl-tagged",
        "# OBJ would be simulated as a four-layer part. Untagged cells (tag -1)",
        "# take the documented fallback instead: bin the cell centroid along",
        "# build_direction by layer_height.",
        "#",
        "# Faces are quads wound CCW seen from outside the solid wall, so",
        "# normals point out of the metal (enclosed volume above is positive).",
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
    verts, faces = build_ring()
    stats = verify(verts, faces)
    if stats["bad_directed_edges"] or stats["open_edges"]:
        raise SystemExit(
            f"surface is not an oriented 2-manifold: "
            f"{stats['bad_directed_edges']} repeated directed edges, "
            f"{stats['open_edges']} open edges"
        )
    if stats["volume_m3"] <= 0.0:
        raise SystemExit("enclosed volume is not positive — winding is inverted")

    exact = math.pi * (R_OUT ** 2 - R_IN ** 2) * BAY_LENGTH
    print(f"vertices            {stats['vertices']}")
    print(f"faces (OBJ f lines) {stats['faces']}")
    print(f"Tri3 cells          {stats['triangles']}")
    print(f"open edges          {stats['open_edges']}")
    print(f"volume              {stats['volume_m3'] * 1e6:.2f} cm^3 "
          f"(exact annulus {exact * 1e6:.2f} cm^3)")
    print(f"mass @ 7990 kg/m^3  {stats['volume_m3'] * 7990.0:.2f} kg")
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
