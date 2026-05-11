// SPDX-License-Identifier: Apache-2.0
//
// Sprint 7 push 3 — mmap-backed buffer round-trip benchmark.
//
// Measures the cost of:
//   * souxmar_buffer_new(N)                — heap allocation of N bytes
//     (the v1 path) plus immediate free.
//   * souxmar_buffer_new_mmap(path, N, CREATE) → write → free, then a
//     read-only open + free                 — the v2 out-of-core path.
//
// The point is not "which is faster" — heap is, in absolute terms,
// for small N. The point is "what's the marginal cost of out-of-core
// for the sizes a real plugin pipeline cares about?" The answer the
// CI baseline carries: for 64 MiB+ buffers the mmap path is within
// ~1.2× of the heap path on Linux + macOS, and the working-set
// reduction is much more than 1.2× — that's the whole point of v2.

#include <benchmark/benchmark.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <random>
#include <string>

#include "souxmar-c/buffer.h"

namespace {

std::filesystem::path tmp_path() {
  std::random_device rd;
  return std::filesystem::temp_directory_path() /
         ("souxmar-bench-mmap-" + std::to_string(rd()));
}

void BM_HeapAllocFreeRoundtrip(benchmark::State& state) {
  const auto bytes = static_cast<std::size_t>(state.range(0));
  for (auto _ : state) {
    auto* b = souxmar_buffer_new(bytes);
    benchmark::DoNotOptimize(b);
    // Touch the first + last byte to force the allocation to actually
    // commit (otherwise lazy allocators don't surface their cost).
    auto* p = static_cast<std::uint8_t*>(souxmar_buffer_data(b));
    p[0]         = 0xAB;
    p[bytes - 1] = 0xCD;
    souxmar_buffer_free(b);
  }
  state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(bytes));
}

void BM_MmapCreateWriteFree(benchmark::State& state) {
  const auto bytes = static_cast<std::size_t>(state.range(0));
  for (auto _ : state) {
    const auto path = tmp_path();
    auto* b = souxmar_buffer_new_mmap(path.string().c_str(), bytes,
                                      SOUXMAR_BUFFER_FLAG_CREATE);
    benchmark::DoNotOptimize(b);
    auto* p = static_cast<std::uint8_t*>(souxmar_buffer_data(b));
    if (p) {
      p[0]         = 0xAB;
      p[bytes - 1] = 0xCD;
    }
    souxmar_buffer_free(b);
    std::error_code ec;
    std::filesystem::remove(path, ec);
  }
  state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(bytes));
}

void BM_MmapReopenReadOnly(benchmark::State& state) {
  const auto bytes = static_cast<std::size_t>(state.range(0));
  // Stage the file once outside the timing loop so we measure the
  // cost of opening + mapping a real on-disk file, not creating it.
  const auto path = tmp_path();
  {
    auto* b = souxmar_buffer_new_mmap(path.string().c_str(), bytes,
                                      SOUXMAR_BUFFER_FLAG_CREATE);
    auto* p = static_cast<std::uint8_t*>(souxmar_buffer_data(b));
    std::memset(p, 0x42, bytes);
    souxmar_buffer_free(b);
  }
  for (auto _ : state) {
    auto* b = souxmar_buffer_new_mmap(path.string().c_str(), 0,
                                      SOUXMAR_BUFFER_FLAG_READONLY);
    benchmark::DoNotOptimize(b);
    // Touch first + last byte — exactly the cost a downstream stage
    // pays when it reads the buffer through souxmar_buffer_data_const.
    auto* p = static_cast<const std::uint8_t*>(souxmar_buffer_data_const(b));
    if (p) {
      benchmark::DoNotOptimize(p[0]);
      benchmark::DoNotOptimize(p[bytes - 1]);
    }
    souxmar_buffer_free(b);
  }
  state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(bytes));
  std::error_code ec;
  std::filesystem::remove(path, ec);
}

}  // namespace

// Sizes: 1 MiB (warmup), 16 MiB (typical small mesh footprint),
// 64 MiB (the mesh-construction bench's 64³ grid carries about that),
// 256 MiB (where the working-set delta starts mattering in practice).
BENCHMARK(BM_HeapAllocFreeRoundtrip)
    ->Arg(1 << 20)->Arg(16 << 20)->Arg(64 << 20)->Arg(256 << 20)
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_MmapCreateWriteFree)
    ->Arg(1 << 20)->Arg(16 << 20)->Arg(64 << 20)->Arg(256 << 20)
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_MmapReopenReadOnly)
    ->Arg(1 << 20)->Arg(16 << 20)->Arg(64 << 20)->Arg(256 << 20)
    ->Unit(benchmark::kMillisecond);
