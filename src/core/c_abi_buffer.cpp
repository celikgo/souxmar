// SPDX-License-Identifier: Apache-2.0
//
// souxmar_buffer_t implementation.
//
// v1 (Sprint 5 push 4): heap-backed via aligned malloc. A small header
// sits immediately before the data slot.
//
// v2 (Sprint 7 push 3, ADR-0006): mmap-backed. The header still sits
// in a small heap allocation but its `kind` field marks the buffer as
// mmap-backed; the data pointer carries the address returned by mmap
// (or MapViewOfFile on Windows). `souxmar_buffer_free` unmaps and
// closes the underlying fd in that case.
//
// The accessor surface (souxmar_buffer_data, _size, _alignment) is
// identical regardless of backing; that's the whole point of the v1
// → v2 forward-compatibility plan in ADR-0006.

#include "souxmar-c/buffer.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <fcntl.h>
#include <unistd.h>
#endif

namespace {

constexpr std::size_t kAlignment = 16;
constexpr std::uint32_t kMagic = 0x53585042;  // 'SXPB' little-endian

// Bit-packed kind discriminator. Lives in the BufferHeader's
// `reserved` field (now renamed `kind`) — v1 zero-initialised it,
// which maps to KindHeap below, so the v1 binary layout is unchanged.
enum BufferKind : std::uint32_t {
  KindHeap = 0,          // v1 heap-backed (default for zero-init headers)
  KindMmap = 1,          // v2 file-mapped, RW
  KindMmapReadOnly = 2,  // v2 file-mapped, read-only
};

// v1 (heap-backed) headers sit immediately before their data slot:
// the slot is owned by the same malloc allocation as the header, so
// freeing requires only `header->allocation`.
//
// v2 (mmap-backed) headers are heap-allocated separately from the
// mapped region; they record the mapping's pointer + length + (on
// POSIX) the fd so souxmar_buffer_free can unmap + close.
struct BufferHeader {
  std::uint32_t magic;
  std::uint32_t kind;  // BufferKind — was `reserved` in v1
  std::size_t size;
  void* allocation;  // heap-backed: unaligned malloc base; mmap: NULL
  // ---- v2 mmap-only fields. Zero for heap-backed. ----
  void* map_addr;  // mmap return / MapViewOfFile return
  std::size_t map_length;
#if defined(_WIN32)
  HANDLE map_handle;   // CreateFileMappingA handle
  HANDLE file_handle;  // CreateFileA handle
#else
  int map_fd;  // POSIX file descriptor
  int _pad;    // align to 8-byte word
#endif
};

static_assert(sizeof(BufferHeader) % kAlignment == 0,
              "BufferHeader must align to kAlignment so the heap-backed data slot stays aligned");

}  // namespace

