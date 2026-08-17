// SPDX-License-Identifier: Apache-2.0
//
// Implementation of the boundary-face extraction described in
// boundary_faces.hpp. The local face tables are copied verbatim from
// src/core/face_topology.cpp — see the header for why the duplication is
// deliberate and why the ordering may not drift.

#include "boundary_faces.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

namespace souxmar::examples::dfam {

namespace {

// Tet4 — 4 triangular faces (opposite-vertex convention).
constexpr LocalFace kTet4Faces[4] = {
    {3, {{1, 2, 3, 0}}},  // opposite v0
    {3, {{0, 3, 2, 0}}},  // opposite v1
    {3, {{0, 1, 3, 0}}},  // opposite v2
    {3, {{0, 2, 1, 0}}},  // opposite v3
};

// Hex8 — 6 quadrilateral faces; VTK_HEXAHEDRON / souxmar internal
// convention. v[0..3] bottom (z=0, CCW from above), v[4..7] top
// (z=1), v[i+4] stacked above v[i]. CCW from outside the cell.
constexpr LocalFace kHex8Faces[6] = {
    {4, {{0, 3, 2, 1}}},  // -z (bottom)
    {4, {{4, 5, 6, 7}}},  // +z (top)
    {4, {{0, 1, 5, 4}}},  // -y (front)
    {4, {{3, 7, 6, 2}}},  // +y (back)
    {4, {{0, 4, 7, 3}}},  // -x (left)
    {4, {{1, 2, 6, 5}}},  // +x (right)
};

// Prism6 — 2 triangular caps + 3 quadrilateral sides.
// v[0..2] bottom triangle, v[3..5] top, v[i+3] above v[i].
constexpr LocalFace kPrism6Faces[5] = {
    {3, {{0, 2, 1, 0}}},  // -z (bottom triangle)
    {3, {{3, 4, 5, 0}}},  // +z (top triangle)
    {4, {{0, 1, 4, 3}}},  // side 0-1
    {4, {{1, 2, 5, 4}}},  // side 1-2
    {4, {{2, 0, 3, 5}}},  // side 2-0
};

// Pyramid5 — 1 quadrilateral base + 4 triangular sides meeting at
// the apex. v[0..3] base (z=0, CCW from above), v[4] apex.
constexpr LocalFace kPyramid5Faces[5] = {
    {4, {{0, 3, 2, 1}}},  // -z (base quad)
    {3, {{0, 1, 4, 0}}},  // side 0-1
    {3, {{1, 2, 4, 0}}},  // side 1-2
    {3, {{2, 3, 4, 0}}},  // side 2-3
    {3, {{3, 0, 4, 0}}},  // side 3-0
};

// Tri3 — the cell IS its single "face". v[0..2] CCW from above.
constexpr LocalFace kTri3Faces[1] = {
    {3, {{0, 1, 2, 0}}},
};

// Quad4 — the cell IS its single "face". v[0..3] CCW from above.
constexpr LocalFace kQuad4Faces[1] = {
    {4, {{0, 1, 2, 3}}},
};

LocalFaceTable make_table(const LocalFace* faces,
                          std::size_t      face_count,
                          std::size_t      corner_node_count,
                          bool             is_surface_element) noexcept {
  LocalFaceTable t;
  t.face_count         = face_count;
  t.corner_node_count  = corner_node_count;
  t.is_surface_element = is_surface_element;
  for (std::size_t i = 0; i < face_count && i < kMaxFacesPerCell; ++i) {
    t.faces[i] = faces[i];
  }
  return t;
}

// Sorted global node ids of one face. Unused slots hold UINT64_MAX, which
// sorts last and can never collide with a real node id (ids are validated
// against num_nodes before the key is built), so a triangular key and a
// quadrilateral key are never equal. std::array's lexicographic operator<
// gives the total order std::map needs — a fixed-size array avoids the
// per-face heap allocation a std::vector key would cost while keeping the
// "sorted node-index key" contract of §3.9.
using FaceKey = std::array<std::uint64_t, kMaxFaceVertices>;

// One candidate face, in (cell, local face) discovery order.
struct CandidateFace {
  FaceKey      key{};
  BoundaryFace face{};
};

}  // namespace

Vec3 vec_sub(const Vec3& a, const Vec3& b) noexcept {
  return Vec3{{a[0] - b[0], a[1] - b[1], a[2] - b[2]}};
}

double vec_dot(const Vec3& a, const Vec3& b) noexcept {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

Vec3 vec_cross(const Vec3& a, const Vec3& b) noexcept {
  return Vec3{{a[1] * b[2] - a[2] * b[1],
               a[2] * b[0] - a[0] * b[2],
               a[0] * b[1] - a[1] * b[0]}};
}

double vec_norm(const Vec3& a) noexcept {
  return std::sqrt(vec_dot(a, a));
}

Vec3 vec_normalise(const Vec3& a, const Vec3& fallback) noexcept {
  const double n = vec_norm(a);
  if (!(n > 0.0)) return fallback;
  return Vec3{{a[0] / n, a[1] / n, a[2] / n}};
}

LocalFaceTable local_face_table(std::uint16_t element_type) noexcept {
  switch (element_type) {
    // Linear volume elements.
    case SOUXMAR_ET_TET4:     return make_table(kTet4Faces,     4, 4,  false);
    case SOUXMAR_ET_HEX8:     return make_table(kHex8Faces,     6, 8,  false);
    case SOUXMAR_ET_PRISM6:   return make_table(kPrism6Faces,   5, 6,  false);
    case SOUXMAR_ET_PYRAMID5: return make_table(kPyramid5Faces, 5, 5,  false);
    // Quadratic volume elements, lowered to their corner nodes.
    case SOUXMAR_ET_TET10:    return make_table(kTet4Faces,     4, 4,  false);
    case SOUXMAR_ET_HEX20:
    case SOUXMAR_ET_HEX27:    return make_table(kHex8Faces,     6, 8,  false);
    case SOUXMAR_ET_PRISM15:  return make_table(kPrism6Faces,   5, 6,  false);
    case SOUXMAR_ET_PYRAMID13:return make_table(kPyramid5Faces, 5, 5,  false);
    // Surface elements — the cell is its own single facet.
    case SOUXMAR_ET_TRI3:     return make_table(kTri3Faces,     1, 3,  true);
    case SOUXMAR_ET_TRI6:     return make_table(kTri3Faces,     1, 3,  true);
    case SOUXMAR_ET_QUAD4:    return make_table(kQuad4Faces,    1, 4,  true);
    case SOUXMAR_ET_QUAD8:
    case SOUXMAR_ET_QUAD9:    return make_table(kQuad4Faces,    1, 4,  true);
    default:                  return LocalFaceTable{};  // face_count == 0
  }
}

souxmar_status_t build_mesh_geometry(const souxmar_mesh_t* mesh,
                                    MeshGeometry*         out) {
  if (!mesh) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "mesh is NULL");
  }
  if (!out) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "out geometry is NULL");
  }

  const std::size_t num_nodes = souxmar_mesh_num_nodes(mesh);
  const std::size_t num_cells = souxmar_mesh_num_cells(mesh);
  if (num_nodes == 0) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "mesh has no nodes");
  }
  if (num_cells == 0) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "mesh has no cells");
  }

  std::size_t   flat_size = 0;
  const double* coords    = souxmar_mesh_nodes_flat(mesh, &flat_size);
  if (!coords || flat_size != num_nodes * 3) {
    return souxmar_status_error(
        SOUXMAR_E_INTERNAL,
        "souxmar_mesh_nodes_flat returned an inconsistent buffer");
  }

  out->num_nodes   = num_nodes;
  out->num_cells   = num_cells;
  out->node_coords = coords;
  out->cell_centroid.assign(num_cells, Vec3{{0.0, 0.0, 0.0}});
  out->cell_volume.assign(num_cells, 0.0);
  out->boundary_faces.clear();
  out->num_volume_cells      = 0;
  out->num_surface_cells     = 0;
  out->num_unsupported_cells = 0;

  // Bounding box in node index order.
  out->bbox_min = Vec3{{coords[0], coords[1], coords[2]}};
  out->bbox_max = out->bbox_min;
  for (std::size_t i = 1; i < num_nodes; ++i) {
    for (std::size_t d = 0; d < 3; ++d) {
      const double x = coords[i * 3 + d];
      if (x < out->bbox_min[d]) out->bbox_min[d] = x;
      if (x > out->bbox_max[d]) out->bbox_max[d] = x;
    }
  }

  std::vector<CandidateFace> candidates;
  // Every cell contributes at most kMaxFacesPerCell faces; reserving the
  // common (hex) case keeps the pass allocation-light.
  candidates.reserve(num_cells * 2);

  // key -> (index of the first candidate carrying it, how many cells share it)
  std::map<FaceKey, std::pair<std::size_t, std::size_t>> face_index;

  std::array<std::uint64_t, kMaxCellNodes> cell_nodes{};
  std::array<Vec3, kMaxCellNodes>          cell_xyz{};

  for (std::size_t c = 0; c < num_cells; ++c) {
    const std::uint16_t etype      = souxmar_mesh_cell_type(mesh, c);
    const std::size_t   node_count = souxmar_mesh_cell_node_count(mesh, c);
    const LocalFaceTable table     = local_face_table(etype);

    if (node_count == 0 || node_count > kMaxCellNodes ||
        table.face_count == 0 || table.corner_node_count > node_count) {
      // Unknown / face-less element type (Vertex, Edge2, Edge3) or a cell
      // the host reports with fewer nodes than its type needs. It gets a
      // zero centroid, zero volume, and no faces; the callers report the
      // count so the user sees that part of the mesh was skipped.
      ++out->num_unsupported_cells;
      continue;
    }

    const souxmar_status_t cs =
        souxmar_mesh_cell_nodes(mesh, c, cell_nodes.data(), cell_nodes.size());
    if (cs.code != SOUXMAR_OK) return cs;

    for (std::size_t k = 0; k < table.corner_node_count; ++k) {
      const std::uint64_t nid = cell_nodes[k];
      if (nid >= num_nodes) {
        return souxmar_status_error(SOUXMAR_E_INTERNAL,
                                    "cell references out-of-range node id");
      }
      cell_xyz[k] = Vec3{{coords[nid * 3 + 0],
                          coords[nid * 3 + 1],
                          coords[nid * 3 + 2]}};
    }

    // Cell centroid = arithmetic mean of the corner nodes.
    Vec3 centroid{{0.0, 0.0, 0.0}};
    for (std::size_t k = 0; k < table.corner_node_count; ++k) {
      for (std::size_t d = 0; d < 3; ++d) centroid[d] += cell_xyz[k][d];
    }
    const double inv_n = 1.0 / static_cast<double>(table.corner_node_count);
    for (std::size_t d = 0; d < 3; ++d) centroid[d] *= inv_n;
    out->cell_centroid[c] = centroid;

    if (table.is_surface_element) {
      ++out->num_surface_cells;
    } else {
      ++out->num_volume_cells;
    }

    double cell_volume = 0.0;

    for (std::size_t fi = 0; fi < table.face_count; ++fi) {
      const LocalFace& lf = table.faces[fi];
      const std::size_t vc = lf.vertex_count;

      // Fan-triangulate the facet about its first vertex, in vertex order.
      Vec3   area_vec{{0.0, 0.0, 0.0}};
      double area       = 0.0;
      double vol_term   = 0.0;  // sum_tri (p0 - centroid) . a_tri
      Vec3   face_mid{{0.0, 0.0, 0.0}};
      const Vec3& p0 = cell_xyz[lf.cell_local_idx[0]];
      for (std::size_t k = 1; k + 1 < vc; ++k) {
        const Vec3& p1 = cell_xyz[lf.cell_local_idx[k]];
        const Vec3& p2 = cell_xyz[lf.cell_local_idx[k + 1]];
        const Vec3  e1 = vec_sub(p1, p0);
        const Vec3  e2 = vec_sub(p2, p0);
        const Vec3  cr = vec_cross(e1, e2);
        const Vec3  a  = Vec3{{0.5 * cr[0], 0.5 * cr[1], 0.5 * cr[2]}};
        area += vec_norm(a);
        for (std::size_t d = 0; d < 3; ++d) area_vec[d] += a[d];
        vol_term += vec_dot(vec_sub(p0, centroid), a);
      }
      for (std::size_t k = 0; k < vc; ++k) {
        for (std::size_t d = 0; d < 3; ++d) {
          face_mid[d] += cell_xyz[lf.cell_local_idx[k]][d];
        }
      }
      const double inv_vc = 1.0 / static_cast<double>(vc);
      for (std::size_t d = 0; d < 3; ++d) face_mid[d] *= inv_vc;

      // Sign-correct the table's orientation against the cell centroid.
      // For a well-formed cell listed by the canonical table this is a
      // no-op (the table is already CCW from outside); it rescues an
      // inverted / negatively-oriented cell. It cannot be applied to a
      // surface element, whose facet centroid *is* its cell centroid —
      // there the orientation is whatever the cell's node ordering says,
      // so a shell mesh must be consistently oriented for its normals to
      // come out pointing away from the solid.
      double sign = 1.0;
      if (!table.is_surface_element &&
          vec_dot(area_vec, vec_sub(face_mid, centroid)) < 0.0) {
        sign = -1.0;
      }
      cell_volume += sign * vol_term;

      const Vec3 oriented{{sign * area_vec[0], sign * area_vec[1], sign * area_vec[2]}};

      CandidateFace cf;
      cf.face.cell       = c;
      cf.face.local_face = static_cast<std::uint8_t>(fi);
      cf.face.normal     = vec_normalise(oriented, Vec3{{0.0, 0.0, 0.0}});
      cf.face.centroid   = face_mid;
      cf.face.area       = area;

      cf.key.fill(std::numeric_limits<std::uint64_t>::max());
      for (std::size_t k = 0; k < vc; ++k) {
        cf.key[k] = cell_nodes[lf.cell_local_idx[k]];
      }
      std::sort(cf.key.begin(), cf.key.end());

      const std::size_t slot = candidates.size();
      candidates.push_back(cf);
      const auto ins = face_index.emplace(cf.key, std::make_pair(slot, std::size_t{1}));
      if (!ins.second) {
        ++ins.first->second.second;
      }
    }

    // The divergence theorem gives a signed volume; report the magnitude.
    // Surface elements enclose nothing, so their volume stays 0.
    out->cell_volume[c] =
        table.is_surface_element ? 0.0 : std::abs(cell_volume / 3.0);
  }

  // A face is on the boundary when exactly one cell claims its node set.
  // Walking `candidates` (not the map) keeps the output in
  // (cell, local face) order.
  for (std::size_t i = 0; i < candidates.size(); ++i) {
    const auto it = face_index.find(candidates[i].key);
    if (it != face_index.end() && it->second.second == 1) {
      out->boundary_faces.push_back(candidates[i].face);
    }
  }

  return souxmar_status_ok();
}

