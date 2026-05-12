// SPDX-License-Identifier: Apache-2.0
//
// Stub backing for surface_stream.h (v1.4 ABI ratchet, ADR-0037).
//
// Sprint 25 PR 1 lands this stub: the ABI surface exists, the
// conformance suite can link against it, but every entry point
// returns NOT_IMPLEMENTED / NULL / 0. The real outer-shell extraction
// + normal computation lands in Sprint 25 PR 2 once the bridge has a
// concrete consumer to test against.
//
// The struct is intentionally empty — there is no implementation
// state to hold yet. Future PRs will give it a vptr to a souxmar::core
// cached-surface object.

#include "souxmar-c/surface_stream.h"

#include <cstddef>
#include <cstdint>

struct souxmar_surface_stream_t {};

extern "C" {

souxmar_surface_stream_t* souxmar_surface_stream_open(const souxmar_mesh_t* /*mesh*/) {
  // Stub — Sprint 25 PR 2 supplies the real implementation.
  return nullptr;
}

void souxmar_surface_stream_close(souxmar_surface_stream_t* stream) {
  delete stream;
}

size_t souxmar_surface_stream_vertex_count(const souxmar_surface_stream_t* /*s*/) {
  return 0;
}

size_t souxmar_surface_stream_triangle_count(const souxmar_surface_stream_t* /*s*/) {
  return 0;
}

souxmar_status_t souxmar_surface_stream_bounds(const souxmar_surface_stream_t* /*s*/,
                                                double /*out_min*/[3],
                                                double /*out_max*/[3]) {
  return souxmar_status_error(SOUXMAR_E_NOT_IMPLEMENTED, "surface_stream stub");
}

souxmar_status_t souxmar_surface_stream_positions(const souxmar_surface_stream_t* /*s*/,
                                                   float* /*out*/, size_t /*out_capacity*/) {
  return souxmar_status_error(SOUXMAR_E_NOT_IMPLEMENTED, "surface_stream stub");
}

souxmar_status_t souxmar_surface_stream_normals(const souxmar_surface_stream_t* /*s*/,
                                                 float* /*out*/, size_t /*out_capacity*/) {
  return souxmar_status_error(SOUXMAR_E_NOT_IMPLEMENTED, "surface_stream stub");
}

souxmar_status_t souxmar_surface_stream_indices(const souxmar_surface_stream_t* /*s*/,
                                                 uint32_t* /*out*/, size_t /*out_capacity*/) {
  return souxmar_status_error(SOUXMAR_E_NOT_IMPLEMENTED, "surface_stream stub");
}

souxmar_status_t souxmar_surface_stream_face_ids(const souxmar_surface_stream_t* /*s*/,
                                                  uint32_t* /*out*/, size_t /*out_capacity*/) {
  return souxmar_status_error(SOUXMAR_E_NOT_IMPLEMENTED, "surface_stream stub");
}

souxmar_status_t souxmar_surface_stream_vertex_ids(const souxmar_surface_stream_t* /*s*/,
                                                    uint64_t* /*out*/, size_t /*out_capacity*/) {
  return souxmar_status_error(SOUXMAR_E_NOT_IMPLEMENTED, "surface_stream stub");
}

}  // extern "C"
