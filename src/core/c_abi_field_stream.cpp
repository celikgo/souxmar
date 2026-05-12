// SPDX-License-Identifier: Apache-2.0
//
// Stub backing for field_stream.h (v1.5 ABI ratchet, ADR-0038).
//
// Sprint 27 PR 1 lands this stub; PR 2 supplies the real
// min/max-caching impl backed by souxmar::core::Field accessors.

#include "souxmar-c/field_stream.h"

#include <cstddef>
#include <cstdint>

struct souxmar_field_stream_t {};

extern "C" {

souxmar_field_stream_t* souxmar_field_stream_open(const souxmar_field_t* /*field*/) {
  return nullptr;
}

void souxmar_field_stream_close(souxmar_field_stream_t* stream) {
  delete stream;
}

size_t souxmar_field_stream_count(const souxmar_field_stream_t* /*s*/) {
  return 0;
}

uint8_t souxmar_field_stream_components(const souxmar_field_stream_t* /*s*/) {
  return 0;
}

souxmar_status_t souxmar_field_stream_range(const souxmar_field_stream_t* /*s*/,
                                             double* /*out_min*/,
                                             double* /*out_max*/) {
  return souxmar_status_error(SOUXMAR_E_NOT_IMPLEMENTED, "field_stream stub");
}

const char* souxmar_field_stream_units(const souxmar_field_stream_t* /*s*/) {
  return "";
}

souxmar_status_t souxmar_field_stream_values(const souxmar_field_stream_t* /*s*/,
                                              float* /*out*/, size_t /*out_capacity*/) {
  return souxmar_status_error(SOUXMAR_E_NOT_IMPLEMENTED, "field_stream stub");
}

}  // extern "C"
