// SPDX-License-Identifier: Apache-2.0
//
// am-slicer / slicer.cpp — implementation of the planar slicer declared in
// slicer.hpp. See that header for the algorithm, the pathological-case
// handling and the "what this is NOT" list.

#include "slicer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <map>
#include <set>
#include <utility>

namespace souxmar_am_slicer {

namespace {

constexpr std::uint32_t kNoSeg  = 0xFFFFFFFFu;
constexpr std::uint32_t kNoNode = 0xFFFFFFFFu;

// Shell meshes above this facet count skip the edge-manifold audit: the audit
// needs a map entry per facet edge, and refusing to allocate ~100 bytes per
// edge for a multi-million-triangle STL is the lesser evil. The verdict then
// stays Manifold::Unknown and the report says so.
constexpr std::size_t kMaxShellAuditFacets = 500000;

double dot(const Vec3& a, const Vec3& b) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

Vec3 cross(const Vec3& a, const Vec3& b) {
  return {a[1] * b[2] - a[2] * b[1],
          a[2] * b[0] - a[0] * b[2],
          a[0] * b[1] - a[1] * b[0]};
}

// ---- Local face tables -------------------------------------------------
// Copied verbatim from src/core/face_topology.cpp (souxmar's single source of
// truth for local face ordering; CCW seen from outside the cell).

struct LocalFace {
  std::uint8_t                n;
  std::array<std::uint8_t, 4> v;
};

constexpr LocalFace kTet4Faces[4] = {
    {3, {{1, 2, 3, 0}}},  // opposite v0
    {3, {{0, 3, 2, 0}}},  // opposite v1
    {3, {{0, 1, 3, 0}}},  // opposite v2
    {3, {{0, 2, 1, 0}}},  // opposite v3
};

constexpr LocalFace kHex8Faces[6] = {
    {4, {{0, 3, 2, 1}}},  // -z (bottom)
    {4, {{4, 5, 6, 7}}},  // +z (top)
    {4, {{0, 1, 5, 4}}},  // -y (front)
    {4, {{3, 7, 6, 2}}},  // +y (back)
    {4, {{0, 4, 7, 3}}},  // -x (left)
    {4, {{1, 2, 6, 5}}},  // +x (right)
};

constexpr LocalFace kPrism6Faces[5] = {
    {3, {{0, 2, 1, 0}}},  // -z (bottom triangle)
    {3, {{3, 4, 5, 0}}},  // +z (top triangle)
    {4, {{0, 1, 4, 3}}},  // side 0-1
    {4, {{1, 2, 5, 4}}},  // side 1-2
    {4, {{2, 0, 3, 5}}},  // side 2-0
};

constexpr LocalFace kPyramid5Faces[5] = {
    {4, {{0, 3, 2, 1}}},  // -z (base quad)
    {3, {{0, 1, 4, 0}}},  // side 0-1
    {3, {{1, 2, 4, 0}}},  // side 1-2
    {3, {{2, 3, 4, 0}}},  // side 2-3
    {3, {{3, 0, 4, 0}}},  // side 3-0
};

constexpr LocalFace kTri3Faces[1]  = {{3, {{0, 1, 2, 0}}}};
constexpr LocalFace kQuad4Faces[1] = {{4, {{0, 1, 2, 3}}}};

struct ElemTopo {
  std::uint8_t     corners    = 0;  // linear corner nodes used
  std::uint8_t     face_count = 0;
  const LocalFace* faces      = nullptr;
  bool             volume     = false;
  bool             shell      = false;
};

// Quadratic types map onto their linear corner subset: the souxmar/VTK/Gmsh
// node ordering puts the corners first, so the first `corners` entries of the
// connectivity are the linear element. Mid-side nodes are ignored, i.e. curved
// faces are approximated by their corner-node planes.
ElemTopo topo_for(std::uint16_t et) {
  switch (et) {
    case SOUXMAR_ET_TET4:
    case SOUXMAR_ET_TET10:
      return {4, 4, kTet4Faces, true, false};
    case SOUXMAR_ET_HEX8:
    case SOUXMAR_ET_HEX20:
    case SOUXMAR_ET_HEX27:
      return {8, 6, kHex8Faces, true, false};
    case SOUXMAR_ET_PRISM6:
    case SOUXMAR_ET_PRISM15:
      return {6, 5, kPrism6Faces, true, false};
    case SOUXMAR_ET_PYRAMID5:
    case SOUXMAR_ET_PYRAMID13:
      return {5, 5, kPyramid5Faces, true, false};
    case SOUXMAR_ET_TRI3:
    case SOUXMAR_ET_TRI6:
      return {3, 1, kTri3Faces, false, true};
    case SOUXMAR_ET_QUAD4:
    case SOUXMAR_ET_QUAD8:
    case SOUXMAR_ET_QUAD9:
      return {4, 1, kQuad4Faces, false, true};
    default:
      return {};
  }
}

using FaceKey = std::array<std::uint64_t, 4>;

FaceKey face_key(const std::uint64_t* cell_nodes, const LocalFace& f) {
  FaceKey k{UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX};
  for (std::uint8_t i = 0; i < f.n; ++i) k[i] = cell_nodes[f.v[i]];
  std::sort(k.begin(), k.begin() + f.n);
  return k;
}

void emit_face(const std::vector<Vec3>&  projected,
               const std::uint64_t*      cell_nodes,
               const LocalFace&          f,
               BoundaryMesh*             bm) {
  const Vec3& a = projected[static_cast<std::size_t>(cell_nodes[f.v[0]])];
  const Vec3& b = projected[static_cast<std::size_t>(cell_nodes[f.v[1]])];
  const Vec3& c = projected[static_cast<std::size_t>(cell_nodes[f.v[2]])];
  if (f.n == 3) {
    bm->triangles.push_back(Triangle{{a, b, c}});
    return;
  }
  // Fixed diagonal v0-v2 — the split rule the contract pins down, so a quad
  // face always produces the same two triangles on every platform.
  const Vec3& d = projected[static_cast<std::size_t>(cell_nodes[f.v[3]])];
  bm->triangles.push_back(Triangle{{a, b, c}});
  bm->triangles.push_back(Triangle{{a, c, d}});
  ++bm->quads_split;
}

// ---- Endpoint welding grid ---------------------------------------------

struct WeldGrid {
  double                                              cell = 1.0;
  double                                              tol  = 0.0;
  std::map<std::array<std::int64_t, 2>,
           std::vector<std::uint32_t>>                buckets;
  std::vector<Point2>                                 points;

