// SPDX-License-Identifier: Apache-2.0

#include "souxmar/core/element_type.h"

#include <gtest/gtest.h>

using namespace souxmar::core;

namespace {

TEST(ElementType, NumericValuesAreStable) {
  // ABI / on-disk-format contract: these numeric values may not change.
  EXPECT_EQ(static_cast<int>(ElementType::Tet4), 9);
  EXPECT_EQ(static_cast<int>(ElementType::Hex8), 11);
  EXPECT_EQ(static_cast<int>(ElementType::Tri3), 4);
  EXPECT_EQ(static_cast<int>(ElementType::Edge2), 2);
  EXPECT_EQ(static_cast<int>(ElementType::Vertex), 1);
}

TEST(ElementType, NodeCountsMatchGeometry) {
  EXPECT_EQ(num_nodes(ElementType::Tet4), 4);
  EXPECT_EQ(num_nodes(ElementType::Tet10), 10);
  EXPECT_EQ(num_nodes(ElementType::Hex8), 8);
  EXPECT_EQ(num_nodes(ElementType::Hex20), 20);
  EXPECT_EQ(num_nodes(ElementType::Hex27), 27);
  EXPECT_EQ(num_nodes(ElementType::Prism6), 6);
  EXPECT_EQ(num_nodes(ElementType::Pyramid5), 5);
}

TEST(ElementType, DimensionByElementClass) {
  EXPECT_EQ(dimension(ElementType::Vertex), 0);
  EXPECT_EQ(dimension(ElementType::Edge2), 1);
  EXPECT_EQ(dimension(ElementType::Tri3), 2);
  EXPECT_EQ(dimension(ElementType::Quad4), 2);
  EXPECT_EQ(dimension(ElementType::Tet4), 3);
  EXPECT_EQ(dimension(ElementType::Hex8), 3);
}

TEST(ElementType, OrderByLinearOrQuadratic) {
  EXPECT_EQ(order(ElementType::Tet4), 1);
  EXPECT_EQ(order(ElementType::Tet10), 2);
  EXPECT_EQ(order(ElementType::Hex8), 1);
  EXPECT_EQ(order(ElementType::Hex20), 2);
}

TEST(ElementType, NamesAreCanonical) {
  EXPECT_EQ(name(ElementType::Tet4), "tet4");
  EXPECT_EQ(name(ElementType::Hex27), "hex27");
}

TEST(ElementType, UnknownIsZero) {
  EXPECT_EQ(static_cast<int>(ElementType::Unknown), 0);
  EXPECT_EQ(num_nodes(ElementType::Unknown), 0);
}

}  // namespace
