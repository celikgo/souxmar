// SPDX-License-Identifier: Apache-2.0
//
// Pipeline cache — content-addressed lookup of stage outputs.
//
// Sprint 3 push 3 promoted the content hash from FNV-1a (collision-tolerant
// in-process only) to SHA-256, and added a DiskCache byte-blob KV that the
// runner uses opt-in via RunOptions::disk_backing. The in-memory Cache below
// stays unchanged (process-scoped, type-erased payloads).

#pragma once

#include "souxmar/pipeline/value.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace souxmar::pipeline {

// 256-bit content key — SHA-256 over (context, inputs, upstream).
//
// A cryptographic digest is overkill for pure cache correctness (the
// in-memory store is process-scoped, so collisions only matter inside one
// run), but the same hash is the key for the on-disk cache and will become
// the key for distributed caches and remote artifact stores in Sprint 7+.
// SHA-256 is the smallest hash that survives those use cases unchanged.
class ContentHash {
 public:
  using Bytes = std::array<std::uint8_t, 32>;

  ContentHash() = default;

  // Legacy / test constructor: pack a uint64_t seed into the first 8 bytes
  // (big-endian) and zero the remaining 24. Lets unit tests build deterministic
  // hashes without computing real digests. Production code uses hash_inputs().
  explicit ContentHash(std::uint64_t seed) noexcept;

  explicit ContentHash(Bytes bytes) noexcept : bytes_{bytes} {}

  [[nodiscard]] const Bytes& bytes() const noexcept {
    return bytes_;
  }

  [[nodiscard]] std::string hex() const;  // 64 lowercase hex chars

  [[nodiscard]] bool operator==(const ContentHash&) const noexcept = default;

 private:
  Bytes bytes_{};
};

// Compute a content hash over a Value tree, threading in a "context" string
// (typically capability_id + plugin_version) that distinguishes otherwise-
// identical input trees fed to different plugins.
[[nodiscard]] ContentHash hash_inputs(
    std::string_view context,
    const Value& inputs,
    std::span<const std::pair<std::string, ContentHash>> upstream);

// In-process content-addressed store. Type-erased payloads — the dispatcher
// knows how to interpret them (Mesh / Field / Path StageOutput).
class Cache {
 public:
  Cache();
  ~Cache();

  // Move-only.
  Cache(Cache&&) noexcept;
  Cache& operator=(Cache&&) noexcept;
  Cache(const Cache&) = delete;
  Cache& operator=(const Cache&) = delete;

  void put(ContentHash key, std::shared_ptr<void> payload);

  [[nodiscard]] std::shared_ptr<void> get(ContentHash key) const;

  [[nodiscard]] bool contains(ContentHash key) const;
  [[nodiscard]] std::size_t size() const;

  void clear();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Persistent byte-blob cache backed by a directory. One file per hash
// (`<dir>/<hex>`). Atomic per-key writes via temp-file + rename, no
// cross-process locking — the parallel runner in Sprint 5 will add a
// per-key advisory lock; for Sprint 3 a single souxmar process is the
// expected user.
class DiskCache {
 public:
  // Creates `dir` if it does not exist. Throws std::filesystem::filesystem_error
  // on permission / IO failure.
  explicit DiskCache(std::filesystem::path dir);

  // Write a blob under `key`. Atomic: writes to <dir>/<hex>.tmp then renames.
  // Returns false on IO error (the cache is best-effort, not a write barrier).
  bool put_bytes(ContentHash key, std::span<const std::uint8_t> blob) const;

  // Read the blob for `key`, or std::nullopt if no such entry.
  [[nodiscard]] std::optional<std::vector<std::uint8_t>> get_bytes(ContentHash key) const;

  [[nodiscard]] bool contains(ContentHash key) const;

  [[nodiscard]] const std::filesystem::path& dir() const noexcept {
    return dir_;
  }

  // Returns the platform default cache directory if `override_path` is empty,
  // else returns `override_path` unchanged. Resolution order:
  //   1. $SOUXMAR_CACHE_DIR
  //   2. $XDG_CACHE_HOME/souxmar               (Linux/BSD)
  //      ~/Library/Caches/souxmar              (macOS)
  //      %LOCALAPPDATA%\souxmar\cache          (Windows)
  //   3. <tempdir>/souxmar-cache               (last-resort fallback)
  static std::filesystem::path default_dir(const std::filesystem::path& override_path = {});

 private:
  std::filesystem::path dir_;
};

}  // namespace souxmar::pipeline

namespace std {
template <>
struct hash<souxmar::pipeline::ContentHash> {
  size_t operator()(const souxmar::pipeline::ContentHash& h) const noexcept {
    // Fold the first 8 bytes of the digest into a size_t — ample entropy
    // for hash-table bucketing; the digest itself disambiguates collisions
    // via operator==.
    const auto& b = h.bytes();
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
      v = (v << 8) | b[static_cast<std::size_t>(i)];
    return static_cast<size_t>(v);
  }
};
}  // namespace std