  static std::int64_t quantise(double x, double cell_size) {
    const double s = x / cell_size;
    // Clamp before the cast: the guards also reject NaN (every comparison
    // with NaN is false, so both branches take the clamp).
    if (!(s > -9.0e15)) return -9000000000000000LL;
    if (!(s < 9.0e15)) return 9000000000000000LL;
    return static_cast<std::int64_t>(std::floor(s));
  }

  // Returns the index of an existing point within `tol`, preferring the
  // lowest such index, or inserts and returns a fresh index. Grid cells are
  // `tol` wide, so a 3x3 neighbourhood covers the whole tolerance disc.
  std::uint32_t weld(const Point2& p) {
    const std::int64_t ci = quantise(p.u, cell);
    const std::int64_t cj = quantise(p.v, cell);
    std::uint32_t      best = kNoNode;
    for (std::int64_t di = -1; di <= 1; ++di) {
      for (std::int64_t dj = -1; dj <= 1; ++dj) {
        const auto it = buckets.find({ci + di, cj + dj});
        if (it == buckets.end()) continue;
        for (const std::uint32_t idx : it->second) {
          const double du = points[idx].u - p.u;
          const double dv = points[idx].v - p.v;
          if (du * du + dv * dv <= tol * tol && idx < best) best = idx;
        }
      }
    }
    if (best != kNoNode) return best;
    const std::uint32_t id = static_cast<std::uint32_t>(points.size());
    points.push_back(p);
    buckets[{ci, cj}].push_back(id);
    return id;
  }
};

struct WeldedSeg {
  std::uint32_t a = 0;
  std::uint32_t b = 0;
};

double distance(const Point2& a, const Point2& b) {
  const double du = b.u - a.u;
  const double dv = b.v - a.v;
  return std::sqrt(du * du + dv * dv);
}

double shoelace(const std::vector<Point2>& pts) {
  double acc = 0.0;
  const std::size_t n = pts.size();
  for (std::size_t i = 0; i < n; ++i) {
    const Point2& a = pts[i];
    const Point2& b = pts[(i + 1) % n];
    acc += a.u * b.v - b.u * a.v;
  }
  return 0.5 * acc;
}

// Even-odd crossing test with the half-open rule on v, so a vertex exactly at
// the query height is counted once. A query point lying *on* the ring is
// arbitrary but deterministic (documented in slicer.hpp).
bool point_in_ring(const Point2& q, const std::vector<Point2>& ring) {
  bool              inside = false;
  const std::size_t n      = ring.size();
  for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
    const Point2& a = ring[i];
    const Point2& b = ring[j];
    if ((a.v > q.v) != (b.v > q.v)) {
      const double x = (b.u - a.u) * (q.v - a.v) / (b.v - a.v) + a.u;
      if (q.u < x) inside = !inside;
    }
  }
  return inside;
}

// Total order over contours: contract §3.12 asks for (min y, min x) of the
// lowest point; point count and the full coordinate list are appended as
// tiebreakers so std::sort has a genuine total order (determinism gate).
struct ContourLess {
  bool operator()(const Contour& a, const Contour& b) const {
    const Point2 la = lowest(a);
    const Point2 lb = lowest(b);
    if (la.v != lb.v) return la.v < lb.v;
    if (la.u != lb.u) return la.u < lb.u;
    if (a.points.size() != b.points.size()) return a.points.size() < b.points.size();
    for (std::size_t i = 0; i < a.points.size(); ++i) {
      if (a.points[i].v != b.points[i].v) return a.points[i].v < b.points[i].v;
      if (a.points[i].u != b.points[i].u) return a.points[i].u < b.points[i].u;
    }
    if (a.closed != b.closed) return static_cast<int>(a.closed) < static_cast<int>(b.closed);
    return false;
  }
  static Point2 lowest(const Contour& c) {
    Point2 best = c.points.empty() ? Point2{} : c.points[0];
    for (const Point2& p : c.points) {
      if (p.v < best.v || (p.v == best.v && p.u < best.u)) best = p;
    }
    return best;
  }
};

// ---- One plane ---------------------------------------------------------

void intersect_triangle(const Triangle& t,
                        double          h,
                        double          eps,
                        double          weld_tol,
                        LayerSlice*     ls,
                        Point2*         out_a,
                        Point2*         out_b,
                        bool*           out_has_segment) {
  *out_has_segment = false;

  double d[3];
  int    n_zero = 0;
  for (int i = 0; i < 3; ++i) {
    double di = t.p[static_cast<std::size_t>(i)][2] - h;
    if (std::abs(di) <= eps) di = 0.0;
    d[i] = di;
    if (di == 0.0) ++n_zero;
  }

  if (n_zero == 3) {
    // Triangle lies in the plane. Skipped on purpose: emitting its three
    // edges would double every boundary edge of the flat region and add its
    // interior edges, which chaining cannot resolve. The bounding edges of
    // the flat region come from the neighbouring triangles instead (each of
    // those has exactly two vertices in the plane, the case below).
    ++ls->coplanar_triangles;
    return;
  }

  Point2 pts[3];
  int    n_pts = 0;
  const auto push = [&](const Point2& p) {
    for (int k = 0; k < n_pts; ++k) {
      if (distance(pts[k], p) <= weld_tol) return;  // same point twice
    }
    if (n_pts < 3) pts[n_pts++] = p;
  };

  for (int i = 0; i < 3; ++i) {
    if (d[i] == 0.0) {
      push(Point2{t.p[static_cast<std::size_t>(i)][0],
                  t.p[static_cast<std::size_t>(i)][1]});
    }
  }
  for (int i = 0; i < 3; ++i) {
    const int    j  = (i + 1) % 3;
    const double di = d[i];
    const double dj = d[j];
    if (!((di > 0.0 && dj < 0.0) || (di < 0.0 && dj > 0.0))) continue;
    const double  s  = di / (di - dj);  // di - dj is strictly non-zero here
    const Vec3&   pi = t.p[static_cast<std::size_t>(i)];
    const Vec3&   pj = t.p[static_cast<std::size_t>(j)];
    push(Point2{pi[0] + s * (pj[0] - pi[0]), pi[1] + s * (pj[1] - pi[1])});
  }

  if (n_pts == 1) {
    // The plane grazes a single vertex (or a needle-thin sliver welded to one
    // point): no cross-section, and the loop through that vertex is carried by
    // the neighbouring triangles.
    ++ls->vertex_touches;
    return;
  }
  if (n_pts != 2) return;  // 0 members: triangle entirely on one side

  *out_a           = pts[0];
  *out_b           = pts[1];
  *out_has_segment = true;
}

void chain_layer(const std::vector<Point2>& raw_a,
                 const std::vector<Point2>& raw_b,
                 double                     weld_tol,
                 LayerSlice*                ls) {
  WeldGrid grid;
  grid.cell = (weld_tol > 0.0) ? weld_tol : 1e-12;
  grid.tol  = weld_tol;

  std::vector<WeldedSeg>                       segs;
  std::set<std::pair<std::uint32_t, std::uint32_t>> seen;
  segs.reserve(raw_a.size());
  for (std::size_t i = 0; i < raw_a.size(); ++i) {
    const std::uint32_t a = grid.weld(raw_a[i]);
    const std::uint32_t b = grid.weld(raw_b[i]);
    if (a == b) {
      ++ls->degenerate_dropped;
      continue;
    }
    const auto key = std::make_pair(std::min(a, b), std::max(a, b));
    if (!seen.insert(key).second) {
      ++ls->duplicate_dropped;
      continue;
    }
    segs.push_back(WeldedSeg{a, b});
  }
  if (segs.empty()) return;

  // Incidence lists are built in segment order, so the first unused entry is
  // always the lowest-index unused segment.
  std::vector<std::vector<std::uint32_t>> incident(grid.points.size());
  for (std::size_t s = 0; s < segs.size(); ++s) {
    incident[segs[s].a].push_back(static_cast<std::uint32_t>(s));
    incident[segs[s].b].push_back(static_cast<std::uint32_t>(s));
  }

  std::vector<char> used(segs.size(), 0);
  const auto pick = [&](std::uint32_t node) -> std::uint32_t {
    for (const std::uint32_t s : incident[node]) {
      if (!used[s]) return s;
    }
    return kNoSeg;
  };
  const auto other = [&](std::uint32_t s, std::uint32_t node) -> std::uint32_t {
    return segs[s].a == node ? segs[s].b : segs[s].a;
  };

  for (std::size_t s0 = 0; s0 < segs.size(); ++s0) {
    if (used[s0]) continue;
    used[s0] = 1;
    std::deque<std::uint32_t> chain;
    chain.push_back(segs[s0].a);
    chain.push_back(segs[s0].b);
    bool closed = false;

    while (true) {
      const std::uint32_t cur = chain.back();
      if (cur == chain.front()) {
        chain.pop_back();
        closed = true;
        break;
      }
      const std::uint32_t nxt = pick(cur);
      if (nxt == kNoSeg) break;
      used[nxt] = 1;
      chain.push_back(other(nxt, cur));
    }
    if (!closed) {
      while (true) {
        const std::uint32_t cur = chain.front();
        const std::uint32_t nxt = pick(cur);
        if (nxt == kNoSeg) break;
        used[nxt]             = 1;
        const std::uint32_t o = other(nxt, cur);
        if (o == chain.back()) {  // the walk met the far end
          closed = true;
          break;
        }
        chain.push_front(o);
      }
    }

    if (closed && chain.size() < 3) {
      ++ls->degenerate_dropped;
      continue;
    }
    if (chain.size() < 2) {
      ++ls->degenerate_dropped;
      continue;
    }

    Contour c;
    c.closed = closed;
    c.points.reserve(chain.size());
    for (const std::uint32_t id : chain) c.points.push_back(grid.points[id]);
    for (std::size_t i = 0; i + 1 < c.points.size(); ++i) {
      c.perimeter += distance(c.points[i], c.points[i + 1]);
    }
    if (closed) c.perimeter += distance(c.points.back(), c.points.front());
    c.signed_area = shoelace(c.points);
    if (!closed) ++ls->open_contours;
    ls->contours.push_back(std::move(c));
  }

  // Hole classification: even-odd containment depth of each closed contour's
  // first vertex inside the other closed contours.
  for (std::size_t i = 0; i < ls->contours.size(); ++i) {
    Contour& ci = ls->contours[i];
    if (!ci.closed) continue;
    std::size_t depth = 0;
    for (std::size_t j = 0; j < ls->contours.size(); ++j) {
      if (i == j || !ls->contours[j].closed) continue;
      if (point_in_ring(ci.points[0], ls->contours[j].points)) ++depth;
    }
    ci.hole = (depth % 2) == 1;

    // Outer contours CCW (positive area), holes CW — the CLI `dir` field and
    // every downstream even-odd fill rule expect that convention.
    const bool want_ccw = !ci.hole;
    if ((want_ccw && ci.signed_area < 0.0) || (!want_ccw && ci.signed_area > 0.0)) {
      std::reverse(ci.points.begin(), ci.points.end());
      ci.signed_area = -ci.signed_area;
    }
    // Rotate to the (min v, min u) vertex so the point list does not depend
    // on which segment the trace happened to start from.
    std::size_t start = 0;
    for (std::size_t k = 1; k < ci.points.size(); ++k) {
      if (ci.points[k].v < ci.points[start].v ||
          (ci.points[k].v == ci.points[start].v && ci.points[k].u < ci.points[start].u)) {
        start = k;
      }
    }
    if (start != 0) {
      std::rotate(ci.points.begin(),
                  ci.points.begin() + static_cast<std::ptrdiff_t>(start),
                  ci.points.end());
    }
  }

  std::stable_sort(ls->contours.begin(), ls->contours.end(), ContourLess{});
}

}  // namespace