bool projected_range(const MeshGeometry& geom,
                     const Vec3&         axis,
                     double*             out_lo,
                     double*             out_hi) noexcept {
  if (!geom.node_coords || geom.num_nodes == 0 || !out_lo || !out_hi) return false;
  double lo = std::numeric_limits<double>::max();
  double hi = -std::numeric_limits<double>::max();
  for (std::size_t i = 0; i < geom.num_nodes; ++i) {
    const Vec3 x{{geom.node_coords[i * 3 + 0],
                  geom.node_coords[i * 3 + 1],
                  geom.node_coords[i * 3 + 2]}};
    const double p = vec_dot(x, axis);
    if (p < lo) lo = p;
    if (p > hi) hi = p;
  }
  *out_lo = lo;
  *out_hi = hi;
  return true;
}

void plane_basis(const Vec3& axis, Vec3* out_u, Vec3* out_v) noexcept {
  if (!out_u || !out_v) return;
  const double ax = std::abs(axis[0]);
  const double ay = std::abs(axis[1]);
  const double az = std::abs(axis[2]);
  Vec3 seed{{1.0, 0.0, 0.0}};
  if (ay < ax && ay <= az) {
    seed = Vec3{{0.0, 1.0, 0.0}};
  } else if (az < ax && az < ay) {
    seed = Vec3{{0.0, 0.0, 1.0}};
  }
  const Vec3 u = vec_normalise(vec_cross(axis, seed), Vec3{{1.0, 0.0, 0.0}});
  *out_u = u;
  *out_v = vec_normalise(vec_cross(axis, u), Vec3{{0.0, 1.0, 0.0}});
}

}  // namespace souxmar::examples::dfam
