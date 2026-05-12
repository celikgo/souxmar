// SPDX-License-Identifier: Apache-2.0

#include "souxmar/core/topology.h"

#include <gtest/gtest.h>

#include <stdexcept>

using namespace souxmar::core;

namespace {

TEST(Topology, FreshIsEmpty) {
  Topology t;
  EXPECT_TRUE(t.empty());
  for (auto k :
       {TopologyKind::Vertex, TopologyKind::Edge, TopologyKind::Face, TopologyKind::Region}) {
    EXPECT_EQ(t.count(k), 0u);
  }
}

TEST(Topology, AddPerKindIndependent) {
  Topology t;
  EXPECT_EQ(t.add_entity(TopologyKind::Face), 0u);
  EXPECT_EQ(t.add_entity(TopologyKind::Face), 1u);
  EXPECT_EQ(t.add_entity(TopologyKind::Edge), 0u);  // separate counter
  EXPECT_EQ(t.count(TopologyKind::Face), 2u);
  EXPECT_EQ(t.count(TopologyKind::Edge), 1u);
}

TEST(Topology, TagAndNameRoundtrip) {
  Topology t;
  t.add_entity(TopologyKind::Face);
  TopologyRef ref{TopologyKind::Face, 0};
  t.set_tag(ref, EntityTag{99});
  t.set_name(ref, "free_surface");
  EXPECT_EQ(t.tag(ref), EntityTag{99});
  ASSERT_TRUE(t.name(ref).has_value());
  EXPECT_EQ(*t.name(ref), "free_surface");
}

TEST(Topology, OutOfRangeSetThrows) {
  Topology t;
  EXPECT_THROW(t.set_tag(TopologyRef{TopologyKind::Region, 0}, EntityTag{1}), std::out_of_range);
}

TEST(Topology, MoveTransfers) {
  Topology a;
  a.add_entity(TopologyKind::Vertex);
  Topology b = std::move(a);
  EXPECT_EQ(b.count(TopologyKind::Vertex), 1u);
}

}  // namespace
