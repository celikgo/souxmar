/* SPDX-License-Identifier: Apache-2.0
 *
 * include/souxmar-c/surface_stream.h — additive, v1.4 of the C ABI.
 *
 * Layers a renderer-friendly surface view on top of an existing
 * souxmar_mesh_t. The view is *derived*: nothing here mutates the
 * underlying mesh; the host computes face normals + an outer-shell
 * extraction on first open and caches the result keyed by mesh handle.
 *
 * Lifetime: the stream holds an internal reference to the mesh.
 * Freeing the mesh while the stream is open is undefined behavior;
 * close the stream first via souxmar_surface_stream_close.
 *
 * Threading: not thread-safe; the renderer thread is expected to
 * marshal calls to the host worker via the existing dispatcher.
 *
 * See docs/rfcs/0001-viewport-renderer.md and ADR-0037.
 */

#ifndef SOUXMAR_C_SURFACE_STREAM_H
#define SOUXMAR_C_SURFACE_STREAM_H

#include "souxmar-c/abi.h"
#include "souxmar-c/mesh.h"
#include "souxmar-c/status.h"

SOUXMAR_C_BEGIN

typedef struct souxmar_surface_stream_t souxmar_surface_stream_t;

/* Open a derived surface view of `mesh`. Returns NULL on allocation
 * failure or if mesh is NULL. */
souxmar_surface_stream_t* souxmar_surface_stream_open(const souxmar_mesh_t* mesh);

void souxmar_surface_stream_close(souxmar_surface_stream_t* stream);

size_t souxmar_surface_stream_vertex_count(const souxmar_surface_stream_t* s);
size_t souxmar_surface_stream_triangle_count(const souxmar_surface_stream_t* s);

souxmar_status_t souxmar_surface_stream_bounds(const souxmar_surface_stream_t* s,
                                                double out_min[3],
                                                double out_max[3]);

/* SoA reads. Out buffers are sized by the count functions above; passing
 * an undersized capacity returns SOUXMAR_E_INVALID_ARGUMENT. */
souxmar_status_t souxmar_surface_stream_positions(const souxmar_surface_stream_t* s,
                                                   float* out, size_t out_capacity);
souxmar_status_t souxmar_surface_stream_normals(const souxmar_surface_stream_t* s,
                                                 float* out, size_t out_capacity);
souxmar_status_t souxmar_surface_stream_indices(const souxmar_surface_stream_t* s,
                                                 uint32_t* out, size_t out_capacity);
souxmar_status_t souxmar_surface_stream_face_ids(const souxmar_surface_stream_t* s,
                                                  uint32_t* out, size_t out_capacity);
souxmar_status_t souxmar_surface_stream_vertex_ids(const souxmar_surface_stream_t* s,
                                                    uint64_t* out, size_t out_capacity);

SOUXMAR_C_END
#endif /* SOUXMAR_C_SURFACE_STREAM_H */
