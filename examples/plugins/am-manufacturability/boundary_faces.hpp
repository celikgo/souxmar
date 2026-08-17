// SPDX-License-Identifier: Apache-2.0
//
// boundary_faces — mesh-topology support code for the am-manufacturability
// plugin. Extracts the boundary (free-surface) facets of a mesh, their
// outward unit normals and their areas, plus per-cell centroids and volumes.
//
// Why this exists plugin-side: the host has the same tables in
// src/core/face_topology.cpp, but plugins link only
// souxmar::public_headers (no libsouxmar-core), so the tables are
// duplicated here. They are copied from that file *verbatim* — the local
// face ordering below must stay byte-for-byte equivalent to the host's,
// because `souxmar_mesh_face_tag(mesh, cell, local_face)` indexes faces
// with exactly this ordering, and `mesher.am.layered` writes its
// boundary-face tags (contract §2.3) against it. openfoam-solver and
// cfd-stub carry their own copies of the same tables for the same reason.
//
// What it computes:
//   * Per cell: the arithmetic mean of its corner nodes (the "centroid"
//     used only for the outward-normal sign test and for layer binning),
//     and the cell volume by the divergence theorem over the fan
//     triangulation of its faces,
//         V = (1/3) * sum_tri (p0_tri - x_centroid) . a_tri
//     where `a_tri = 0.5 * (p1-p0) x (p2-p0)` is the outward area vector
//     of a fan triangle. Exact for straight-edged cells with planar
//     faces; for a warped hex face it is the exact volume of the
//     fan-triangulated polyhedron, which is what the area/normal
//     computation also sees, so the two stay consistent.
//   * Boundary faces: every face of every cell is keyed by its *sorted*
//     global node ids; faces whose key occurs exactly once are on the
//     boundary. Two cells sharing a face necessarily list the same node
//     set, so the key is orientation- and rotation-independent.
//   * Face area: sum of the fan triangles' areas (|a_tri|).
//   * Face normal: the normalised sum of the fan triangles' area vectors
//     (the polygon's vector area), then sign-corrected so that it points
//     away from the owning cell's centroid.
//
// What this is NOT: a half-edge / topology library. It does not detect
// non-manifold meshes (a face shared by three cells is simply not on the
// boundary), does not stitch geometrically-coincident-but-separately-
// numbered nodes (two blocks meshed with duplicated interface nodes will
// each report that interface as boundary), and does not curve-fit
// quadratic elements — Tet10/Hex20/Hex27/Prism15/Pyramid13/Tri6/Quad8/
// Quad9 are lowered to their leading corner nodes (souxmar, like Gmsh and
// VTK, lists corner nodes first), so a curved facet is measured as the
// straight-edged facet through its corners. That under-estimates the area
// of a strongly curved quadratic face.
//
// Determinism: cells are visited in index order, faces in local-face
// order, fan triangles in vertex order, and the face key index is a
// `std::map` over a fixed-size sorted array of node ids — a total order
// with no ties. No `unordered_*` container is iterated, no accumulation
// order depends on the platform. Cost is O(F log F) in the number of
// cell faces.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "souxmar-c/mesh.h"
#include "souxmar-c/status.h"
#include "souxmar-c/types.h"

