// SPDX-License-Identifier: Apache-2.0
//
// Sprint 9 push 9 — unit tests for souxmar::plugin::HeapAccountant.
//
// HeapAccountant is intentionally low-level (a thin wrapper over
// `mallinfo2` on glibc; a no-op elsewhere) so the assertions split
// into two buckets:
//
//   * Tier 1 — assertions that hold on every platform. is_supported()
//     is consistent across snapshot()s; delta_since(unsupported) is 0;
//     two consecutive snapshots taken on a quiet thread differ by an
//     amount the test can tolerate.
//
//   * Tier 2 — Linux-glibc-only. After a deliberate allocation the
//     delta is positive and at least the allocation size; after the
//     matching free the delta drops back to near-zero. These are
//     SKIP'd on macOS / Windows / non-glibc Linux instead of failing
//     so the unit-test suite stays green on the full CI matrix.

#include "souxmar/plugin/heap_accountant.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

using souxmar::plugin::HeapAccountant;

TEST(HeapAccountant, IsSupportedAgreesWithSnapshot) {
  const bool reported = HeapAccountant::is_supported();
  const auto sample = HeapAccountant::snapshot();
  EXPECT_EQ(reported, sample.supported);
  if (!reported) {
    EXPECT_EQ(sample.in_use_bytes, 0u) << "an unsupported platform must report in_use_bytes=0 so "
                                          "callers don't mistake the absence of accounting for a "
                                          "real reading of zero";
  }
}

TEST(HeapAccountant, DeltaBetweenSupportedAndUnsupportedIsZero) {
  // A caller that pairs a supported snapshot with an unsupported one
  // (e.g. between processes or after a build flag flip) gets back 0,
  // never a misleading large positive number from the supported side
  // alone.
  HeapAccountant::Sample fake_unsupported;
  fake_unsupported.supported = false;
  fake_unsupported.in_use_bytes = 0;
  EXPECT_EQ(HeapAccountant::delta_since(fake_unsupported), 0);
}

TEST(HeapAccountant, QuietThreadDeltaIsBoundedlySmall) {
  // No deliberate allocation between the two snapshots. The result
  // isn't strictly zero on a busy process — other threads, async
  // glibc bookkeeping, and the test harness itself can touch the
  // arena — but it shouldn't move by megabytes. 1 MiB is a generous
  // tolerance that catches a future regression where the accountant
  // accidentally pulls a per-thread counter that drifts.
  const auto before = HeapAccountant::snapshot();
  const auto delta = HeapAccountant::delta_since(before);
  if (!before.supported) {
    EXPECT_EQ(delta, 0);
    GTEST_SKIP() << "platform doesn't support mallinfo2; tier-1 only";
  }
  EXPECT_LT(std::abs(delta), 1 << 20)
      << "two consecutive snapshots on a quiet thread shouldn't differ "
         "by more than 1 MiB; got "
      << delta << " bytes";
}

#if defined(__linux__) && defined(__GLIBC__) \
    && ((__GLIBC__ > 2) || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 33))

// Tier-2 tests run only where mallinfo2 is available. The matching
// runtime check guards against a build where the compile-time predicate
// matches but the runtime glibc somehow doesn't (vanishingly unlikely
// but cheap to guard against). On every other platform the file
// compiles to just the tier-1 assertions above.

TEST(HeapAccountantLinux, DeliberateAllocationShowsAsPositiveDelta) {
  if (!HeapAccountant::is_supported()) {
    GTEST_SKIP() << "runtime accountant unsupported";
  }
  // 1 MiB is comfortably above any noise floor — glibc may inline
  // smaller allocations into per-thread cache slots that don't move
  // `uordblks` in lock step. We use std::malloc directly so the
  // accounting tracks the same path real plugins use.
  constexpr std::size_t kSize = 1u << 20;
  const auto before = HeapAccountant::snapshot();
  void* buf = std::malloc(kSize);
  ASSERT_NE(buf, nullptr);
  // Touch the buffer so the kernel actually commits the pages —
  // otherwise some malloc configurations defer the work and the
  // accountant reading lags reality.
  std::memset(buf, 0xAB, kSize);
  const std::int64_t delta_with_buf = HeapAccountant::delta_since(before);
  EXPECT_GE(delta_with_buf, static_cast<std::int64_t>(kSize))
      << "delta after a " << kSize << "-byte alloc should be at least "
      << "that size; got " << delta_with_buf;
  std::free(buf);
}

TEST(HeapAccountantLinux, MatchedAllocFreeReturnsCloseToZero) {
  if (!HeapAccountant::is_supported()) {
    GTEST_SKIP() << "runtime accountant unsupported";
  }
  constexpr std::size_t kSize = 1u << 20;
  const auto before = HeapAccountant::snapshot();
  void* buf = std::malloc(kSize);
  ASSERT_NE(buf, nullptr);
  std::memset(buf, 0, kSize);
  std::free(buf);
  const std::int64_t delta = HeapAccountant::delta_since(before);
  // Glibc may keep a freed arena chunk cached so the delta after a
  // matched alloc+free isn't strictly zero. The tolerance is the same
  // 1 MiB we use for the quiet-thread test — meaningful regressions
  // (where free() stops being recorded) would show megabytes of
  // residual.
  EXPECT_LT(std::abs(delta), 1 << 20)
      << "matched alloc+free should net to near-zero; got " << delta;
}

TEST(HeapAccountantLinux, VectorGrowthShowsAsPositiveDelta) {
  if (!HeapAccountant::is_supported()) {
    GTEST_SKIP() << "runtime accountant unsupported";
  }
  const auto before = HeapAccountant::snapshot();
  // std::vector<double>(N) is the canonical "plugin allocates a
  // working buffer" pattern. N = 1 Mi doubles = 8 MiB — large
  // enough to dominate noise.
  auto v = std::make_unique<std::vector<double>>(1u << 20);
  const std::int64_t delta = HeapAccountant::delta_since(before);
  EXPECT_GE(delta, static_cast<std::int64_t>(8 * (1 << 20)))
      << "8 MiB std::vector<double> should be visible in the delta; "
         "got "
      << delta;
  v.reset();
}

#endif  // glibc >= 2.33