bool build_frame(const Vec3& build_direction, Frame* out) {
  if (!out) return false;
  const double len = std::sqrt(dot(build_direction, build_direction));
  if (!(len > 1e-12)) return false;  // also rejects NaN
  Frame f;
  f.w = {build_direction[0] / len, build_direction[1] / len, build_direction[2] / len};
  // Seed the in-plane axes with the coordinate axis least aligned with w
  // (lowest index on a tie). For w = +Z the seed is +X, which makes
  // u = X and v = w x u = Y exactly — slice coordinates then *are* the
  // model's X/Y with no rotation applied at all.
  std::size_t seed = 0;
  for (std::size_t i = 1; i < 3; ++i) {
    if (std::abs(f.w[i]) < std::abs(f.w[seed])) seed = i;
  }
  Vec3 axis{0.0, 0.0, 0.0};
  axis[seed]        = 1.0;
  const double proj = dot(axis, f.w);
  Vec3         u{axis[0] - proj * f.w[0], axis[1] - proj * f.w[1], axis[2] - proj * f.w[2]};
  const double ulen = std::sqrt(dot(u, u));
  if (!(ulen > 1e-12)) return false;
  f.u = {u[0] / ulen, u[1] / ulen, u[2] / ulen};
  f.v = cross(f.w, f.u);
  *out = f;
  return true;
}