extern "C" {

souxmar_buffer_t* souxmar_buffer_new(std::size_t size_bytes) {
  if (size_bytes == 0)
    return nullptr;

  if (size_bytes > SIZE_MAX - sizeof(BufferHeader) - kAlignment) {
    return nullptr;
  }
  const std::size_t total = sizeof(BufferHeader) + size_bytes + kAlignment;
  void* raw = std::malloc(total);
  if (!raw)
    return nullptr;

  // Place the header so the data area immediately following it is
  // aligned. We compute the data pointer first, then back the header
  // pointer up by sizeof(BufferHeader).
  const auto raw_addr = reinterpret_cast<std::uintptr_t>(raw);
  const auto data_addr = (raw_addr + sizeof(BufferHeader) + (kAlignment - 1))
                         & ~static_cast<std::uintptr_t>(kAlignment - 1);
  auto* header = reinterpret_cast<BufferHeader*>(data_addr - sizeof(BufferHeader));
  std::memset(header, 0, sizeof(*header));
  header->magic = kMagic;
  header->kind = KindHeap;
  header->size = size_bytes;
  header->allocation = raw;
#if !defined(_WIN32)
  header->map_fd = -1;
#endif
  return reinterpret_cast<souxmar_buffer_t*>(header);
}

souxmar_buffer_t* souxmar_buffer_new_mmap(const char* path,
                                          std::size_t size_bytes,
                                          std::uint32_t flags) {
  if (!path)
    return nullptr;
  const bool read_only = (flags & SOUXMAR_BUFFER_FLAG_READONLY) != 0;
  const bool create = (flags & SOUXMAR_BUFFER_FLAG_CREATE) != 0;
  if (read_only && create)
    return nullptr;  // creating implies RW

  auto* header = static_cast<BufferHeader*>(std::malloc(sizeof(BufferHeader)));
  if (!header)
    return nullptr;
  std::memset(header, 0, sizeof(*header));
  header->magic = kMagic;
  header->kind = read_only ? KindMmapReadOnly : KindMmap;
  header->allocation = nullptr;  // mmap-backed: nothing to free()
#if !defined(_WIN32)
  header->map_fd = -1;
#endif

#if defined(_WIN32)
  // ---- Windows: CreateFileA + CreateFileMappingA + MapViewOfFile ----
  const DWORD access_flags = read_only ? GENERIC_READ : (GENERIC_READ | GENERIC_WRITE);
  const DWORD disposition = create ? OPEN_ALWAYS : OPEN_EXISTING;
  HANDLE fh = CreateFileA(path,
                          access_flags,
                          FILE_SHARE_READ | FILE_SHARE_WRITE,
                          nullptr,
                          disposition,
                          FILE_ATTRIBUTE_NORMAL,
                          nullptr);
  if (fh == INVALID_HANDLE_VALUE) {
    std::free(header);
    return nullptr;
  }

  std::size_t map_size = size_bytes;
  if (!read_only && size_bytes > 0) {
    LARGE_INTEGER li;
    li.QuadPart = static_cast<LONGLONG>(size_bytes);
    if (!SetFilePointerEx(fh, li, nullptr, FILE_BEGIN) || !SetEndOfFile(fh)) {
      CloseHandle(fh);
      std::free(header);
      return nullptr;
    }
  } else if (read_only) {
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(fh, &sz)) {
      CloseHandle(fh);
      std::free(header);
      return nullptr;
    }
    map_size = static_cast<std::size_t>(sz.QuadPart);
  }
  if (map_size == 0) {
    CloseHandle(fh);
    std::free(header);
    return nullptr;
  }

  const DWORD prot = read_only ? PAGE_READONLY : PAGE_READWRITE;
  const DWORD view_acc = read_only ? FILE_MAP_READ : FILE_MAP_WRITE;
  HANDLE mh = CreateFileMappingA(fh, nullptr, prot, 0, 0, nullptr);
  if (!mh) {
    CloseHandle(fh);
    std::free(header);
    return nullptr;
  }
  void* addr = MapViewOfFile(mh, view_acc, 0, 0, map_size);
  if (!addr) {
    CloseHandle(mh);
    CloseHandle(fh);
    std::free(header);
    return nullptr;
  }

  header->size = map_size;
  header->map_addr = addr;
  header->map_length = map_size;
  header->map_handle = mh;
  header->file_handle = fh;
#else
  // ---- POSIX: open + (optional ftruncate) + mmap ----
  int open_flags = read_only ? O_RDONLY : O_RDWR;
  if (create)
    open_flags |= O_CREAT;
  const mode_t open_mode = 0644;
  const int fd = ::open(path, open_flags, open_mode);
  if (fd < 0) {
    std::free(header);
    return nullptr;
  }

  std::size_t map_size = size_bytes;
  if (!read_only && size_bytes > 0) {
    if (::ftruncate(fd, static_cast<off_t>(size_bytes)) != 0) {
      ::close(fd);
      std::free(header);
      return nullptr;
    }
  } else if (read_only) {
    struct stat st;
    if (::fstat(fd, &st) != 0) {
      ::close(fd);
      std::free(header);
      return nullptr;
    }
    map_size = static_cast<std::size_t>(st.st_size);
  }
  if (map_size == 0) {
    ::close(fd);
    std::free(header);
    return nullptr;
  }

  const int prot = read_only ? PROT_READ : (PROT_READ | PROT_WRITE);
  void* addr = ::mmap(nullptr, map_size, prot, MAP_SHARED, fd, 0);
  if (addr == MAP_FAILED) {
    ::close(fd);
    std::free(header);
    return nullptr;
  }

  header->size = map_size;
  header->map_addr = addr;
  header->map_length = map_size;
  header->map_fd = fd;
#endif

  return reinterpret_cast<souxmar_buffer_t*>(header);
}

