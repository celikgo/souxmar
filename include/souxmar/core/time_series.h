// SPDX-License-Identifier: Apache-2.0
//
// TimeSeries — bounded-memory time-series view over a sequence of Fields.
//
// The series is a fixed-size list of frame times and a fixed-size list of
// field names; per-(frame, field) Field instances are produced on demand
// by a caller-supplied loader. The class owns an LRU cache that holds at
// most `cache_window` Fields in memory and evicts the least-recently-used
// entry when the window is full.
//
// The contract documented at the C ABI (souxmar-c/timeseries.h) says
// returned Field pointers are valid until the next call that mutates the
// cache. The C++ class enforces that — the cache owns the Field; any
// frame() call may evict prior entries.
//
// Sprint 32 PR 1 (partial — the LRU machinery; PVD-file dispatch and the
// reader plugin path land in PR 2). See docs/rfcs/0006-time-series.md
// and ADR-0042.

#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace souxmar::core {

class Field;

class TimeSeries {
 public:
  // Loader signature: (frame_index, field_name) -> a freshly-allocated
  // Field for that pair, or nullptr if it doesn't exist. The loader is
  // called at most once per (frame, field) per cache eviction cycle.
  using FrameLoader =
      std::function<std::unique_ptr<Field>(std::size_t frame_index,
                                            std::string_view field_name)>;

  TimeSeries(std::vector<double>      frame_times,
             std::vector<std::string> field_names,
             FrameLoader              loader,
             std::size_t              initial_cache_window = 16);

  ~TimeSeries();

  TimeSeries(TimeSeries&&) noexcept;
  TimeSeries& operator=(TimeSeries&&) noexcept;

  TimeSeries(const TimeSeries&)            = delete;
  TimeSeries& operator=(const TimeSeries&) = delete;

  // -------- Metadata --------

  [[nodiscard]] std::size_t frame_count() const noexcept;
  [[nodiscard]] std::size_t field_count() const noexcept;

  // Returns the timestamp of the given frame, or 0.0 if out of range.
  [[nodiscard]] double time(std::size_t frame_index) const noexcept;

  // Borrowed view; valid for the lifetime of the TimeSeries.
  [[nodiscard]] std::string_view field_name(std::size_t index) const noexcept;

  // -------- Frame access --------

  // Returns the cached Field*, calling the loader if not already cached.
  // Returns nullptr if frame_index is out of range, field_name is unknown,
  // or the loader returns nullptr. The pointer is valid until the next
  // call that mutates the cache (any frame()/cache_preload()/
  // set_cache_window() call).
  [[nodiscard]] const Field* frame(std::size_t      frame_index,
                                    std::string_view field_name);

  // -------- Cache control --------

  // Resize the LRU window. Setting a smaller window than the current
  // populated size triggers immediate LRU eviction. window_size == 0
  // disables caching (every frame() call hits the loader).
  void set_cache_window(std::size_t window_size);
  [[nodiscard]] std::size_t cache_window()   const noexcept;
  [[nodiscard]] std::size_t cache_occupancy() const noexcept;  // count currently resident

  // Pre-warm: synchronously load [start_frame, start_frame + count) for
  // every known field. Caps `count` at frame_count - start_frame.
  // Returns the number of (frame, field) pairs successfully loaded.
  std::size_t cache_preload(std::size_t start_frame, std::size_t count);

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace souxmar::core
