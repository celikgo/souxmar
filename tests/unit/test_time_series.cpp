// SPDX-License-Identifier: Apache-2.0

#include "souxmar/core/field.h"
#include "souxmar/core/time_series.h"

#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace souxmar::core;

namespace {

// Synthetic loader: produces a scalar field of 4 locations where every
// value equals `frame_index * 100 + location_index`. Counts how many
// times it's been called via a shared atomic so the LRU behaviour can
// be observed.
struct CountingLoader {
  std::shared_ptr<std::atomic<int>> calls = std::make_shared<std::atomic<int>>(0);

  std::unique_ptr<Field> operator()(std::size_t frame_index, std::string_view field_name) const {
    calls->fetch_add(1, std::memory_order_relaxed);
    if (field_name != "scalar")
      return nullptr;  // only one known field
    auto f = std::make_unique<Field>(
        std::string{field_name}, FieldLocation::Nodal, FieldKind::Scalar, 4);
    auto buf = f->step(0);
    for (std::size_t i = 0; i < 4; ++i) {
      buf[i] = static_cast<double>(frame_index * 100 + i);
    }
    return f;
  }
};

TimeSeries MakeSeries(std::size_t window = 16) {
  std::vector<double> times;
  std::vector<std::string> names = {"scalar"};
  for (std::size_t i = 0; i < 10; ++i)
    times.push_back(static_cast<double>(i) * 0.1);
  return TimeSeries(std::move(times), std::move(names), CountingLoader{}, window);
}

}  // namespace

TEST(TimeSeries, FrameCountAndTimeMetadata) {
  auto ts = MakeSeries();
  EXPECT_EQ(ts.frame_count(), 10u);
  EXPECT_EQ(ts.field_count(), 1u);
  EXPECT_DOUBLE_EQ(ts.time(0), 0.0);
  EXPECT_DOUBLE_EQ(ts.time(5), 0.5);
  EXPECT_DOUBLE_EQ(ts.time(9), 0.9);
}

TEST(TimeSeries, OutOfRangeFrameReturnsNullptr) {
  auto ts = MakeSeries();
  EXPECT_EQ(ts.frame(10, "scalar"), nullptr);
  EXPECT_EQ(ts.frame(999, "scalar"), nullptr);
}

TEST(TimeSeries, UnknownFieldReturnsNullptr) {
  auto ts = MakeSeries();
  EXPECT_EQ(ts.frame(0, "nope"), nullptr);
}

TEST(TimeSeries, FrameValuesReflectLoaderOutput) {
  auto ts = MakeSeries();
  const Field* f3 = ts.frame(3, "scalar");
  ASSERT_NE(f3, nullptr);
  const auto data = f3->step(0);
  ASSERT_EQ(data.size(), 4u);
  EXPECT_DOUBLE_EQ(data[0], 300.0);
  EXPECT_DOUBLE_EQ(data[1], 301.0);
  EXPECT_DOUBLE_EQ(data[2], 302.0);
  EXPECT_DOUBLE_EQ(data[3], 303.0);
}

TEST(TimeSeries, CacheHitDoesNotInvokeLoader) {
  CountingLoader loader;
  auto calls = loader.calls;
  TimeSeries ts(std::vector<double>{0.0, 1.0, 2.0}, std::vector<std::string>{"scalar"}, loader, 16);
  EXPECT_EQ(calls->load(), 0);
  EXPECT_NE(ts.frame(0, "scalar"), nullptr);
  EXPECT_EQ(calls->load(), 1);
  // Second access to the same (frame, field) should not call the loader again.
  EXPECT_NE(ts.frame(0, "scalar"), nullptr);
  EXPECT_EQ(calls->load(), 1);
}

TEST(TimeSeries, LRUEvictionRespectsWindow) {
  CountingLoader loader;
  auto calls = loader.calls;
  // Window of 2: holds at most 2 frames cached.
  std::vector<double> times;
  for (std::size_t i = 0; i < 5; ++i)
    times.push_back(static_cast<double>(i));
  TimeSeries ts(std::move(times), {"scalar"}, loader, 2);

  // Load frames 0, 1, 2 sequentially. After this, cache should hold only
  // frames 1 and 2 (frame 0 evicted as LRU).
  EXPECT_NE(ts.frame(0, "scalar"), nullptr);
  EXPECT_NE(ts.frame(1, "scalar"), nullptr);
  EXPECT_NE(ts.frame(2, "scalar"), nullptr);
  EXPECT_EQ(calls->load(), 3);
  EXPECT_EQ(ts.cache_occupancy(), 2u);

  // Re-requesting frame 1 hits cache (no new load).
  EXPECT_NE(ts.frame(1, "scalar"), nullptr);
  EXPECT_EQ(calls->load(), 3);

  // Re-requesting frame 0 is a miss (was evicted); causes new load.
  EXPECT_NE(ts.frame(0, "scalar"), nullptr);
  EXPECT_EQ(calls->load(), 4);
}

TEST(TimeSeries, SetCacheWindowEvictsImmediately) {
  auto ts = MakeSeries(/*window=*/16);
  // Populate 5 frames.
  for (std::size_t i = 0; i < 5; ++i)
    (void)ts.frame(i, "scalar");
  EXPECT_EQ(ts.cache_occupancy(), 5u);

  // Shrinking the window evicts down to the new size.
  ts.set_cache_window(2);
  EXPECT_EQ(ts.cache_occupancy(), 2u);
}

TEST(TimeSeries, CachePreloadFillsRange) {
  CountingLoader loader;
  auto calls = loader.calls;
  std::vector<double> times;
  for (std::size_t i = 0; i < 10; ++i)
    times.push_back(static_cast<double>(i));
  TimeSeries ts(std::move(times), {"scalar"}, loader, 16);

  const std::size_t loaded = ts.cache_preload(2, 4);
  EXPECT_EQ(loaded, 4u);
  EXPECT_EQ(calls->load(), 4);
  EXPECT_EQ(ts.cache_occupancy(), 4u);

  // Accessing pre-loaded frames doesn't increment loader calls.
  for (std::size_t i = 2; i < 6; ++i)
    (void)ts.frame(i, "scalar");
  EXPECT_EQ(calls->load(), 4);
}

TEST(TimeSeries, CachePreloadClampsAtEnd) {
  auto ts = MakeSeries();
  // 10-frame series; preload starting at 8 with count 5 should clamp
  // to 2 (frames 8 and 9).
  EXPECT_EQ(ts.cache_preload(8, 5), 2u);
  EXPECT_EQ(ts.cache_preload(15, 5), 0u);  // start beyond end
}

TEST(TimeSeries, WindowZeroDisablesPersistentCache) {
  CountingLoader loader;
  auto calls = loader.calls;
  TimeSeries ts(std::vector<double>{0.0, 1.0, 2.0}, {"scalar"}, loader, 0);
  (void)ts.frame(0, "scalar");
  (void)ts.frame(0, "scalar");  // every call re-loads when window is 0
  EXPECT_EQ(calls->load(), 2);
  EXPECT_LE(ts.cache_occupancy(), 1u);  // transient entry only
}
