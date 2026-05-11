// SPDX-License-Identifier: Apache-2.0
//
// Pins the mesh-quality math for Tet4 and Tri3 across the cases the
// in-tree plugins and agent tools depend on. Regressions here trip
// before the integration test catches them.

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <limits>
#include <vector>

#include "souxmar/core/mesh_quality.h"

using souxmar::core::ElementType;
using souxmar::core::quality::evaluate;
using souxmar::core::quality::evaluate_all;
using souxmar::core::quality::kNumMetrics;
using souxmar::core::quality::Metric;
using souxmar::core::quality::summarise;
using Vec3 = std::array<double, 3>;

namespace {

constexpr double kRegularTetDihedralDeg = 70.528779365509308;  // arccos(1/3)

// Regular tetrahedron, vertex 3 above the (0,1,2) base; positively
// oriented so signed volume > 0.
std::vector<Vec3> regular_tet() {
  // Edge length 1 by construction.
  return {
      {0.0, 0.0, 0.0},
      {1.0, 0.0, 0.0},
      {0.5, std::sqrt(3.0) / 2.0, 0.0},
      {0.5, std::sqrt(3.0) / 6.0, std::sqrt(2.0 / 3.0)},
  };
}

}  // namespace

TEST(MeshQuality, RegularTetHasExpectedMetrics) {
  const auto nodes = regular_tet();
  EXPECT_GT(evaluate(ElementType::Tet4, Metric::SignedVolume, nodes), 0.0);
  // EdgeRatio is exactly 1 for a regular tet.
  EXPECT_NEAR(evaluate(ElementType::Tet4, Metric::EdgeRatio, nodes), 1.0, 1e-10);
  // Min dihedral of a regular tet is arccos(1/3) ≈ 70.529°.
  EXPECT_NEAR(evaluate(ElementType::Tet4, Metric::MinDihedralDeg, nodes),
              kRegularTetDihedralDeg, 1e-6);
}

TEST(MeshQuality, InvertedTetReportsNegativeVolume) {
  // Swap two vertices to flip the orientation.
  auto nodes = regular_tet();
  std::swap(nodes[1], nodes[2]);
  EXPECT_LT(evaluate(ElementType::Tet4, Metric::SignedVolume, nodes), 0.0);
  // Edge ratio + dihedral unaffected by orientation.
  EXPECT_NEAR(evaluate(ElementType::Tet4, Metric::EdgeRatio, nodes), 1.0, 1e-10);
}

TEST(MeshQuality, SliverTetHasSmallMinDihedral) {
  // Almost-coplanar tet: vertex 3 only a hair above the base plane.
  std::vector<Vec3> nodes = {
      {0.0, 0.0, 0.0},
      {1.0, 0.0, 0.0},
      {0.5, std::sqrt(3.0) / 2.0, 0.0},
      {0.5, std::sqrt(3.0) / 6.0, 1e-4},
  };
  const double dih = evaluate(ElementType::Tet4, Metric::MinDihedralDeg, nodes);
  EXPECT_LT(dih, 1.0);  // well under the 5° sliver threshold
  EXPECT_GT(dih, 0.0);
}

TEST(MeshQuality, StretchedTetHasHighEdgeRatio) {
  std::vector<Vec3> nodes = {
      {0.0, 0.0, 0.0},
      {100.0, 0.0, 0.0},
      {0.5, 1.0, 0.0},
      {0.5, 0.5, 0.5},
  };
  EXPECT_GT(evaluate(ElementType::Tet4, Metric::EdgeRatio, nodes), 50.0);
}

TEST(MeshQuality, Tri3AreaAndEdgeRatio) {
  std::vector<Vec3> nodes = {
      {0.0, 0.0, 0.0},
      {1.0, 0.0, 0.0},
      {0.0, 1.0, 0.0},
  };
  // Right triangle: area = 0.5; edge_ratio = sqrt(2).
  EXPECT_NEAR(evaluate(ElementType::Tri3, Metric::SignedVolume, nodes), 0.5, 1e-10);
  EXPECT_NEAR(evaluate(ElementType::Tri3, Metric::EdgeRatio, nodes),
              std::sqrt(2.0), 1e-10);
  // Tri3 has no dihedral.
  EXPECT_TRUE(std::isnan(
      evaluate(ElementType::Tri3, Metric::MinDihedralDeg, nodes)));
}

