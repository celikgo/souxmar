// SPDX-License-Identifier: Apache-2.0
//
// HeapAccountant implementation. See include/souxmar/plugin/heap_accountant.h
// for the contract.

#include "souxmar/plugin/heap_accountant.h"

#include <cstddef>
#include <cstdint>

// glibc-only path. mallinfo2() arrived in glibc 2.33 (2021); CI runs
// Ubuntu 22.04 with glibc 2.35. The legacy mallinfo() truncates fields
// to `int`, which silently misreports anything above ~2 GiB — we
// explicitly avoid that and treat older / non-glibc Linux as
// "unsupported" rather than report wrong numbers.
//
// A sanitizer build is excluded, and that is a correctness statement rather
// than a convenience. ASan and TSan replace the allocator wholesale, so
// mallinfo2() goes on reporting a glibc arena that nothing allocates from any
// more: uordblks sits still while the program allocates megabytes. Reporting
// supported=true there would mean handing callers a number that is confidently
// wrong, which is the one thing this file's glibc-version check already exists
// to avoid.
//
// Found the first night the sanitizers ever reached ctest —
// HeapAccountantLinux.DeliberateAllocationShowsAsPositiveDelta and
// .VectorGrowthShowsAsPositiveDelta failed under tsan. They guard on
// is_supported(), so making that honest is what makes them skip; the
// alternative, a skip written into the tests, would have left the library
// still claiming an accounting it cannot perform.
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define SOUXMAR_HEAP_ACCOUNTANT_SANITIZED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#define SOUXMAR_HEAP_ACCOUNTANT_SANITIZED 1
#endif
#endif

#if defined(__linux__) && defined(__GLIBC__)                          \
    && ((__GLIBC__ > 2) || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 33)) \
    && !defined(SOUXMAR_HEAP_ACCOUNTANT_SANITIZED)
#include <malloc.h>
#define SOUXMAR_HEAP_ACCOUNTANT_LINUX_MALLINFO2 1
#endif

namespace souxmar::plugin {

bool HeapAccountant::is_supported() noexcept {
#ifdef SOUXMAR_HEAP_ACCOUNTANT_LINUX_MALLINFO2
  return true;
#else
  return false;
#endif
}

HeapAccountant::Sample HeapAccountant::snapshot() noexcept {
  Sample s;
#ifdef SOUXMAR_HEAP_ACCOUNTANT_LINUX_MALLINFO2
  const struct mallinfo2 mi = mallinfo2();
  // mallinfo2.uordblks: total allocated space in bytes. This is the
  // narrow "currently-in-use heap" reading we want — not arena
  // commits (mi.arena), which would include returnable-but-cached
  // pages and add noise.
  s.in_use_bytes = static_cast<std::size_t>(mi.uordblks);
  s.supported = true;
#endif
  return s;
}

std::int64_t HeapAccountant::delta_since(const Sample& start) noexcept {
  const Sample now = snapshot();
  if (!now.supported || !start.supported)
    return 0;
  // Compute the signed delta in int64 — heap usage in / out of the
  // accountant can drop (a freeing tool) or grow past int32 on
  // industrial-scale meshes, so int64 is the right width. The cast
  // below preserves sign across the subtraction.
  return static_cast<std::int64_t>(now.in_use_bytes)
         - static_cast<std::int64_t>(start.in_use_bytes);
}

}  // namespace souxmar::plugin
