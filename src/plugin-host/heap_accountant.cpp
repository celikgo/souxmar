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
#if defined(__linux__) && defined(__GLIBC__) \
    && ((__GLIBC__ > 2) || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 33))
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
