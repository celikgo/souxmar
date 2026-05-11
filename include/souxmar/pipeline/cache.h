// SPDX-License-Identifier: Apache-2.0
//
// Pipeline cache — content-addressed lookup of stage outputs.
//
// Sprint 3 push 1 ships an in-memory cache keyed by FNV-1a hashes; the
// on-disk cache + cryptographic hashing (BLAKE3 or SHA256) lands in
// Sprint 3 push 2 alongside the orchestrator's persistence story. The
// public interface here will not change when that lands.

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

#include "souxmar/pipeline/value.h"

namespace souxmar::pipeline {

// 64-bit FNV-1a content key. Stable across builds of the same compiler
// version + the same byte stream; not cryptographic — collisions are
// theoretically possible but practically irrelevant in-memory.
class ContentHash {
 public:
  ContentHash() = default;
  explicit ContentHash(std::uint64_t v) noexcept : value_{v} {}

  [[nodiscard]] std::uint64_t value() const noexcept { return value_; }
  [[nodiscard]] std::string   hex()   const;

  [[nodiscard]] bool operator==(const ContentHash&) const noexcept = default;

 private:
  std::uint64_t value_{0};
};

// Compute a content hash over a Value tree, threading in a "context" string
// (typically capability_id + plugin_version) that distinguishes otherwise-
// identical input trees fed to different plugins.
[[nodiscard]] ContentHash hash_inputs(std::string_view             context,
                                      const Value&                 inputs,
                                      std::span<const std::pair<std::string, ContentHash>>
                                                                   upstream);

// Cache entry — opaque payload + the hash it was computed under.
class Cache {
 public:
  Cache();
  ~Cache();

  // Move-only.
  Cache(Cache&&) noexcept;
  Cache& operator=(Cache&&) noexcept;
  Cache(const Cache&)            = delete;
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

}  // namespace souxmar::pipeline

namespace std {
template <>
struct hash<souxmar::pipeline::ContentHash> {
  size_t operator()(const souxmar::pipeline::ContentHash& h) const noexcept {
    return h.value();
  }
};
}  // namespace std
