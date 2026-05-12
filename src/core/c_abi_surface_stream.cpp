// SPDX-License-Identifier: Apache-2.0
//
// C ABI bridge for surface_stream.h (v1.4, ADR-0037).
//
// Sprint 25 PR 1 — real implementation. Delegates to
// souxmar::core::SurfaceStream which lives next to this file.

#include "souxmar/core/mesh.h"
#include "souxmar/core/surface_stream.h"

#include "souxmar-c/surface_stream.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {

souxmar::core::SurfaceStream* as_cpp(souxmar_surface_stream_t* p) noexcept {
  return reinterpret_cast<souxmar::core::SurfaceStream*>(p);
}

const souxmar::core::SurfaceStream* as_cpp(const souxmar_surface_stream_t* p) noexcept {
  return reinterpret_cast<const souxmar::core::SurfaceStream*>(p);
}

souxmar_surface_stream_t* as_c(souxmar::core::SurfaceStream* p) noexcept {
  return reinterpret_cast<souxmar_surface_stream_t*>(p);
}

const souxmar::core::Mesh* mesh_as_cpp(const souxmar_mesh_t* p) noexcept {
  return reinterpret_cast<const souxmar::core::Mesh*>(p);
}

template <typename T>
souxmar_status_t copy_span_into(std::span<const T> src, T* out, std::size_t capacity) {
  if (out == nullptr || capacity < src.size()) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "surface_stream: out buffer too small");
  }
  std::memcpy(out, src.data(), src.size() * sizeof(T));
  return souxmar_status_ok();
}

}  // namespace

extern "C" {

souxmar_surface_stream_t* souxmar_surface_stream_open(const souxmar_mesh_t* mesh) {
  if (mesh == nullptr)
    return nullptr;
  try {
    return as_c(new souxmar::core::SurfaceStream(*mesh_as_cpp(mesh)));
  } catch (...) {
    return nullptr;
  }
}

void souxmar_surface_stream_close(souxmar_surface_stream_t* stream) {
  delete as_cpp(stream);
}

size_t souxmar_surface_stream_vertex_count(const souxmar_surface_stream_t* s) {
  return s ? as_cpp(s)->vertex_count() : 0;
}

size_t souxmar_surface_stream_triangle_count(const souxmar_surface_stream_t* s) {
  return s ? as_cpp(s)->triangle_count() : 0;
}

souxmar_status_t souxmar_surface_stream_bounds(const souxmar_surface_stream_t* s,
                                               double out_min[3],
                                               double out_max[3]) {
  if (s == nullptr || out_min == nullptr || out_max == nullptr) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "surface_stream_bounds: null argument");
  }
  const auto bmin = as_cpp(s)->bounds_min();
  const auto bmax = as_cpp(s)->bounds_max();
  for (std::size_t i = 0; i < 3; ++i) {
    out_min[i] = bmin[i];
    out_max[i] = bmax[i];
  }
  return souxmar_status_ok();
}

souxmar_status_t souxmar_surface_stream_positions(const souxmar_surface_stream_t* s,
                                                  float* out,
                                                  size_t out_capacity) {
  if (s == nullptr) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "surface_stream: null handle");
  }
  return copy_span_into(as_cpp(s)->positions(), out, out_capacity);
}

souxmar_status_t souxmar_surface_stream_normals(const souxmar_surface_stream_t* s,
                                                float* out,
                                                size_t out_capacity) {
  if (s == nullptr) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "surface_stream: null handle");
  }
  return copy_span_into(as_cpp(s)->normals(), out, out_capacity);
}

souxmar_status_t souxmar_surface_stream_indices(const souxmar_surface_stream_t* s,
                                                uint32_t* out,
                                                size_t out_capacity) {
  if (s == nullptr) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "surface_stream: null handle");
  }
  return copy_span_into(as_cpp(s)->indices(), out, out_capacity);
}

souxmar_status_t souxmar_surface_stream_face_ids(const souxmar_surface_stream_t* s,
                                                 uint32_t* out,
                                                 size_t out_capacity) {
  if (s == nullptr) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "surface_stream: null handle");
  }
  return copy_span_into(as_cpp(s)->face_ids(), out, out_capacity);
}

souxmar_status_t souxmar_surface_stream_vertex_ids(const souxmar_surface_stream_t* s,
                                                   uint64_t* out,
                                                   size_t out_capacity) {
  if (s == nullptr) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "surface_stream: null handle");
  }
  return copy_span_into(as_cpp(s)->vertex_ids(), out, out_capacity);
}

}  // extern "C"
