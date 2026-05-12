// SPDX-License-Identifier: Apache-2.0

#include "souxmar/pipeline/cache.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <random>
#include <shared_mutex>
#include <string>
#include <system_error>

namespace souxmar::pipeline {

// ============================================================================
// SHA-256
// ============================================================================
//
// Straightforward in-tree implementation per FIPS 180-4. Not constant-time
// (we hash public, non-secret inputs). Verified against NIST KATs in
// tests/unit/test_pipeline_cache.cpp.
//
// Performance is fine for the orchestrator's use case (hashing kilobyte-
// scale Value trees). When the parallel runner needs faster hashing in
// Sprint 5, BLAKE3 with a tuned implementation slots in behind the same
// public ContentHash type.

namespace {

constexpr std::uint32_t kK[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

constexpr std::uint32_t kInitH[8] = {
    0x6a09e667,
    0xbb67ae85,
    0x3c6ef372,
    0xa54ff53a,
    0x510e527f,
    0x9b05688c,
    0x1f83d9ab,
    0x5be0cd19,
};

inline std::uint32_t rotr32(std::uint32_t x, int n) noexcept {
  return (x >> n) | (x << (32 - n));
}

class Sha256 {
 public:
  Sha256() noexcept {
    std::memcpy(h_, kInitH, sizeof(h_));
  }

  void update(const void* data, std::size_t len) noexcept {
    const auto* p = static_cast<const std::uint8_t*>(data);
    bit_count_ += static_cast<std::uint64_t>(len) * 8;
    while (len > 0) {
      const std::size_t take = std::min<std::size_t>(64 - buf_len_, len);
      std::memcpy(buf_ + buf_len_, p, take);
      buf_len_ += take;
      p += take;
      len -= take;
      if (buf_len_ == 64) {
        compress(buf_);
        buf_len_ = 0;
      }
    }
  }

  ContentHash::Bytes finalize() noexcept {
    // Pad: 0x80 then zeros, then 64-bit big-endian bit count in last 8 bytes.
    buf_[buf_len_++] = 0x80;
    if (buf_len_ > 56) {
      std::memset(buf_ + buf_len_, 0, 64 - buf_len_);
      compress(buf_);
      buf_len_ = 0;
    }
    std::memset(buf_ + buf_len_, 0, 56 - buf_len_);
    for (int i = 0; i < 8; ++i) {
      buf_[63 - i] = static_cast<std::uint8_t>(bit_count_ >> (8 * i));
    }
    compress(buf_);

    ContentHash::Bytes out{};
    for (int i = 0; i < 8; ++i) {
      out[static_cast<std::size_t>(i * 4 + 0)] = static_cast<std::uint8_t>(h_[i] >> 24);
      out[static_cast<std::size_t>(i * 4 + 1)] = static_cast<std::uint8_t>(h_[i] >> 16);
      out[static_cast<std::size_t>(i * 4 + 2)] = static_cast<std::uint8_t>(h_[i] >> 8);
      out[static_cast<std::size_t>(i * 4 + 3)] = static_cast<std::uint8_t>(h_[i]);
    }
    return out;
  }

 private:
  void compress(const std::uint8_t block[64]) noexcept {
    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
      w[i] = (static_cast<std::uint32_t>(block[i * 4 + 0]) << 24)
             | (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16)
             | (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8)
             | static_cast<std::uint32_t>(block[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
      const std::uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const std::uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    std::uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3];
    std::uint32_t e = h_[4], f = h_[5], g = h_[6], hh = h_[7];

    for (int i = 0; i < 64; ++i) {
      const std::uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
      const std::uint32_t ch = (e & f) ^ (~e & g);
      const std::uint32_t temp1 = hh + S1 + ch + kK[i] + w[i];
      const std::uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
      const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      const std::uint32_t temp2 = S0 + maj;

      hh = g;
      g = f;
      f = e;
      e = d + temp1;
      d = c;
      c = b;
      b = a;
      a = temp1 + temp2;
    }

    h_[0] += a;
    h_[1] += b;
    h_[2] += c;
    h_[3] += d;
    h_[4] += e;
    h_[5] += f;
    h_[6] += g;
    h_[7] += hh;
  }

  std::uint32_t h_[8];
  std::uint8_t buf_[64]{};
  std::size_t buf_len_{0};
  std::uint64_t bit_count_{0};
};

// ============================================================================
// Value-tree hashing
// ============================================================================
//
// Walk the Value tree feeding canonical bytes into SHA-256. The encoding is
// length-prefixed and tag-prefixed so distinct structurally-equal-looking
// trees never collide:
//
//   Null     : tag(0)
//   Bool     : tag(1) + 1 byte
//   Number   : tag(2) + 8 raw bytes (host-endian double bit pattern)
//   String   : tag(3) + uint64 length + bytes
//   StageRef : tag(4) + uint64 length + bytes + upstream hash (32 bytes
//                                                if resolved, all-0xFF if not)
//   List     : tag(5) + uint64 length + element encodings
//   Map      : tag(6) + uint64 length + (key length+bytes, value encoding)*
//              keys are walked in std::map sorted order.

void feed_u64(Sha256& sh, std::uint64_t v) {
  std::uint8_t b[8];
  for (int i = 0; i < 8; ++i)
    b[i] = static_cast<std::uint8_t>(v >> (8 * i));
  sh.update(b, 8);
}

void feed_str(Sha256& sh, std::string_view s) {
  feed_u64(sh, s.size());
  if (!s.empty())
    sh.update(s.data(), s.size());
}

void hash_value(Sha256& sh,
                const Value& v,
                std::span<const std::pair<std::string, ContentHash>> upstream) {
  const std::uint8_t kind_byte = static_cast<std::uint8_t>(v.kind());
  sh.update(&kind_byte, 1);
  switch (v.kind()) {
    case Value::Kind::Null:
      return;
    case Value::Kind::Bool: {
      const std::uint8_t b = v.as_bool() ? 1 : 0;
      sh.update(&b, 1);
      return;
    }
    case Value::Kind::Number: {
      const double d = v.as_number();
      static_assert(sizeof(d) == 8, "double must be 8 bytes for portable hash");
      sh.update(&d, sizeof(d));
      return;
    }
    case Value::Kind::String:
      feed_str(sh, v.as_string());
      return;
    case Value::Kind::Stage: {
      const auto& ref = v.as_stage();
      feed_str(sh, ref.stage_id);
      ContentHash::Bytes upbytes{};
      bool resolved = false;
      for (const auto& [id, ch] : upstream) {
        if (id == ref.stage_id) {
          upbytes = ch.bytes();
          resolved = true;
          break;
        }
      }
      if (!resolved)
        std::memset(upbytes.data(), 0xFF, upbytes.size());
      sh.update(upbytes.data(), upbytes.size());
      return;
    }
    case Value::Kind::List: {
      const auto items = v.as_list();
      feed_u64(sh, items.size());
      for (const auto& item : items)
        hash_value(sh, item, upstream);
      return;
    }
    case Value::Kind::Map: {
      const auto& m = v.as_map();
      feed_u64(sh, m.size());
      for (const auto& [k, child] : m) {
        feed_str(sh, k);
        hash_value(sh, child, upstream);
      }
      return;
    }
  }
}

}  // namespace

// ============================================================================
// ContentHash
// ============================================================================

ContentHash::ContentHash(std::uint64_t seed) noexcept {
  // Big-endian pack into the first 8 bytes; remainder zero. Keeps unit-test
  // hex output predictable: ContentHash{0xDEADBEEFCAFEBABE}.hex() prefix is
  // "deadbeefcafebabe...".
  for (int i = 0; i < 8; ++i) {
    bytes_[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(seed >> (8 * (7 - i)));
  }
}

std::string ContentHash::hex() const {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out;
  out.resize(64);
  for (std::size_t i = 0; i < bytes_.size(); ++i) {
    out[2 * i + 0] = kHex[(bytes_[i] >> 4) & 0xF];
    out[2 * i + 1] = kHex[bytes_[i] & 0xF];
  }
  return out;
}

ContentHash hash_inputs(std::string_view context,
                        const Value& inputs,
                        std::span<const std::pair<std::string, ContentHash>> upstream) {
  Sha256 sh;
  feed_str(sh, context);
  hash_value(sh, inputs, upstream);
  return ContentHash{sh.finalize()};
}

// ============================================================================
// In-memory Cache
// ============================================================================

struct Cache::Impl {
  mutable std::shared_mutex mu;
  std::unordered_map<ContentHash, std::shared_ptr<void>> entries;
};

Cache::Cache() : impl_(std::make_unique<Impl>()) {}

Cache::~Cache() = default;
Cache::Cache(Cache&&) noexcept = default;
Cache& Cache::operator=(Cache&&) noexcept = default;

void Cache::put(ContentHash key, std::shared_ptr<void> payload) {
  std::unique_lock lock(impl_->mu);
  impl_->entries.insert_or_assign(key, std::move(payload));
}

std::shared_ptr<void> Cache::get(ContentHash key) const {
  std::shared_lock lock(impl_->mu);
  auto it = impl_->entries.find(key);
  if (it == impl_->entries.end())
    return nullptr;
  return it->second;
}

bool Cache::contains(ContentHash key) const {
  std::shared_lock lock(impl_->mu);
  return impl_->entries.contains(key);
}

std::size_t Cache::size() const {
  std::shared_lock lock(impl_->mu);
  return impl_->entries.size();
}

void Cache::clear() {
  std::unique_lock lock(impl_->mu);
  impl_->entries.clear();
}

// ============================================================================
// DiskCache
// ============================================================================

namespace {

std::filesystem::path home_dir() {
#if defined(_WIN32)
  if (const char* v = std::getenv("USERPROFILE"); v && *v)
    return v;
  return {};
#else
  if (const char* v = std::getenv("HOME"); v && *v)
    return v;
  return {};
#endif
}

}  // namespace

DiskCache::DiskCache(std::filesystem::path dir) : dir_(std::move(dir)) {
  std::error_code ec;
  std::filesystem::create_directories(dir_, ec);
  if (ec && !std::filesystem::is_directory(dir_)) {
    throw std::filesystem::filesystem_error("souxmar DiskCache: cannot create directory", dir_, ec);
  }
}

bool DiskCache::put_bytes(ContentHash key, std::span<const std::uint8_t> blob) const {
  // tmp + rename for atomicity. The randomization avoids collisions when
  // two threads in the same process write the same key concurrently.
  thread_local std::mt19937_64 rng{std::random_device{}()};
  const auto target = dir_ / key.hex();
  const auto tmp = dir_ / (key.hex() + ".tmp." + std::to_string(rng()));

  {
    std::ofstream sink(tmp, std::ios::binary | std::ios::trunc);
    if (!sink.is_open())
      return false;
    if (!blob.empty()) {
      sink.write(reinterpret_cast<const char*>(blob.data()),
                 static_cast<std::streamsize>(blob.size()));
    }
    if (!sink.good()) {
      std::error_code ec;
      std::filesystem::remove(tmp, ec);
      return false;
    }
  }

  std::error_code ec;
  std::filesystem::rename(tmp, target, ec);
  if (ec) {
    std::filesystem::remove(tmp, ec);
    return false;
  }
  return true;
}

std::optional<std::vector<std::uint8_t>> DiskCache::get_bytes(ContentHash key) const {
  const auto path = dir_ / key.hex();
  std::ifstream src(path, std::ios::binary);
  if (!src.is_open())
    return std::nullopt;
  src.seekg(0, std::ios::end);
  const auto size = src.tellg();
  if (size < 0)
    return std::nullopt;
  src.seekg(0, std::ios::beg);
  std::vector<std::uint8_t> out(static_cast<std::size_t>(size));
  if (size > 0) {
    src.read(reinterpret_cast<char*>(out.data()), size);
    if (!src.good() && !src.eof())
      return std::nullopt;
  }
  return out;
}

bool DiskCache::contains(ContentHash key) const {
  std::error_code ec;
  return std::filesystem::exists(dir_ / key.hex(), ec);
}

std::filesystem::path DiskCache::default_dir(const std::filesystem::path& override_path) {
  if (!override_path.empty())
    return override_path;

  if (const char* v = std::getenv("SOUXMAR_CACHE_DIR"); v && *v) {
    return std::filesystem::path{v};
  }
#if defined(_WIN32)
  if (const char* v = std::getenv("LOCALAPPDATA"); v && *v) {
    return std::filesystem::path{v} / "souxmar" / "cache";
  }
#elif defined(__APPLE__)
  if (auto h = home_dir(); !h.empty()) {
    return h / "Library" / "Caches" / "souxmar";
  }
#else
  if (const char* v = std::getenv("XDG_CACHE_HOME"); v && *v) {
    return std::filesystem::path{v} / "souxmar";
  }
  if (auto h = home_dir(); !h.empty()) {
    return h / ".cache" / "souxmar";
  }
#endif
  std::error_code ec;
  return std::filesystem::temp_directory_path(ec) / "souxmar-cache";
}

}  // namespace souxmar::pipeline