souxmar_status_t extract_boundary(const souxmar_mesh_t* mesh,
                                 const Frame&          frame,
                                 BoundaryMesh*         out) {
  if (!mesh) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "mesh is NULL");
  }
  if (!out) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "out boundary is NULL");
  }
  const std::size_t num_nodes = souxmar_mesh_num_nodes(mesh);
  const std::size_t num_cells = souxmar_mesh_num_cells(mesh);
  if (num_nodes == 0) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "mesh has no nodes");
  }
  std::size_t   flat_size = 0;
  const double* coords    = souxmar_mesh_nodes_flat(mesh, &flat_size);
  if (!coords || flat_size != num_nodes * 3) {
    return souxmar_status_error(SOUXMAR_E_INTERNAL,
                                "souxmar_mesh_nodes_flat returned an inconsistent buffer");
  }

  BoundaryMesh      bm;
  std::vector<Vec3> projected(num_nodes);
  for (std::size_t i = 0; i < num_nodes; ++i) {
    const Vec3 p{coords[i * 3 + 0], coords[i * 3 + 1], coords[i * 3 + 2]};
    if (!std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2])) {
      return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT,
                                  "mesh has a non-finite node coordinate");
    }
    projected[i] = {dot(p, frame.u), dot(p, frame.v), dot(p, frame.w)};
    if (i == 0) {
      bm.lo = projected[i];
      bm.hi = projected[i];
    } else {
      for (std::size_t k = 0; k < 3; ++k) {
        if (projected[i][k] < bm.lo[k]) bm.lo[k] = projected[i][k];
        if (projected[i][k] > bm.hi[k]) bm.hi[k] = projected[i][k];
      }
    }
  }

  // Does the mesh carry volume cells? If it does, its boundary is the set of
  // volume faces owned by exactly one cell and any stray shell cells are
  // ignored (they would double the surface). A pure shell mesh is its own
  // boundary.
  bool has_volume = false;
  for (std::size_t c = 0; c < num_cells; ++c) {
    if (topo_for(souxmar_mesh_cell_type(mesh, c)).volume) {
      has_volume = true;
      break;
    }
  }
  bm.from_shell = !has_volume;

  std::array<std::uint64_t, 27> cn{};  // Hex27 is the widest type we handle
  const auto load_cell = [&](std::size_t c, const ElemTopo& topo,
                             souxmar_status_t* st) -> bool {
    const std::size_t n = souxmar_mesh_cell_node_count(mesh, c);
    if (n < topo.corners || n > cn.size()) return false;
    const souxmar_status_t s = souxmar_mesh_cell_nodes(mesh, c, cn.data(), cn.size());
    if (s.code != SOUXMAR_OK) {
      *st = s;
      return false;
    }
    for (std::uint8_t k = 0; k < topo.corners; ++k) {
      if (cn[k] >= num_nodes) {
        *st = souxmar_status_error(SOUXMAR_E_INTERNAL,
                                   "cell references out-of-range node id");
        return false;
      }
    }
    return true;
  };

  if (has_volume) {
    std::map<FaceKey, std::uint32_t> face_count;
    for (std::size_t c = 0; c < num_cells; ++c) {
      const ElemTopo topo = topo_for(souxmar_mesh_cell_type(mesh, c));
      if (!topo.volume) continue;
      souxmar_status_t st = souxmar_status_ok();
      if (!load_cell(c, topo, &st)) {
        if (st.code != SOUXMAR_OK) return st;
        continue;
      }
      for (std::uint8_t f = 0; f < topo.face_count; ++f) {
        ++face_count[face_key(cn.data(), topo.faces[f])];
      }
    }
    for (const auto& entry : face_count) {
      if (entry.second > 1) ++bm.interior_faces;
    }
    // Second pass in cell/face index order so the facet list order depends
    // only on the mesh, never on map iteration.
    for (std::size_t c = 0; c < num_cells; ++c) {
      const ElemTopo topo = topo_for(souxmar_mesh_cell_type(mesh, c));
      if (!topo.volume) {
        // Shell / lower-dimensional cells riding along with a volume mesh are
        // ignored: adding them would double the surface the volume boundary
        // already provides.
        ++bm.unsupported_cells;
        continue;
      }
      souxmar_status_t st = souxmar_status_ok();
      if (!load_cell(c, topo, &st)) {
        if (st.code != SOUXMAR_OK) return st;
        ++bm.unsupported_cells;
        continue;
      }
      for (std::uint8_t f = 0; f < topo.face_count; ++f) {
        const auto it = face_count.find(face_key(cn.data(), topo.faces[f]));
        if (it == face_count.end() || it->second != 1) continue;
        emit_face(projected, cn.data(), topo.faces[f], &bm);
      }
    }
    // A volume mesh's one-owner face set is a closed surface by construction.
    bm.manifold = bm.triangles.empty() ? Manifold::Unknown : Manifold::Closed;
  } else {
    std::map<std::array<std::uint64_t, 2>, std::uint32_t> edge_count;
    bool audit = true;
    for (std::size_t c = 0; c < num_cells; ++c) {
      const ElemTopo topo = topo_for(souxmar_mesh_cell_type(mesh, c));
      if (!topo.shell) {
        ++bm.unsupported_cells;
        continue;
      }
      souxmar_status_t st = souxmar_status_ok();
      if (!load_cell(c, topo, &st)) {
        if (st.code != SOUXMAR_OK) return st;
        ++bm.unsupported_cells;
        continue;
      }
      emit_face(projected, cn.data(), topo.faces[0], &bm);
      if (bm.triangles.size() > kMaxShellAuditFacets) audit = false;
      if (!audit) continue;
      const LocalFace& lf = topo.faces[0];
      for (std::uint8_t k = 0; k < lf.n; ++k) {
        const std::uint64_t a = cn[lf.v[k]];
        const std::uint64_t b = cn[lf.v[(k + 1) % lf.n]];
        ++edge_count[{std::min(a, b), std::max(a, b)}];
      }
    }
    if (!audit || bm.triangles.empty()) {
      bm.manifold = Manifold::Unknown;
    } else {
      bm.manifold = Manifold::Closed;
      for (const auto& entry : edge_count) {
        if (entry.second != 2) {
          bm.manifold = Manifold::Open;
          break;
        }
      }
    }
  }

  *out = std::move(bm);
  return souxmar_status_ok();
}

