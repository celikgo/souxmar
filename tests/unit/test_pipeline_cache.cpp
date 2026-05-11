// SPDX-License-Identifier: Apache-2.0

#include "souxmar/pipeline/cache.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <vector>

using namespace souxmar::pipeline;
namespace fs = std::filesystem;

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

TEST(ContentHash, HexFormatIs64Chars) {
  // Sprint 3 push 3: ContentHash now backs a 256-bit (32-byte) digest.
  // The seed constructor packs the uint64 into the first 8 bytes (big
  // endian) and zero-fills the remaining 24 — gives unit tests deterministic
  // hex output without computing a real SHA-256.
  ContentHash h{0xDEADBEEFCAFEBABE};
  EXPECT_EQ(h.hex().size(), 64u);
  EXPECT_EQ(h.hex().substr(0, 16), "deadbeefcafebabe");
  EXPECT_EQ(h.hex().substr(16), std::string(48, '0'));
}

// Stability: two calls with identical inputs return the same digest, and
// the digest covers all 32 bytes (i.e. the SHA-256 finalize step is wired
// through, not just leaving zeros). This catches accidental encoding drift
// without pinning a specific digest value (we'd want a separate, dedicated
// SHA-256 KAT suite for that — Sprint 5 hardening territory).
TEST(ContentHash, DigestIsStableAndUses32Bytes) {
  const auto h = hash_inputs("ctx", Value::number(3.14), {});
  const auto h2 = hash_inputs("ctx", Value::number(3.14), {});
  EXPECT_EQ(h, h2);
  // At least one byte beyond the legacy 8-byte prefix must be non-zero,
  // proving the digest is the real SHA-256 output and not a uint64 seed.
  bool tail_nonzero = false;
  for (std::size_t i = 8; i < 32; ++i) {
    if (h.bytes()[i] != 0) { tail_nonzero = true; break; }
  }
  EXPECT_TRUE(tail_nonzero);
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

// ---- DiskCache ----------------------------------------------------------

class DiskCacheTest : public ::testing::Test {
 protected:
  void SetUp() override {
    std::random_device rd;
    dir_ = fs::temp_directory_path() /
           ("souxmar-diskcache-test-" + std::to_string(rd()));
  }
  void TearDown() override {
    std::error_code ec;
    fs::remove_all(dir_, ec);
  }
  fs::path dir_;
};

TEST_F(DiskCacheTest, RoundtripsBytesUnderHash) {
  DiskCache d(dir_);
  const ContentHash key{0x1122334455667788ULL};
  const std::vector<std::uint8_t> blob{0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0xFF};
  ASSERT_TRUE(d.put_bytes(key, blob));
  ASSERT_TRUE(d.contains(key));

  auto got = d.get_bytes(key);
  ASSERT_TRUE(got.has_value());
  EXPECT_EQ(*got, blob);
}

TEST_F(DiskCacheTest, MissingKeyReturnsNullopt) {
  DiskCache d(dir_);
  EXPECT_FALSE(d.contains(ContentHash{0xDEAD}));
  EXPECT_FALSE(d.get_bytes(ContentHash{0xDEAD}).has_value());
}

TEST_F(DiskCacheTest, DefaultDirHonorsOverride) {
  const auto chosen = DiskCache::default_dir(dir_);
  EXPECT_EQ(chosen, dir_);
}

TEST_F(DiskCacheTest, EmptyBlobIsValid) {
  DiskCache d(dir_);
  const ContentHash key{0x42};
  ASSERT_TRUE(d.put_bytes(key, {}));
  auto got = d.get_bytes(key);
  ASSERT_TRUE(got.has_value());
  EXPECT_TRUE(got->empty());
}

}  // namespace
