// SPDX-License-Identifier: Apache-2.0
//
// am-slicer / slicer.hpp — planar slicing + contour chaining, shared by
// `writer.am.gcode` and `writer.am.cli` (the two capabilities that need a
// layer cross-section) and reused by `writer.am.report` for its
// boundary/volume summary. Header-only declarations; the implementation is
// in slicer.cpp. Nothing here touches the filesystem or the value bag —
// that is am_slicer.cpp's job.
//
// What it computes:
//   1. An orthonormal slicing frame (u, v, w), w = normalise(build_direction).
//      For the default [0, 0, 1] the frame is exactly (X, Y, Z), so slice
//      coordinates are the model's own X/Y and layer height is Z.
//   2. The triangulated boundary of the mesh:
//        * volume meshes (Tet4/Hex8/Prism6/Pyramid5 and their quadratic
//          siblings, corner nodes only) are reduced to the faces that
//          exactly one cell owns — the classic "face key occurs once"
//          boundary test. The key is the *sorted* vector of the face's
//          global node ids, so the test is orientation-independent, and it
//          lives in a std::map so no unordered-container iteration order can
//          leak into the output (determinism gate);
//        * shell meshes (Tri3/Quad4 and siblings) are used as-is;
//        * quads split on the fixed diagonal v0-v2: (v0,v1,v2) + (v0,v2,v3).
//      Local face tables are copied from src/core/face_topology.cpp so the
//      winding matches the rest of souxmar (CCW seen from outside the cell).
//   3. Per layer, the intersection of every boundary triangle with the plane
//      h = const. With vertex heights d_i = h_i - h_plane, snapped to exactly
//      zero inside `plane_eps`, the crossing set is
//        {vertex i : d_i == 0} ∪ {edge (i,j) : d_i, d_j strictly opposite},
//      the edge crossing evaluated at t = d_i / (d_i - d_j). That set has 0,
//      1 or 2 distinct members for a triangle, which is what makes the
//      pathological cases fall out of the general rule instead of needing
//      special cases:
//        * triangle wholly in the plane (3 zeros) → skipped and counted; its
//          bounding edges are contributed by the neighbouring triangles;
//        * single-vertex touch (1 zero, other two on the same side) → one
//          member → dropped and counted;
//        * an edge lying in the plane (2 zeros) → that edge is emitted, which
//          is exactly the cross-section of a flat face the plane grazes. Both
//          triangles sharing the edge emit it; the duplicate is dropped
//          during chaining;
//        * a vertex the plane passes through (1 zero, one +, one −) → the
//          vertex plus the opposite-edge crossing → a proper segment.
//   4. Chaining: segment endpoints are welded onto a shared point set using a
//      uniform grid of cell size `weld_tol` with a 3x3 neighbourhood search
//      (so any two endpoints within the tolerance weld, and the grid is a
//      std::map — no hash iteration). Zero-length and duplicate segments are
//      dropped and counted. Loops are then traced greedily, always taking the
//      lowest-index unused incident segment, so the trace depends on nothing
//      but the input ordering. A chain that returns to its start is a closed
//      contour; anything else is reported as an open contour (callers print
//      those as a comment in the output rather than failing).
//   5. Closed contours get hole classification by even-odd containment of
//      their first vertex, are oriented CCW (outer) / CW (hole), are rotated
//      to start at their (min v, min u) vertex, and all contours are then
//      sorted by (min v, min u at that v, point count, coordinates) — a total
//      order, so the emitted file is byte-stable.
//
// What this is NOT: a production slicer. There is no adaptive slicing, no
// self-intersection or watertightness repair (a leaky mesh yields open
// contours, which are reported, not fixed), no non-manifold disambiguation (a
// figure-eight junction is split arbitrarily, though deterministically), no
// curvature handling for quadratic elements (Tet10/Hex20/... contribute their
// corner-node planar facets only), and no toolpath optimisation beyond
// alternating the scan direction of consecutive infill lines. Contours are not
// simplified either: a contour keeps one vertex per crossed facet edge, so a
// flat wall built from many facets yields many collinear vertices. That is
// faithful to the mesh and costs a few bytes of output; it is not a bug.

#ifndef SOUXMAR_EXAMPLES_AM_SLICER_SLICER_HPP
#define SOUXMAR_EXAMPLES_AM_SLICER_SLICER_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "souxmar-c/mesh.h"
#include "souxmar-c/status.h"