namespace souxmar::examples::dfam {

using Vec3 = std::array<double, 3>;

// ---- Small vector helpers (metres in, metres out) ----------------------

[[nodiscard]] Vec3   vec_sub(const Vec3& a, const Vec3& b) noexcept;
[[nodiscard]] double vec_dot(const Vec3& a, const Vec3& b) noexcept;
[[nodiscard]] Vec3   vec_cross(const Vec3& a, const Vec3& b) noexcept;
[[nodiscard]] double vec_norm(const Vec3& a) noexcept;
// Returns a/|a|, or `fallback` when |a| is not strictly positive.
[[nodiscard]] Vec3   vec_normalise(const Vec3& a, const Vec3& fallback) noexcept;

// ---- Local face tables (copied from src/core/face_topology.cpp) --------

// Largest values over the element types this file decomposes: Hex8 has 6
// faces, a quadrilateral face has 4 vertices, Hex27 has 27 nodes.
constexpr std::size_t kMaxFacesPerCell = 6;
constexpr std::size_t kMaxFaceVertices = 4;
constexpr std::size_t kMaxCellNodes    = 27;

struct LocalFace {
  // 3 for triangular faces, 4 for quadrilateral faces.
  std::uint8_t vertex_count = 0;
  // Cell-local vertex indices, listed CCW seen from outside the cell.
  // Slots [vertex_count..3] are unused for triangular faces.
  std::array<std::uint8_t, kMaxFaceVertices> cell_local_idx{{0, 0, 0, 0}};
};

struct LocalFaceTable {
  std::size_t                                 face_count = 0;
  std::array<LocalFace, kMaxFacesPerCell>     faces{};
  // Number of leading nodes of the cell that are corner nodes. Quadratic
  // element types are lowered to this many nodes (see the header comment);
  // for linear types it equals the cell's node count.
  std::size_t                                 corner_node_count = 0;
  // True for Tri3/Quad4/Tri6/Quad8/Quad9 — the cell *is* its own single
  // facet, so it encloses no volume and its facet orientation comes from
  // the cell's node ordering rather than from a centroid sign test.
  bool                                        is_surface_element = false;
};

// Empty table (face_count == 0) for element types without a face
// decomposition: Unknown, Vertex, Edge2, Edge3.
[[nodiscard]] LocalFaceTable local_face_table(std::uint16_t element_type) noexcept;

// ---- Extracted geometry -----------------------------------------------

struct BoundaryFace {
  std::size_t  cell       = 0;    // owning cell index
  std::uint8_t local_face = 0;    // local face index in the host's ordering
  Vec3         normal{{0.0, 0.0, 0.0}};    // outward unit normal (zero if degenerate)
  Vec3         centroid{{0.0, 0.0, 0.0}};  // mean of the facet's corner nodes, m
  double       area = 0.0;                 // m^2
};

struct MeshGeometry {
  std::size_t         num_nodes = 0;
  std::size_t         num_cells = 0;
  // Borrowed from souxmar_mesh_nodes_flat(); 3 * num_nodes doubles, valid
  // for as long as the mesh is not mutated (i.e. for the whole solve call).
  const double*       node_coords = nullptr;

  std::vector<Vec3>   cell_centroid;  // per cell, index order, m
  std::vector<double> cell_volume;    // per cell, m^3; 0 for surface cells

  // Ordered by (cell index, local face index) — the order they were
  // discovered in, which is the deterministic traversal order.
  std::vector<BoundaryFace> boundary_faces;

  // Axis-aligned bounding box over all nodes, m.
  Vec3 bbox_min{{0.0, 0.0, 0.0}};
  Vec3 bbox_max{{0.0, 0.0, 0.0}};

  std::size_t num_volume_cells      = 0;  // cells with a 3-D face table
  std::size_t num_surface_cells     = 0;  // Tri3/Quad4/... single-facet cells
  std::size_t num_unsupported_cells = 0;  // no face table (Vertex/Edge*/unknown)
};

// Fills *out. Returns SOUXMAR_E_INVALID_ARGUMENT for a NULL/empty mesh,
// SOUXMAR_E_INTERNAL for an inconsistent node buffer or an out-of-range
// node reference. Every message string is a literal (static storage
// duration) as the ABI requires. On failure *out is left in an
// unspecified-but-valid state and must not be read.
[[nodiscard]] souxmar_status_t build_mesh_geometry(const souxmar_mesh_t* mesh,
                                                  MeshGeometry*         out);

// Extent of the mesh's nodes projected on the unit vector `axis`:
// *out_lo = min over nodes of (x_i . axis), *out_hi = the max. Returns
// false when the mesh has no nodes (outputs untouched).
[[nodiscard]] bool projected_range(const MeshGeometry& geom,
                                   const Vec3&         axis,
                                   double*             out_lo,
                                   double*             out_hi) noexcept;

// A right-handed orthonormal pair spanning the plane perpendicular to the
// unit vector `axis`. Deterministic: the seed axis is the cardinal
// direction with the smallest |component| of `axis`, ties broken X, Y, Z.
void plane_basis(const Vec3& axis, Vec3* out_u, Vec3* out_v) noexcept;

}  // namespace souxmar::examples::dfam
