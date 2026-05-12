// SPDX-License-Identifier: Apache-2.0
//
// HeapAccountant — per-call heap-allocation accounting for the plugin
// host's hot path.
//
// Sprint 9 push 9. Closes the Plugin-Host-team named SPRINT_PLAN.md
// story for Sprint 9 ("Per-plugin heap accounting; report leaks via
// instrumentation"). The audit-log Entry shape grows a
// `heap_bytes_delta` field that records the net change in
// process-wide in-use heap bytes across one tool dispatch — useful as
// a leak indicator (steadily growing delta over a session signals a
// plugin that owns memory it isn't releasing), as a per-call cost
// signal in the agent UI, and as a benchmark target the perf-regression
// gate can watch (push 8's dashboard already surfaces it).
//
// Platform support:
//   * Linux (glibc ≥ 2.33)   — `mallinfo2()` returns total `uordblks`
//                              (in-use heap bytes across all arenas);
//                              `supported = true`.
//   * macOS / Windows         — accounting is currently a no-op; the
//                              snapshot returns `supported = false`
//                              and `in_use_bytes = 0`. Adding
//                              `malloc_zone_statistics` (Darwin) and
//                              HeapWalk (Win32) is a follow-on.
//
// Accuracy caveat:
//   `mallinfo2` is process-wide. A heap delta taken around a single
//   plugin dispatch in a single-threaded session is accurate; in
//   multi-threaded execution (Sprint 4 push 2's parallel runner) the
//   delta also captures allocations from sibling threads.
//   Documentation in AI_INTEGRATION.md names this caveat — for
//   leak-detection use (the primary motivation), running with
//   max_workers=1 is the recommended audit configuration.
//
// Cost:
//   `mallinfo2` on glibc is implemented as a per-arena tally that
//   takes a small lock and walks a fixed-size accounting struct;
//   measured at < 1 µs per call on the reference hardware (see
//   bench_heap_accountant). Safe to leave always-on in production.

#pragma once

#include <cstddef>
#include <cstdint>

namespace souxmar::plugin {

class HeapAccountant {
 public:
  // One sample of the process-wide in-use heap. `supported = false`
  // on platforms where the implementation can't query (macOS /
  // Windows for now); callers should treat the delta from / to such a
  // sample as 0 and not surface a misleading "0 bytes" reading as a
  // real measurement in the audit log.
  struct Sample {
    std::size_t in_use_bytes = 0;
    bool supported = false;
  };

  // Returns true on a platform where snapshot() returns meaningful
  // numbers. constexpr-ish in practice (the result doesn't change
  // across the process lifetime), but defined as a function so a
  // future per-process opt-in via env var can plug in here without
  // breaking ABI.
  [[nodiscard]] static bool is_supported() noexcept;

  // Take a heap-usage snapshot. Constant time + no allocations.
  [[nodiscard]] static Sample snapshot() noexcept;

  // Difference in in-use bytes since `start`, signed. Negative values
  // mean the dispatch net-freed bytes (uncommon but legitimate — e.g.
  // a `clear()` tool). Returns 0 when either side reports
  // `supported = false`, so the audit-log field stays a meaningful
  // measurement and not a platform-confused mix of zeros and absences.
  [[nodiscard]] static std::int64_t delta_since(const Sample& start) noexcept;
};

}  // namespace souxmar::plugin