void souxmar_buffer_free(souxmar_buffer_t* buffer) {
  if (!buffer)
    return;
  auto* header = reinterpret_cast<BufferHeader*>(buffer);
  // Magic check: catches double-frees + non-buffer pointers in debug.
  // In release we still free regardless to match the "NULL is no-op"
  // contract — defensive on the assumption that getting here with a
  // wrong pointer is itself a bug worth reporting via the inevitable
  // crash, not silently swallowing.
  if (header->magic != kMagic) {
    return;
  }
  const auto kind = header->kind;
  header->magic = 0;  // poison first so double-free is a no-op

  if (kind == KindHeap) {
    std::free(header->allocation);
    return;
  }
  // Mmap-backed: unmap + close the file. Then free the small header
  // allocation we own separately from the mapping.
#if defined(_WIN32)
  if (header->map_addr)
    UnmapViewOfFile(header->map_addr);
  if (header->map_handle)
    CloseHandle(header->map_handle);
  if (header->file_handle && header->file_handle != INVALID_HANDLE_VALUE) {
    CloseHandle(header->file_handle);
  }
#else
  if (header->map_addr && header->map_length > 0) {
    ::munmap(header->map_addr, header->map_length);
  }
  if (header->map_fd >= 0)
    ::close(header->map_fd);
#endif
  std::free(header);
}

void* souxmar_buffer_data(souxmar_buffer_t* buffer) {
  if (!buffer)
    return nullptr;
  auto* header = reinterpret_cast<BufferHeader*>(buffer);
  if (header->magic != kMagic)
    return nullptr;
  if (header->kind == KindMmapReadOnly) {
    // Read-only mapping: writeable view is forbidden. Use
    // souxmar_buffer_data_const for the read view.
    return nullptr;
  }
  if (header->kind == KindMmap) {
    return header->map_addr;
  }
  // Heap-backed v1.
  return reinterpret_cast<std::uint8_t*>(header) + sizeof(BufferHeader);
}

const void* souxmar_buffer_data_const(const souxmar_buffer_t* buffer) {
  if (!buffer)
    return nullptr;
  const auto* header = reinterpret_cast<const BufferHeader*>(buffer);
  if (header->magic != kMagic)
    return nullptr;
  if (header->kind == KindMmap || header->kind == KindMmapReadOnly) {
    return header->map_addr;
  }
  return reinterpret_cast<const std::uint8_t*>(header) + sizeof(BufferHeader);
}

std::size_t souxmar_buffer_size(const souxmar_buffer_t* buffer) {
  if (!buffer)
    return 0;
  const auto* header = reinterpret_cast<const BufferHeader*>(buffer);
  if (header->magic != kMagic)
    return 0;
  return header->size;
}

std::size_t souxmar_buffer_alignment(void) {
  return kAlignment;
}

int souxmar_buffer_is_mmap(const souxmar_buffer_t* buffer) {
  if (!buffer)
    return 0;
  const auto* header = reinterpret_cast<const BufferHeader*>(buffer);
  if (header->magic != kMagic)
    return 0;
  return (header->kind == KindMmap || header->kind == KindMmapReadOnly) ? 1 : 0;
}

}  // extern "C"
