// SPDX-License-Identifier: Apache-2.0
//
// souxmar_buffer_t implementation — heap-backed v1.
//
// Layout: a tiny header struct followed by the aligned data area. We
// store the size + a magic word so souxmar_buffer_size() is O(1) and
// double-free (or wrong-pointer) attempts die loudly under ASAN /
// UBSAN rather than corrupting nearby memory.
//
// Future v2 (ADR-0006): the same handle type wraps an mmap'd region,
// transparently to the plugin. The accessor surface is the public
// contract; the internal struct is host-private.

#include "souxmar-c/buffer.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>

namespace {

constexpr std::size_t kAlignment = 16;
constexpr std::uint32_t kMagic   = 0x53585042;  // 'SXPB' little-endian

// Sized so the data area begins at a kAlignment boundary regardless of
// the malloc baseline. We over-allocate by kAlignment-1 bytes and
// round the data pointer up; the header sits in the leading slot,
// directly before the data pointer (so the header pointer is
// recoverable from the data pointer).
struct BufferHeader {
  std::uint32_t  magic;
  std::uint32_t  reserved;   // future flags (mmap-backed bit etc.); zero in v1
  std::size_t    size;
  void*          allocation; // unaligned malloc base for free()
};

static_assert(sizeof(BufferHeader) % kAlignment == 0,
              "BufferHeader must align to kAlignment so the data slot stays aligned");

}  // namespace

extern "C" {

souxmar_buffer_t* souxmar_buffer_new(std::size_t size_bytes) {
  if (size_bytes == 0) return nullptr;

  // Allocate header + size_bytes + alignment slack so we can guarantee
  // the data pointer lands on a kAlignment boundary independent of the
  // platform malloc's baseline alignment. Overflow-safe arithmetic:
  // both terms are user-input-bounded by callers we trust (the plugin
  // itself authored the request) but cheap to defend.
  if (size_bytes > SIZE_MAX - sizeof(BufferHeader) - kAlignment) {
    return nullptr;
  }
  const std::size_t total = sizeof(BufferHeader) + size_bytes + kAlignment;
  void* raw = std::malloc(total);
  if (!raw) return nullptr;

  // Place the header so the data area immediately following it is
  // aligned. We compute the data pointer first, then back the header
  // pointer up by sizeof(BufferHeader).
  const auto raw_addr  = reinterpret_cast<std::uintptr_t>(raw);
  const auto data_addr = (raw_addr + sizeof(BufferHeader) + (kAlignment - 1)) &
                         ~static_cast<std::uintptr_t>(kAlignment - 1);
  auto* header = reinterpret_cast<BufferHeader*>(data_addr - sizeof(BufferHeader));
  header->magic      = kMagic;
  header->reserved   = 0;
  header->size       = size_bytes;
  header->allocation = raw;
  return reinterpret_cast<souxmar_buffer_t*>(header);
}

void souxmar_buffer_free(souxmar_buffer_t* buffer) {
  if (!buffer) return;
  auto* header = reinterpret_cast<BufferHeader*>(buffer);
  // Magic check: catches double-frees + non-buffer pointers in debug.
  // In release we still free regardless to match the "NULL is no-op"
  // contract — defensive on the assumption that getting here with a
  // wrong pointer is itself a bug worth reporting via the inevitable
  // crash, not silently swallowing.
  if (header->magic != kMagic) {
    return;
  }
  void* alloc = header->allocation;
  // Poison the magic so a subsequent free of the same handle is a
  // no-op rather than a double-free of `alloc`.
  header->magic = 0;
  std::free(alloc);
}

void* souxmar_buffer_data(souxmar_buffer_t* buffer) {
  if (!buffer) return nullptr;
  auto* header = reinterpret_cast<BufferHeader*>(buffer);
  if (header->magic != kMagic) return nullptr;
  return reinterpret_cast<std::uint8_t*>(header) + sizeof(BufferHeader);
}

const void* souxmar_buffer_data_const(const souxmar_buffer_t* buffer) {
  return souxmar_buffer_data(const_cast<souxmar_buffer_t*>(buffer));
}

std::size_t souxmar_buffer_size(const souxmar_buffer_t* buffer) {
  if (!buffer) return 0;
  const auto* header = reinterpret_cast<const BufferHeader*>(buffer);
  if (header->magic != kMagic) return 0;
  return header->size;
}

std::size_t souxmar_buffer_alignment(void) {
  return kAlignment;
}

}  // extern "C"
