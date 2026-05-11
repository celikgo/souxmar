// SPDX-License-Identifier: Apache-2.0

#include "souxmar/pipeline/cache.h"

#include <fmt/core.h>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

namespace souxmar::pipeline {

namespace {

constexpr std::uint64_t kFnvOffset = 0xcbf29ce484222325ULL;
constexpr std::uint64_t kFnvPrime  = 0x100000001b3ULL;

inline std::uint64_t mix_byte(std::uint64_t h, std::uint8_t b) noexcept {
  h ^= static_cast<std::uint64_t>(b);
  h *= kFnvPrime;
  return h;
}

inline std::uint64_t mix_bytes(std::uint64_t h, const void* data, std::size_t len) noexcept {
  const auto* p = static_cast<const std::uint8_t*>(data);
  for (std::size_t i = 0; i < len; ++i) h = mix_byte(h, p[i]);
  return h;
}

inline std::uint64_t mix_string(std::uint64_t h, std::string_view s) noexcept {
  h = mix_bytes(h, s.data(), s.size());
  // Length-prefix-free hashing risks collisions like ("ab","c") vs ("a","bc");
  // mix in a separator so distinct token boundaries are visible to the hash.
  return mix_byte(h, 0);
}

// Recursive walk of a Value tree. The order of map iteration is the natural
// std::map order (sorted by key) which is stable and deterministic.
std::uint64_t hash_value(std::uint64_t h, const Value& v,
                         std::span<const std::pair<std::string, ContentHash>> upstream) {
  h = mix_byte(h, static_cast<std::uint8_t>(v.kind()));
  switch (v.kind()) {
    case Value::Kind::Null:
      return mix_byte(h, 0);
    case Value::Kind::Bool:
      return mix_byte(h, v.as_bool() ? 1 : 0);
    case Value::Kind::Number: {
      const double d = v.as_number();
      static_assert(sizeof(d) == 8, "double must be 8 bytes for portable hash");
      return mix_bytes(h, &d, sizeof(d));
    }
    case Value::Kind::String:
      return mix_string(h, v.as_string());
    case Value::Kind::Stage: {
      const auto& ref = v.as_stage();
      h = mix_string(h, ref.stage_id);
      // Fold the upstream stage's hash in so a change in the producer
      // changes this stage's content hash transitively.
      for (const auto& [id, ch] : upstream) {
        if (id == ref.stage_id) {
          const auto u = ch.value();
          return mix_bytes(h, &u, sizeof(u));
        }
      }
      // Unresolved upstream — distinguish from "no upstream" deliberately.
      return mix_byte(h, 0xFF);
    }
    case Value::Kind::List: {
      for (const auto& item : v.as_list()) h = hash_value(h, item, upstream);
      return mix_byte(h, 0);
    }
    case Value::Kind::Map: {
      for (const auto& [k, child] : v.as_map()) {
        h = mix_string(h, k);
        h = hash_value(h, child, upstream);
      }
      return mix_byte(h, 0);
    }
  }
  return h;
}

}  // namespace

std::string ContentHash::hex() const {
  return fmt::format("{:016x}", value_);
}

ContentHash hash_inputs(std::string_view             context,
                        const Value&                 inputs,
                        std::span<const std::pair<std::string, ContentHash>>
                                                     upstream) {
  std::uint64_t h = kFnvOffset;
  h = mix_string(h, context);
  h = hash_value(h, inputs, upstream);
  return ContentHash{h};
}

// ---- Cache --------------------------------------------------------------

struct Cache::Impl {
  mutable std::shared_mutex                                mu;
  std::unordered_map<ContentHash, std::shared_ptr<void>>   entries;
};

Cache::Cache() : impl_(std::make_unique<Impl>()) {}
Cache::~Cache() = default;
Cache::Cache(Cache&&) noexcept            = default;
Cache& Cache::operator=(Cache&&) noexcept = default;

void Cache::put(ContentHash key, std::shared_ptr<void> payload) {
  std::unique_lock lock(impl_->mu);
  impl_->entries.insert_or_assign(key, std::move(payload));
}

std::shared_ptr<void> Cache::get(ContentHash key) const {
  std::shared_lock lock(impl_->mu);
  auto it = impl_->entries.find(key);
  if (it == impl_->entries.end()) return nullptr;
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

}  // namespace souxmar::pipeline