double enclosed_volume(const BoundaryMesh& boundary) {
  double acc = 0.0;
  for (const Triangle& t : boundary.triangles) {
    const Vec3 c = cross(t.p[1], t.p[2]);
    acc += dot(t.p[0], c);
  }
  return acc / 6.0;
}

Tolerances tolerances_for(const BoundaryMesh& boundary) {
  double diag2 = 0.0;
  for (std::size_t k = 0; k < 3; ++k) {
    const double d = boundary.hi[k] - boundary.lo[k];
    diag2 += d * d;
  }
  double diag = std::sqrt(diag2);
  if (!(diag > 0.0)) diag = 1.0;  // degenerate box: fall back to 1 m
  Tolerances t;
  // 1e-9 * diagonal ≈ 35 pm for a 35 mm part: three orders of magnitude above
  // double round-off on the coordinates (~1e-16 * diagonal) and nine orders
  // below any manufacturable feature.
  t.plane_eps = std::max(1e-15, 1e-9 * diag);
  // 1e-6 * diagonal ≈ 35 nm for a 35 mm part: generous enough to weld the two
  // roundings of the same shared-edge crossing, far under one machine step.
  t.weld_tol = std::max(1e-12, 1e-6 * diag);
  return t;
}

SliceSet slice_uniform(const BoundaryMesh& boundary,
                       double              h0,
                       double              dh,
                       std::size_t         num_layers,
                       const Tolerances&   tol) {
  SliceSet out;
  out.h0 = h0;
  out.dh = dh;
  if (num_layers == 0 || !(dh > 0.0)) return out;
  out.layers.resize(num_layers);
  for (std::size_t k = 0; k < num_layers; ++k) {
    out.layers[k].h = h0 + static_cast<double>(k) * dh;
  }
  if (boundary.triangles.empty()) return out;

  // Sweep order: triangles sorted by (min h, index). The active list holds the
  // triangles whose h-extent straddles the current plane; removal is a stable
  // in-place compaction, so the active list — and therefore the per-layer
  // segment order — is a pure function of the mesh.
  const std::size_t   n_tri = boundary.triangles.size();
  std::vector<double> h_lo(n_tri);
  std::vector<double> h_hi(n_tri);
  for (std::size_t i = 0; i < n_tri; ++i) {
    const Triangle& t = boundary.triangles[i];
    h_lo[i] = std::min(t.p[0][2], std::min(t.p[1][2], t.p[2][2])) - tol.plane_eps;
    h_hi[i] = std::max(t.p[0][2], std::max(t.p[1][2], t.p[2][2])) + tol.plane_eps;
  }
  std::vector<std::uint32_t> order(n_tri);
  for (std::size_t i = 0; i < n_tri; ++i) order[i] = static_cast<std::uint32_t>(i);
  std::stable_sort(order.begin(), order.end(),
                   [&](std::uint32_t a, std::uint32_t b) {
                     if (h_lo[a] != h_lo[b]) return h_lo[a] < h_lo[b];
                     return a < b;
                   });

  std::vector<std::uint32_t> active;
  std::size_t                cursor = 0;
  std::vector<Point2>        seg_a;
  std::vector<Point2>        seg_b;

  for (std::size_t k = 0; k < num_layers; ++k) {
    const double h = out.layers[k].h;
    while (cursor < n_tri && h_lo[order[cursor]] <= h) {
      active.push_back(order[cursor]);
      ++cursor;
    }
    active.erase(std::remove_if(active.begin(), active.end(),
                                [&](std::uint32_t i) { return h_hi[i] < h; }),
                 active.end());

    seg_a.clear();
    seg_b.clear();
    LayerSlice& ls = out.layers[k];
    for (const std::uint32_t i : active) {
      Point2 a{};
      Point2 b{};
      bool   has = false;
      intersect_triangle(boundary.triangles[i], h, tol.plane_eps, tol.weld_tol,
                         &ls, &a, &b, &has);
      if (!has) continue;
      seg_a.push_back(a);
      seg_b.push_back(b);
    }
    chain_layer(seg_a, seg_b, tol.weld_tol, &ls);

    out.coplanar_triangles += ls.coplanar_triangles;
    out.vertex_touches += ls.vertex_touches;
    out.degenerate_dropped += ls.degenerate_dropped;
    out.duplicate_dropped += ls.duplicate_dropped;
    out.open_contours += ls.open_contours;
    for (const Contour& c : ls.contours) {
      if (c.closed) ++out.closed_contours;
    }
  }
  return out;
}

