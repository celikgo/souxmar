// SPDX-License-Identifier: Apache-2.0
//
// C ABI bridge for field_stream.h (v1.5, ADR-0038).
//
// Sprint 27 PR 1 — real implementation. Delegates to
// souxmar::core::FieldStream which lives next to this file.

#include "souxmar/core/field.h"
#include "souxmar/core/field_stream.h"

#include "souxmar-c/field_stream.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {

souxmar::core::FieldStream* as_cpp(souxmar_field_stream_t* p) noexcept {
  return reinterpret_cast<souxmar::core::FieldStream*>(p);
}

const souxmar::core::FieldStream* as_cpp(const souxmar_field_stream_t* p) noexcept {
  return reinterpret_cast<const souxmar::core::FieldStream*>(p);
}

souxmar_field_stream_t* as_c(souxmar::core::FieldStream* p) noexcept {
  return reinterpret_cast<souxmar_field_stream_t*>(p);
}

const souxmar::core::Field* field_as_cpp(const souxmar_field_t* p) noexcept {
  return reinterpret_cast<const souxmar::core::Field*>(p);
}

}  // namespace

extern "C" {

souxmar_field_stream_t* souxmar_field_stream_open(const souxmar_field_t* field) {
  if (field == nullptr)
    return nullptr;
  try {
    return as_c(new souxmar::core::FieldStream(*field_as_cpp(field)));
  } catch (...) {
    return nullptr;
  }
}

void souxmar_field_stream_close(souxmar_field_stream_t* stream) {
  delete as_cpp(stream);
}

size_t souxmar_field_stream_count(const souxmar_field_stream_t* s) {
  return s ? as_cpp(s)->count() : 0;
}

uint8_t souxmar_field_stream_components(const souxmar_field_stream_t* s) {
  return s ? as_cpp(s)->components() : 0;
}

souxmar_status_t souxmar_field_stream_range(const souxmar_field_stream_t* s,
                                            double* out_min,
                                            double* out_max) {
  if (s == nullptr || out_min == nullptr || out_max == nullptr) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "field_stream_range: null argument");
  }
  const auto rmin = as_cpp(s)->range_min();
  const auto rmax = as_cpp(s)->range_max();
  // Out buffers are caller-allocated; the caller is expected to size them
  // to components(). We can't bound-check here without an out_capacity
  // argument the header doesn't provide — the caller-owns-capacity rule
  // matches souxmar_surface_stream_bounds's fixed-size out arrays.
  std::memcpy(out_min, rmin.data(), rmin.size() * sizeof(double));
  std::memcpy(out_max, rmax.data(), rmax.size() * sizeof(double));
  return souxmar_status_ok();
}

const char* souxmar_field_stream_units(const souxmar_field_stream_t* s) {
  if (s == nullptr)
    return "";
  const auto sv = as_cpp(s)->units();
  return sv.empty() ? "" : sv.data();  // FieldStream::units owns the storage; not freed
}

souxmar_status_t souxmar_field_stream_values(const souxmar_field_stream_t* s,
                                             float* out,
                                             size_t out_capacity) {
  if (s == nullptr) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "field_stream_values: null handle");
  }
  const auto vals = as_cpp(s)->values();
  if (out == nullptr || out_capacity < vals.size()) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT,
                                "field_stream_values: out buffer too small");
  }
  std::memcpy(out, vals.data(), vals.size() * sizeof(float));
  return souxmar_status_ok();
}

}  // extern "C"
