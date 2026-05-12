// SPDX-License-Identifier: Apache-2.0
//
// Stub backing for timeseries.h (v1.9 ABI ratchet, ADR-0042).
//
// Sprint 32 PR 1 lands this stub; PR 2 supplies the PVD reader +
// LRU cache via examples/plugins/vtu-reader extension.

#include "souxmar-c/timeseries.h"

#include <cstddef>
#include <cstdint>

struct souxmar_timeseries_t {};

extern "C" {

souxmar_timeseries_t* souxmar_timeseries_open(const char* /*path*/) {
  return nullptr;
}

void souxmar_timeseries_close(souxmar_timeseries_t* series) {
  delete series;
}

size_t souxmar_timeseries_frame_count(const souxmar_timeseries_t* /*series*/) {
  return 0;
}

souxmar_status_t souxmar_timeseries_time(const souxmar_timeseries_t* /*series*/,
                                          size_t /*frame_index*/,
                                          double* /*out_time*/) {
  return souxmar_status_error(SOUXMAR_E_NOT_IMPLEMENTED, "timeseries stub");
}

size_t souxmar_timeseries_field_count(const souxmar_timeseries_t* /*series*/) {
  return 0;
}

const char* souxmar_timeseries_field_name(const souxmar_timeseries_t* /*series*/,
                                           size_t /*field_index*/) {
  return nullptr;
}

const souxmar_field_t* souxmar_timeseries_frame(souxmar_timeseries_t* /*series*/,
                                                  size_t /*frame_index*/,
                                                  const char* /*field_name*/) {
  return nullptr;
}

souxmar_status_t souxmar_timeseries_cache_window(souxmar_timeseries_t* /*series*/,
                                                  size_t /*window_size*/) {
  return souxmar_status_error(SOUXMAR_E_NOT_IMPLEMENTED, "timeseries stub");
}

souxmar_status_t souxmar_timeseries_cache_preload(souxmar_timeseries_t* /*series*/,
                                                   size_t /*start_frame*/,
                                                   size_t /*count*/) {
  return souxmar_status_error(SOUXMAR_E_NOT_IMPLEMENTED, "timeseries stub");
}

}  // extern "C"