namespace souxmar_am_slicer {

using Vec3 = std::array<double, 3>;

struct Point2 {
  double u = 0.0;
  double v = 0.0;
};

// Right-handed orthonormal slicing frame. `w` is the normalised build
// direction; (u, v) span the layer plane.
struct Frame {
  Vec3 u{1.0, 0.0, 0.0};
  Vec3 v{0.0, 1.0, 0.0};
  Vec3 w{0.0, 0.0, 1.0};
};

// Builds the frame from an arbitrary build direction. Returns false when the
// direction has (near) zero length — callers turn that into
// SOUXMAR_E_INVALID_ARGUMENT per contract §2.1.
bool build_frame(const Vec3& build_direction, Frame* out);

// A boundary facet expressed in frame coordinates: p[k] = {u, v, h}. Winding
// is inherited from the source face table, so for a watertight volume mesh the
// triangle normal points out of the solid.
struct Triangle {
  std::array<Vec3, 3> p{};
};

// Tri-state manifold verdict for the extracted boundary. `Closed` for a
// volume-mesh boundary (closed by construction from the face-count test) and
// for a shell whose every edge is shared by exactly two facets; `Open` for a
// leaky shell; `Unknown` when the shell was too large for the edge audit.
enum class Manifold : std::uint8_t { Unknown = 0, Closed = 1, Open = 2 };

struct BoundaryMesh {
  std::vector<Triangle> triangles;
  Vec3                  lo{0.0, 0.0, 0.0};  // node bbox in frame coords
  Vec3                  hi{0.0, 0.0, 0.0};
  bool                  from_shell        = false;  // Tri3/Quad4 input
  std::size_t           quads_split       = 0;
  std::size_t           interior_faces    = 0;
  std::size_t           unsupported_cells = 0;
  Manifold              manifold          = Manifold::Unknown;
};

// Extracts the triangulated boundary. Succeeds with an empty triangle list
// for meshes that have no facets at all (e.g. an Edge2 lattice) — the slicing
// writers reject that, the report writer reports it.
souxmar_status_t extract_boundary(const souxmar_mesh_t* mesh,
                                 const Frame&          frame,
                                 BoundaryMesh*         out);

// Signed volume enclosed by the boundary triangulation,
// V = (1/6) * sum over facets of p0 . (p1 x p2). Exact for the piecewise-flat
// triangulation that extract_boundary emits; positive for outward winding.
// Meaningless unless `manifold == Manifold::Closed`.
double enclosed_volume(const BoundaryMesh& boundary);

struct Contour {
  std::vector<Point2> points;             // no repeated closing point
  bool                closed      = false;
  bool                hole        = false;  // odd containment depth
  double              perimeter   = 0.0;    // m, closing edge included when closed
  double              signed_area = 0.0;    // m^2, positive = CCW
};

struct LayerSlice {
  double               h = 0.0;  // plane height in frame coords
  std::vector<Contour> contours;
  std::size_t          open_contours      = 0;
  std::size_t          coplanar_triangles = 0;
  std::size_t          vertex_touches     = 0;
  std::size_t          degenerate_dropped = 0;
  std::size_t          duplicate_dropped  = 0;
};

struct Tolerances {
  double plane_eps = 1e-12;  // |h - plane| under this counts as in-plane
  double weld_tol  = 1e-12;  // endpoint welding radius when chaining
};

// Model-size-relative tolerances; see slicer.cpp for the constants.
Tolerances tolerances_for(const BoundaryMesh& boundary);

struct SliceSet {
  double                  h0 = 0.0;  // height of plane 0
  double                  dh = 0.0;  // plane spacing
  std::vector<LayerSlice> layers;
  std::size_t             closed_contours    = 0;
  std::size_t             open_contours      = 0;
  std::size_t             coplanar_triangles = 0;
  std::size_t             vertex_touches     = 0;
  std::size_t             degenerate_dropped = 0;
  std::size_t             duplicate_dropped  = 0;
};

// Slices `num_layers` planes at h0 + k*dh. Triangles are visited through an
// ordered sweep (sorted by (min h, triangle index), stable in-place removal),
// so the cost is O(sum of layer spans) with O(triangle count) extra memory and
// a deterministic per-layer segment order.
SliceSet slice_uniform(const BoundaryMesh& boundary,
                       double              h0,
                       double              dh,
                       std::size_t         num_layers,
                       const Tolerances&   tol);

// ---- Locale-independent fixed-point formatting -------------------------
//
// Every number that reaches a file goes through these. snprintf("%.*f") is
// used with an explicit precision (never "%g", never the default
// iostream/locale float formatting) and the result is scrubbed of a localised
// radix character and of a "-0.000" sign, because a plugin must not call
// setlocale (that is global, host-visible state). No scientific notation ever
// appears, so the byte stream is identical on every platform.
void        append_fixed(std::string* out, double value, int decimals);
std::string format_fixed(double value, int decimals);

}  // namespace souxmar_am_slicer

#endif  // SOUXMAR_EXAMPLES_AM_SLICER_SLICER_HPP
