// SPDX-License-Identifier: Apache-2.0

#include "souxmar/core/tag.h"

#include <gtest/gtest.h>

#include <unordered_map>
#include <unordered_set>

using namespace souxmar::core;

namespace {

TEST(EntityTag, DefaultIsInvalid) {
  EntityTag t;
  EXPECT_FALSE(t.valid());
  EXPECT_EQ(t.value, -1);
}

TEST(EntityTag, ZeroIsValid) {
  EntityTag t{0};
  EXPECT_TRUE(t.valid());
}

TEST(EntityTag, EqualityAndOrdering) {
  EXPECT_EQ((EntityTag{5}), (EntityTag{5}));
  EXPECT_NE((EntityTag{5}), (EntityTag{6}));
  EXPECT_LT((EntityTag{5}), (EntityTag{6}));
}

TEST(EntityTag, HashableAsKey) {
  std::unordered_map<EntityTag, int> m;
  m[EntityTag{1}] = 10;
  m[EntityTag{2}] = 20;
  EXPECT_EQ(m[EntityTag{1}], 10);
  EXPECT_EQ(m[EntityTag{2}], 20);
  EXPECT_EQ(m.size(), 2u);
}

TEST(NodeIndex, DistinctFromCellIndex) {
  // Compile-time check that types are distinct: std::is_same_v.
  static_assert(!std::is_same_v<NodeIndex, CellIndex>);
  static_assert(!std::is_same_v<NodeIndex, VertexIndex>);
}

TEST(NodeIndex, HashableAndOrderable) {
  std::unordered_set<NodeIndex> s;
  s.insert(NodeIndex{1});
  s.insert(NodeIndex{2});
  s.insert(NodeIndex{1});  // duplicate
  EXPECT_EQ(s.size(), 2u);
  EXPECT_LT((NodeIndex{1}), (NodeIndex{2}));
}

TEST(VertexIndex, ZeroInitialised) {
  VertexIndex v{};
  EXPECT_EQ(v.value, 0u);
}

}  // namespace
