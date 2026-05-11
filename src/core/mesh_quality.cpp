// SPDX-License-Identifier: Apache-2.0

#include "souxmar/core/mesh_quality.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace souxmar::core::quality {

namespace {

using Vec3 = std::array<double, 3>;

constexpr double kPi = 3.14159265358979323846;
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

inline Vec3 sub(const Vec3& a, const Vec3& b) noexcept {
  return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}

inline double dot(const Vec3& a, const Vec3& b) noexcept {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

inline Vec3 cross(const Vec3& a, const Vec3& b) noexcept {
  return {a[1] * b[2] - a[2] * b[1],
          a[2] * b[0] - a[0] * b[2],
          a[0] * b[1] - a[1] * b[0]};
}

inline double norm(const Vec3& a) noexcept {
  return std::sqrt(dot(a, a));
}

// Signed volume of a tet (0,1,2,3): det([n1-n0; n2-n0; n3-n0]) / 6.
// Sign follows the right-hand rule: positive when (n1-n0, n2-n0, n3-n0)
// forms a right-handed basis. The hello-mesher emits a positively-
// oriented tet so this is the "good" sign convention.
double tet_signed_volume(const Vec3& n0, const Vec3& n1,
                         const Vec3& n2, const Vec3& n3) noexcept {
  const Vec3 e1 = sub(n1, n0);
  const Vec3 e2 = sub(n2, n0);
  const Vec3 e3 = sub(n3, n0);
  // det = e1 . (e2 x e3)
  return dot(e1, cross(e2, e3)) / 6.0;
}

// Signed area of a tri (0,1,2) in 3D: half the z-projection of the
// cross product — but we don't have a canonical normal direction in 3D,
// so we report the unsigned area magnitude (≥ 0). The metric id stays
// "SignedVolume" for uniformity; documented in the header.
double tri_area(const Vec3& n0, const Vec3& n1, const Vec3& n2) noexcept {
  return 0.5 * norm(cross(sub(n1, n0), sub(n2, n0)));
}

// All 6 edge lengths of a tet, fed into the EdgeRatio metric.
// Edge ordering doesn't matter — we only need min/max.
void tet_edge_lengths(const Vec3& n0, const Vec3& n1, const Vec3& n2,
                      const Vec3& n3, std::array<double, 6>& out) noexcept {
  out[0] = norm(sub(n1, n0));
  out[1] = norm(sub(n2, n0));
  out[2] = norm(sub(n3, n0));
  out[3] = norm(sub(n2, n1));
  out[4] = norm(sub(n3, n1));
  out[5] = norm(sub(n3, n2));
}

void tri_edge_lengths(const Vec3& n0, const Vec3& n1, const Vec3& n2,
                      std::array<double, 3>& out) noexcept {
  out[0] = norm(sub(n1, n0));
  out[1] = norm(sub(n2, n1));
  out[2] = norm(sub(n0, n2));
}

template <std::size_t N>
double edge_ratio_from(const std::array<double, N>& lens) noexcept {
  double lmin = lens[0];
  double lmax = lens[0];
  for (std::size_t i = 1; i < N; ++i) {
    if (lens[i] < lmin) lmin = lens[i];
    if (lens[i] > lmax) lmax = lens[i];
  }
  if (!(lmin > 0.0)) return kNaN;
  return lmax / lmin;
}

// Dihedral angle along edge (a, b) of a tet whose other two vertices
// are c, d.
//
// We use the orthogonal-projection construction: project (c - a) and
// (d - a) onto the plane perpendicular to (b - a) and take the angle
// between the projections. This is well-conditioned for almost-
// degenerate edges as long as the edge itself has positive length, and
// avoids the sign ambiguity of the cross-product normal approach.
double tet_dihedral_along_edge_rad(const Vec3& a, const Vec3& b,
                                   const Vec3& c, const Vec3& d) noexcept {
  const Vec3   u    = sub(b, a);
  const double uu   = dot(u, u);
  if (!(uu > 0.0)) return kNaN;

  const Vec3   v1   = sub(c, a);
  const Vec3   v2   = sub(d, a);

  const double k1   = dot(v1, u) / uu;
  const double k2   = dot(v2, u) / uu;

  const Vec3   p1{v1[0] - k1 * u[0], v1[1] - k1 * u[1], v1[2] - k1 * u[2]};
  const Vec3   p2{v2[0] - k2 * u[0], v2[1] - k2 * u[1], v2[2] - k2 * u[2]};

  const double n1   = norm(p1);
  const double n2   = norm(p2);
  if (!(n1 > 0.0) || !(n2 > 0.0)) return kNaN;

  double cos_t = dot(p1, p2) / (n1 * n2);
  if (cos_t >  1.0) cos_t =  1.0;
  if (cos_t < -1.0) cos_t = -1.0;
  return std::acos(cos_t);
}

double tet_min_dihedral_rad(const Vec3& n0, const Vec3& n1,
                            const Vec3& n2, const Vec3& n3) noexcept {
  // (edge_a, edge_b, opp_c, opp_d) for each of the 6 edges of a tet.
  const std::array<std::array<int, 4>, 6> edges_with_opposites = {{
      {0, 1, 2, 3},
      {0, 2, 1, 3},
      {0, 3, 1, 2},
      {1, 2, 0, 3},
      {1, 3, 0, 2},
      {2, 3, 0, 1},
  }};
  const std::array<const Vec3*, 4> p{&n0, &n1, &n2, &n3};
  double min_rad = std::numeric_limits<double>::infinity();
  for (const auto& e : edges_with_opposites) {
    const double r = tet_dihedral_along_edge_rad(*p[e[0]], *p[e[1]],
                                                 *p[e[2]], *p[e[3]]);
    if (std::isnan(r)) return kNaN;
    if (r < min_rad) min_rad = r;
  }
  return min_rad;
}

}  // namespace

double evaluate(ElementType                            type,
                Metric                                 metric,
                std::span<const Vec3>                  nodes_xyz) noexcept {
  switch (type) {
    case ElementType::Tet4: {
      if (nodes_xyz.size() < 4) return kNaN;
      const auto& n0 = nodes_xyz[0];
      const auto& n1 = nodes_xyz[1];
      const auto& n2 = nodes_xyz[2];
      const auto& n3 = nodes_xyz[3];
      switch (metric) {
        case Metric::SignedVolume:
          return tet_signed_volume(n0, n1, n2, n3);
        case Metric::EdgeRatio: {
          std::array<double, 6> lens{};
          tet_edge_lengths(n0, n1, n2, n3, lens);
          return edge_ratio_from(lens);
        }
        case Metric::MinDihedralDeg: {
          const double rad = tet_min_dihedral_rad(n0, n1, n2, n3);
          return std::isnan(rad) ? kNaN : rad * (180.0 / kPi);
        }
      }
      return kNaN;
    }
    case ElementType::Tri3: {
      if (nodes_xyz.size() < 3) return kNaN;
      const auto& n0 = nodes_xyz[0];
      const auto& n1 = nodes_xyz[1];
      const auto& n2 = nodes_xyz[2];
      switch (metric) {
        case Metric::SignedVolume:
          return tri_area(n0, n1, n2);
        case Metric::EdgeRatio: {
          std::array<double, 3> lens{};
          tri_edge_lengths(n0, n1, n2, lens);
          return edge_ratio_from(lens);
        }
        case Metric::MinDihedralDeg:
          return kNaN;  // 2D element has no dihedral
      }
      return kNaN;
    }
    default:
      // Hex8, Quad4, Pyramid5, Prism6, higher-order — not in v1.
      return kNaN;
  }
}

void evaluate_all(ElementType                            type,
                  std::span<const Vec3>                  nodes_xyz,
                  std::span<double>                      out) noexcept {
  if (out.size() < kNumMetrics) return;
  // Single dispatch on type lets us share edge-length / dihedral work
  // when adding more metrics later. Today the three metrics are
  // independent so the savings are modest — but the API shape is the
  // one we want long-term.
  out[static_cast<std::size_t>(Metric::SignedVolume)] =
      evaluate(type, Metric::SignedVolume,   nodes_xyz);
  out[static_cast<std::size_t>(Metric::EdgeRatio)] =
      evaluate(type, Metric::EdgeRatio,      nodes_xyz);
  out[static_cast<std::size_t>(Metric::MinDihedralDeg)] =
      evaluate(type, Metric::MinDihedralDeg, nodes_xyz);
}

Report summarise(std::span<const double> data,
                 std::size_t             num_cells) noexcept {
  Report r{};
  if (data.size() < num_cells * kNumMetrics) return r;

  std::array<double, kNumMetrics> sum{};
  for (auto& s : r.per_metric) s.total_count = num_cells;

  for (std::size_t cell = 0; cell < num_cells; ++cell) {
    bool any_nan = false;
    for (std::size_t m = 0; m < kNumMetrics; ++m) {
      const double v = data[cell * kNumMetrics + m];
      if (std::isnan(v)) {
        any_nan = true;
        continue;
      }
      auto& s = r.per_metric[m];
      if (s.finite_count == 0) {
        s.min = v;
        s.max = v;
      } else {
        if (v < s.min) s.min = v;
        if (v > s.max) s.max = v;
      }
      sum[m] += v;
      ++s.finite_count;
    }
    if (any_nan) ++r.cells_unsupported;

    // Threshold counters use the raw per-cell values (no smoothing).
    const double vol  = data[cell * kNumMetrics + 0];
    const double er   = data[cell * kNumMetrics + 1];
    const double dih  = data[cell * kNumMetrics + 2];
    if (!std::isnan(vol) && vol < 0.0)            ++r.cells_inverted;
    if (!std::isnan(er)  && er  > 20.0)           ++r.cells_extreme_aspect;
    if (!std::isnan(dih) && (dih < 5.0 || dih > 175.0))
      ++r.cells_sliver_dihedral;
  }

  for (std::size_t m = 0; m < kNumMetrics; ++m) {
    auto& s = r.per_metric[m];
    if (s.finite_count > 0) {
      s.mean = sum[m] / static_cast<double>(s.finite_count);
    }
  }
  return r;
}

}  // namespace souxmar::core::quality
