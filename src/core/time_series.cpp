// SPDX-License-Identifier: Apache-2.0
//
// TimeSeries — LRU-cached (frame, field) Field manager.
//
// Algorithm: standard LRU with a std::list (recency order, front =
// most recent) + an unordered_map (O(1) lookup by key). The map stores
// iterators into the list, which std::list guarantees remain valid
// across insertions and erasures of unrelated elements.

#include "souxmar/core/time_series.h"

#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "souxmar/core/field.h"

namespace souxmar::core {

namespace {

// Composite key. `field_index` is the index into field_names_; lookup
// by name is linear in field_count (assumed small) but the cache
// itself indexes by this packed key for O(1) operations.
struct Key {
  std::size_t frame_index;
  std::size_t field_index;
  bool operator==(const Key& o) const noexcept {
    return frame_index == o.frame_index && field_index == o.field_index;
  }
};

struct KeyHash {
  std::size_t operator()(const Key& k) const noexcept {
    // Mix using FNV-style splitmix.
    std::size_t h = k.frame_index;
    h ^= k.field_index + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    return h;
  }
};

struct CacheEntry {
  Key                    key;
  std::unique_ptr<Field> field;
};

}  // namespace

class TimeSeries::Impl {
 public:
  std::vector<double>      frame_times;
  std::vector<std::string> field_names;
  FrameLoader              loader;
  std::size_t              window;

  // LRU machinery. The list owns the entries (front = most recent).
  // The map indexes by composite key for O(1) lookup.
  std::list<CacheEntry>                                        lru;
  std::unordered_map<Key, std::list<CacheEntry>::iterator, KeyHash> index;

  Impl(std::vector<double> ft, std::vector<std::string> fn,
       FrameLoader l, std::size_t w)
      : frame_times(std::move(ft)),
        field_names(std::move(fn)),
        loader(std::move(l)),
        window(w) {}

  // Resolve a field name to its index (linear scan; field_names is small).
  // Returns SIZE_MAX if not found.
  std::size_t resolve_field(std::string_view name) const noexcept {
    for (std::size_t i = 0; i < field_names.size(); ++i) {
      if (field_names[i] == name) return i;
    }
    return static_cast<std::size_t>(-1);
  }

  // Touch an entry: move it to the front of the LRU list.
  void touch(std::list<CacheEntry>::iterator it) noexcept {
    lru.splice(lru.begin(), lru, it);
    // `it` remains valid; std::list iterators are stable across splice.
  }

  // Evict from the back until size <= window.
  void evict_to_window() noexcept {
    while (lru.size() > window && !lru.empty()) {
      index.erase(lru.back().key);
      lru.pop_back();
    }
  }

  const Field* frame_internal(std::size_t frame_index,
                               std::string_view field_name) {
    if (frame_index >= frame_times.size()) return nullptr;
    const std::size_t field_idx = resolve_field(field_name);
    if (field_idx == static_cast<std::size_t>(-1)) return nullptr;

    const Key key{frame_index, field_idx};

    if (auto it = index.find(key); it != index.end()) {
      touch(it->second);
      return it->second->field.get();
    }

    // Cache miss. Load via the user loader.
    if (!loader) return nullptr;
    auto fresh = loader(frame_index, field_name);
    if (!fresh) return nullptr;

    if (window == 0) {
      // Caching disabled. We still need to return *some* pointer that's
      // valid until the next call — keep a single-slot "scratch" entry
      // and rotate it. With window==0, the cache holds at most one
      // ephemeral entry; the next call evicts it.
      lru.clear();
      index.clear();
      lru.push_front(CacheEntry{key, std::move(fresh)});
      // Window of 0 means we don't even keep this entry across calls —
      // but the contract says the pointer is valid until the NEXT call,
      // so we hold it transiently and the next call clears.
      return lru.front().field.get();
    }

    lru.push_front(CacheEntry{key, std::move(fresh)});
    index.emplace(key, lru.begin());
    evict_to_window();
    return lru.front().field.get();
  }
};

TimeSeries::TimeSeries(std::vector<double>      frame_times,
                       std::vector<std::string> field_names,
                       FrameLoader              loader,
                       std::size_t              initial_cache_window)
    : impl_(std::make_unique<Impl>(std::move(frame_times),
                                    std::move(field_names),
                                    std::move(loader),
                                    initial_cache_window)) {}

TimeSeries::~TimeSeries()                              = default;
TimeSeries::TimeSeries(TimeSeries&&) noexcept          = default;
TimeSeries& TimeSeries::operator=(TimeSeries&&) noexcept = default;

std::size_t TimeSeries::frame_count() const noexcept {
  return impl_->frame_times.size();
}

std::size_t TimeSeries::field_count() const noexcept {
  return impl_->field_names.size();
}

double TimeSeries::time(std::size_t frame_index) const noexcept {
  if (frame_index >= impl_->frame_times.size()) return 0.0;
  return impl_->frame_times[frame_index];
}

std::string_view TimeSeries::field_name(std::size_t index) const noexcept {
  if (index >= impl_->field_names.size()) return {};
  return impl_->field_names[index];
}

const Field* TimeSeries::frame(std::size_t frame_index,
                                std::string_view field_name) {
  return impl_->frame_internal(frame_index, field_name);
}

void TimeSeries::set_cache_window(std::size_t window_size) {
  impl_->window = window_size;
  impl_->evict_to_window();
}

std::size_t TimeSeries::cache_window() const noexcept {
  return impl_->window;
}

std::size_t TimeSeries::cache_occupancy() const noexcept {
  return impl_->lru.size();
}

std::size_t TimeSeries::cache_preload(std::size_t start_frame,
                                       std::size_t count) {
  const std::size_t n = impl_->frame_times.size();
  if (start_frame >= n) return 0;
  const std::size_t end = std::min(start_frame + count, n);

  std::size_t loaded = 0;
  for (std::size_t f = start_frame; f < end; ++f) {
    for (const auto& name : impl_->field_names) {
      if (impl_->frame_internal(f, name) != nullptr) ++loaded;
    }
  }
  return loaded;
}

}  // namespace souxmar::core