TEST(MeshQuality, UnsupportedTypeReturnsNaN) {
  std::vector<Vec3> nodes(8, Vec3{0, 0, 0});
  EXPECT_TRUE(std::isnan(evaluate(ElementType::Hex8, Metric::SignedVolume, nodes)));
  EXPECT_TRUE(std::isnan(evaluate(ElementType::Hex8, Metric::EdgeRatio, nodes)));
  EXPECT_TRUE(std::isnan(evaluate(ElementType::Hex8, Metric::MinDihedralDeg, nodes)));
}

TEST(MeshQuality, EvaluateAllMatchesIndividualEvaluate) {
  const auto nodes = regular_tet();
  std::array<double, kNumMetrics> all{};
  evaluate_all(ElementType::Tet4, nodes, all);
  EXPECT_EQ(all[0], evaluate(ElementType::Tet4, Metric::SignedVolume,   nodes));
  EXPECT_EQ(all[1], evaluate(ElementType::Tet4, Metric::EdgeRatio,      nodes));
  EXPECT_EQ(all[2], evaluate(ElementType::Tet4, Metric::MinDihedralDeg, nodes));
}

TEST(MeshQuality, DegenerateTetReturnsNaNForRatioAndDihedral) {
  // All four nodes coincident.
  std::vector<Vec3> nodes(4, Vec3{0, 0, 0});
  // Volume is 0, not NaN.
  EXPECT_EQ(evaluate(ElementType::Tet4, Metric::SignedVolume, nodes), 0.0);
  // Edge ratio: lmin == 0 → NaN.
  EXPECT_TRUE(std::isnan(evaluate(ElementType::Tet4, Metric::EdgeRatio, nodes)));
  // Dihedral: also NaN (any edge has zero length).
  EXPECT_TRUE(std::isnan(evaluate(ElementType::Tet4, Metric::MinDihedralDeg, nodes)));
}

TEST(MeshQualitySummarise, EmptyDataYieldsZeroFiniteCount) {
  std::vector<double> data;
  const auto r = summarise(data, 0);
  for (const auto& s : r.per_metric) {
    EXPECT_EQ(s.finite_count, 0u);
    EXPECT_EQ(s.total_count,  0u);
  }
  EXPECT_EQ(r.cells_inverted,        0u);
  EXPECT_EQ(r.cells_sliver_dihedral, 0u);
  EXPECT_EQ(r.cells_extreme_aspect,  0u);
  EXPECT_EQ(r.cells_unsupported,     0u);
}

TEST(MeshQualitySummarise, FlagsCellsByThreshold) {
  // 3 cells: { good, inverted-and-sliver, unsupported-NaN }
  std::vector<double> data{
      // good
      0.1666,  1.0,   70.5,
      // inverted + sliver + extreme aspect
     -0.0001, 25.0,    3.0,
      // unsupported (NaN for one metric)
      std::numeric_limits<double>::quiet_NaN(), 2.0, 60.0,
  };
  const auto r = summarise(data, 3);
  EXPECT_EQ(r.cells_inverted,        1u);
  EXPECT_EQ(r.cells_sliver_dihedral, 1u);
  EXPECT_EQ(r.cells_extreme_aspect,  1u);
  EXPECT_EQ(r.cells_unsupported,     1u);

  // Volume metric: 2 finite values (-0.0001, 0.1666), 1 NaN-skipped.
  const auto& vol = r.per_metric[
      static_cast<std::size_t>(Metric::SignedVolume)];
  EXPECT_EQ(vol.total_count,  3u);
  EXPECT_EQ(vol.finite_count, 2u);
  EXPECT_NEAR(vol.min, -0.0001, 1e-12);
  EXPECT_NEAR(vol.max,  0.1666, 1e-12);
}
