// SPDX-License-Identifier: Apache-2.0

#include "souxmar/pipeline/cache.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

using namespace souxmar::pipeline;

namespace {

TEST(ContentHash, EqualInputsProduceEqualHashes) {
  auto v = Value::map({{"a", Value::number(1.0)}, {"b", Value::string("x")}});
  auto h1 = hash_inputs("ctx", v, {});
  auto h2 = hash_inputs("ctx", v, {});
  EXPECT_EQ(h1, h2);
}

TEST(ContentHash, ContextDistinguishes) {
  auto v = Value::map({{"a", Value::number(1.0)}});
  EXPECT_NE(hash_inputs("ctx-a", v, {}), hash_inputs("ctx-b", v, {}));
}

TEST(ContentHash, DifferentNumbersDiffer) {
  EXPECT_NE(hash_inputs("c", Value::number(1.0), {}),
            hash_inputs("c", Value::number(2.0), {}));
}

TEST(ContentHash, UpstreamHashChangePropagates) {
  auto v = Value::map({{"in", Value::stage_ref("up")}});
  std::pair<std::string, ContentHash> u1{"up", ContentHash{0xAAAA}};
  std::pair<std::string, ContentHash> u2{"up", ContentHash{0xBBBB}};
  std::span<const std::pair<std::string, ContentHash>> s1{&u1, 1};
  std::span<const std::pair<std::string, ContentHash>> s2{&u2, 1};
  EXPECT_NE(hash_inputs("c", v, s1), hash_inputs("c", v, s2));
}

TEST(ContentHash, MapKeyOrderingDoesNotMatter) {
  // std::map orders keys, so equivalent maps hash identically regardless of
  // construction order.
  auto v1 = Value::map({{"a", Value::number(1.0)}, {"b", Value::number(2.0)}});
  auto v2 = Value::map({{"b", Value::number(2.0)}, {"a", Value::number(1.0)}});
  EXPECT_EQ(hash_inputs("c", v1, {}), hash_inputs("c", v2, {}));
}

TEST(ContentHash, HexFormatIs16Chars) {
  ContentHash h{0xDEADBEEFCAFEBABE};
  EXPECT_EQ(h.hex(), "deadbeefcafebabe");
  EXPECT_EQ(h.hex().size(), 16u);
}

TEST(Cache, EmptyOnConstruction) {
  Cache c;
  EXPECT_EQ(c.size(), 0u);
  EXPECT_EQ(c.get(ContentHash{1}), nullptr);
  EXPECT_FALSE(c.contains(ContentHash{1}));
}

TEST(Cache, PutAndGetRoundtrip) {
  Cache c;
  auto payload = std::make_shared<int>(42);
  c.put(ContentHash{99}, payload);
  EXPECT_EQ(c.size(), 1u);
  EXPECT_TRUE(c.contains(ContentHash{99}));
  auto got = c.get(ContentHash{99});
  ASSERT_NE(got, nullptr);
  EXPECT_EQ(*static_cast<int*>(got.get()), 42);
}

TEST(Cache, OverwriteReplacesPayload) {
  Cache c;
  c.put(ContentHash{1}, std::make_shared<int>(1));
  c.put(ContentHash{1}, std::make_shared<int>(2));
  EXPECT_EQ(c.size(), 1u);
  auto got = c.get(ContentHash{1});
  ASSERT_NE(got, nullptr);
  EXPECT_EQ(*static_cast<int*>(got.get()), 2);
}

TEST(Cache, ClearEmpties) {
  Cache c;
  c.put(ContentHash{1}, std::make_shared<int>(1));
  c.put(ContentHash{2}, std::make_shared<int>(2));
  EXPECT_EQ(c.size(), 2u);
  c.clear();
  EXPECT_EQ(c.size(), 0u);
}

}  // namespace