void append_fixed(std::string* out, double value, int decimals) {
  if (!out) return;
  const int d = std::clamp(decimals, 0, 12);
  if (!std::isfinite(value)) {
    // "nan" / "inf" / "-nan(ind)" all differ between C libraries; a
    // non-finite number never means anything useful in a machine file, so it
    // is written as zero. Callers validate their inputs up front.
    out->append("0");
    return;
  }
  // 1e15 keeps the %.*f expansion inside the buffer (16 integer digits + 12
  // decimals + sign + dot + NUL = 31).
  const double v = std::clamp(value, -1e15, 1e15);
  char         buf[64];
  const int    n = std::snprintf(buf, sizeof buf, "%.*f", d, v);
  if (n <= 0 || static_cast<std::size_t>(n) >= sizeof buf) {
    out->append("0");
    return;
  }
  bool any_nonzero_digit = false;
  for (int i = 0; i < n; ++i) {
    if (buf[i] == ',') buf[i] = '.';  // localised radix, if LC_NUMERIC bites
    if (buf[i] >= '1' && buf[i] <= '9') any_nonzero_digit = true;
  }
  // "-0.000" vs "0.000" is a real cross-platform difference for tiny negative
  // values; normalise the sign away when every digit is zero.
  const char* start = (!any_nonzero_digit && buf[0] == '-') ? buf + 1 : buf;
  out->append(start);
}

std::string format_fixed(double value, int decimals) {
  std::string s;
  append_fixed(&s, value, decimals);
  return s;
}

}  // namespace souxmar_am_slicer
