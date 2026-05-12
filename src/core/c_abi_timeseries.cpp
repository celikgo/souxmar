// SPDX-License-Identifier: Apache-2.0
//
// C ABI bridge for timeseries.h (v1.9, ADR-0042).
//
// Sprint 32 PR 1 (partial) — the LRU cache + TimeSeries class is real;
// the souxmar_timeseries_open(path) entry point still returns NULL
// because it requires the reader.* plugin dispatch to find a parser
// for the file extension (`.pvd` → PVD reader). That dispatch lands
// in Sprint 32 PR 2 alongside the vtu-reader extension that produces
// the per-frame Fields. The rest of the C ABI delegates to the real
// C++ class — if a caller constructs a TimeSeries via the C++ API
// and reinterpret_casts to the C handle, every operation below works
// correctly today.

#include "souxmar/core/field.h"
#include "souxmar/core/time_series.h"

#include "souxmar-c/timeseries.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace {

souxmar::core::TimeSeries* as_cpp(souxmar_timeseries_t* p) noexcept {
  return reinterpret_cast<souxmar::core::TimeSeries*>(p);
}

const souxmar::core::TimeSeries* as_cpp(const souxmar_timeseries_t* p) noexcept {
  return reinterpret_cast<const souxmar::core::TimeSeries*>(p);
}

const souxmar_field_t* field_as_c(const souxmar::core::Field* p) noexcept {
  return reinterpret_cast<const souxmar_field_t*>(p);
}

}  // namespace

extern "C" {

souxmar_timeseries_t* souxmar_timeseries_open(const char* /*path*/) {
  // No reader.* plugin dispatch yet — Sprint 32 PR 2 wires that in.
  // Until then, returning NULL tells callers the surface isn't
  // dispatchable on disk paths. The C++ class is constructible
  // directly for testing.
  return nullptr;
}

void souxmar_timeseries_close(souxmar_timeseries_t* series) {
  delete as_cpp(series);
}

size_t souxmar_timeseries_frame_count(const souxmar_timeseries_t* series) {
  return series ? as_cpp(series)->frame_count() : 0;
}

souxmar_status_t souxmar_timeseries_time(const souxmar_timeseries_t* series,
                                         size_t frame_index,
                                         double* out_time) {
  if (series == nullptr || out_time == nullptr) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "timeseries_time: null argument");
  }
  const auto* ts = as_cpp(series);
  if (frame_index >= ts->frame_count()) {
    return souxmar_status_error(SOUXMAR_E_NOT_FOUND, "timeseries_time: frame index out of range");
  }
  *out_time = ts->time(frame_index);
  return souxmar_status_ok();
}

size_t souxmar_timeseries_field_count(const souxmar_timeseries_t* series) {
  return series ? as_cpp(series)->field_count() : 0;
}

const char* souxmar_timeseries_field_name(const souxmar_timeseries_t* series, size_t field_index) {
  if (series == nullptr)
    return nullptr;
  const auto sv = as_cpp(series)->field_name(field_index);
  // TimeSeries::field_name returns a string_view backed by the owned
  // std::string in field_names_; .data() is null-terminated as long
  // as the TimeSeries exists. Out-of-range index returns an empty
  // view; in that case, surface a nullptr to the C caller.
  return sv.empty() ? nullptr : sv.data();
}

const souxmar_field_t* souxmar_timeseries_frame(souxmar_timeseries_t* series,
                                                size_t frame_index,
                                                const char* field_name) {
  if (series == nullptr || field_name == nullptr)
    return nullptr;
  const auto* f = as_cpp(series)->frame(frame_index, std::string_view{field_name});
  return field_as_c(f);
}

souxmar_status_t souxmar_timeseries_cache_window(souxmar_timeseries_t* series, size_t window_size) {
  if (series == nullptr) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "timeseries_cache_window: null handle");
  }
  as_cpp(series)->set_cache_window(window_size);
  return souxmar_status_ok();
}

souxmar_status_t souxmar_timeseries_cache_preload(souxmar_timeseries_t* series,
                                                  size_t start_frame,
                                                  size_t count) {
  if (series == nullptr) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT,
                                "timeseries_cache_preload: null handle");
  }
  as_cpp(series)->cache_preload(start_frame, count);
  return souxmar_status_ok();
}

}  // extern "C"
