/* SPDX-License-Identifier: Apache-2.0
 *
 * souxmar opaque byte buffer for bulk ABI transfer.
 *
 * Motivation: building large meshes (1M+ nodes) one element at a time
 * via souxmar_mesh_add_node / souxmar_mesh_add_cell is dominated by
 * per-call ABI overhead. The bulk path lets a plugin allocate
 * host-owned buffers, write nodes / cell types / connectivity directly
 * into them, then hand the whole package to the host with one ABI call
 * (souxmar_mesh_from_buffers in mesh.h).
 *
 * Memory model:
 *   * Host allocates with souxmar_buffer_new(bytes).
 *   * Plugin writes through souxmar_buffer_data() — pointer is valid
 *     until souxmar_buffer_free() is called on the same handle.
 *   * Plugin or host frees with souxmar_buffer_free().
 *
 * Sprint 5 push 4 ships v1: heap-backed (std::aligned_alloc, 16-byte
 * alignment for SIMD-friendly writes). The handle type is forward-
 * compatible with the v2 mmap-backed implementation per ADR-0006 — no
 * plugin-side change needed when v2 lands.
 *
 * Implementation in src/core/c_abi_buffer.cpp.
 */

#ifndef SOUXMAR_C_BUFFER_H
#define SOUXMAR_C_BUFFER_H

#include "souxmar-c/abi.h"
#include "souxmar-c/types.h"

SOUXMAR_C_BEGIN

/* Allocate a writable buffer of `size_bytes` bytes. The contents are
 * uninitialised. Returns NULL on out-of-memory or if size_bytes == 0. */
souxmar_buffer_t* souxmar_buffer_new(size_t size_bytes);

/* Free a buffer allocated by souxmar_buffer_new. NULL is a no-op. */
void souxmar_buffer_free(souxmar_buffer_t* buffer);

/* Mutable pointer to the first byte. Valid for souxmar_buffer_size()
 * bytes. Returns NULL if `buffer` is NULL. */
void* souxmar_buffer_data(souxmar_buffer_t* buffer);

/* Read-only variant — used by host code that holds a borrowed buffer
 * (e.g. souxmar_mesh_from_buffers reading the plugin's writes). */
const void* souxmar_buffer_data_const(const souxmar_buffer_t* buffer);

/* Size of the buffer in bytes. Returns 0 if `buffer` is NULL. */
size_t souxmar_buffer_size(const souxmar_buffer_t* buffer);

/* Alignment guarantee for the data pointer in bytes. Currently 16; may
 * grow in future versions but never shrinks. Useful for plugins that
 * want to write SIMD vectors without an unaligned-store path. */
size_t souxmar_buffer_alignment(void);

/* ---- v2: memory-mapped backing (Sprint 7 push 3, ABI minor v1.2) ----
 *
 * Out-of-core mesh streaming. A file-backed buffer pages data in/out
 * through the OS page cache rather than holding the working set in
 * RAM. The accessor surface (souxmar_buffer_data, _size, _alignment)
 * is identical to the heap-backed v1; plugins that consume a buffer
 * cannot tell which backing it has, which is the whole point.
 *
 * Memory model:
 *   - SOUXMAR_BUFFER_FLAG_READONLY   : open the file read-only;
 *     souxmar_buffer_data() returns NULL (use _data_const).
 *   - SOUXMAR_BUFFER_FLAG_CREATE     : create the file if missing
 *     (RW only); otherwise an existing file is required.
 *   - flags == 0 ⇒ RW, file must already exist, mapped at file size.
 *
 * On success returns a handle whose lifetime mirrors heap-backed
 * buffers: free via souxmar_buffer_free, which unmaps and closes
 * the underlying fd.
 *
 * Alignment: the OS page-aligned map is already a superset of
 * souxmar_buffer_alignment(), so the same guarantee holds.
 *
 * Errors return NULL. The plugin gets no structured error from this
 * call directly — the host's surrounding ABI bridge (e.g.
 * souxmar_mesh_from_buffers) is the appropriate place to wrap a
 * souxmar_status_t around the operation.
 */

#define SOUXMAR_BUFFER_FLAG_READONLY (1u << 0)
#define SOUXMAR_BUFFER_FLAG_CREATE (1u << 1)

souxmar_buffer_t* souxmar_buffer_new_mmap(const char* path, size_t size_bytes, uint32_t flags);

/* Returns 1 if the buffer's data comes from a file mapping (created
 * by souxmar_buffer_new_mmap), 0 if it's heap-backed (the v1 path),
 * and 0 for NULL. Plugins generally don't need to ask — the data
 * pointer behaves the same either way — but the conformance suite
 * and the bulk-ingest validator use it to distinguish "this buffer
 * is OS-paged" from "this buffer is fully resident." */
int souxmar_buffer_is_mmap(const souxmar_buffer_t* buffer);

SOUXMAR_C_END

#endif /* SOUXMAR_C_BUFFER_H */
