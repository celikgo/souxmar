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

SOUXMAR_C_END

#endif /* SOUXMAR_C_BUFFER_H */
