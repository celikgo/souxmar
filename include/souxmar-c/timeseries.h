/* SPDX-License-Identifier: Apache-2.0
 *
 * include/souxmar-c/timeseries.h — additive, v1.9 of the C ABI.
 *
 * A time-series view over a sequence of souxmar_field_t handles. The
 * sequence is keyed by an opaque handle that the reader plugin
 * produces (typically by parsing a PVD); the host owns frame caching.
 *
 * Memory model: a cache window holds at most N frames in RAM; frames
 * outside the window are evicted (LRU) and re-read on demand. Window
 * size is host-controlled — the renderer asks for a window large
 * enough to keep playback at the requested framerate, but never asks
 * for the whole series unless the user explicitly pins it.
 *
 * Threading: a series is single-writer (the host worker handles
 * eviction) and single-reader (the renderer thread). Multiple
 * readers on one series are undefined.
 *
 * See docs/rfcs/0006-time-series.md and ADR-0042.
 */

#ifndef SOUXMAR_C_TIMESERIES_H
#define SOUXMAR_C_TIMESERIES_H

#include "souxmar-c/abi.h"
#include "souxmar-c/field.h"
#include "souxmar-c/status.h"

SOUXMAR_C_BEGIN

typedef struct souxmar_timeseries_t souxmar_timeseries_t;

/* ---- Lifecycle ----
 *
 * Open a PVD or PVD-equivalent file. The reader is selected via the
 * existing reader.* plugin dispatch on the file extension. */
souxmar_timeseries_t* souxmar_timeseries_open(const char* path);

void souxmar_timeseries_close(souxmar_timeseries_t* series);

/* ---- Series metadata ---- */

size_t souxmar_timeseries_frame_count(const souxmar_timeseries_t* series);

souxmar_status_t souxmar_timeseries_time(const souxmar_timeseries_t* series,
                                         size_t frame_index,
                                         double* out_time);

size_t souxmar_timeseries_field_count(const souxmar_timeseries_t* series);

const char* souxmar_timeseries_field_name(const souxmar_timeseries_t* series, size_t field_index);

/* ---- Frame access ----
 *
 * Returns a field handle valid until the next call that mutates the
 * cache (any souxmar_timeseries_frame or souxmar_timeseries_cache_*
 * call). The renderer thread must consume the field before it next
 * calls the API. The host promises the returned field is in the
 * cache window. */
const souxmar_field_t* souxmar_timeseries_frame(souxmar_timeseries_t* series,
                                                size_t frame_index,
                                                const char* field_name);

/* ---- Cache control ----
 *
 * window_size: how many frames the host may keep resident. Defaults
 * to 16. Setting 0 disables caching (every frame request hits disk).
 * Setting SIZE_MAX pins the whole series (caller's risk). */
souxmar_status_t souxmar_timeseries_cache_window(souxmar_timeseries_t* series, size_t window_size);

/* Pre-warm the cache so playback can start without disk stalls. */
souxmar_status_t souxmar_timeseries_cache_preload(souxmar_timeseries_t* series,
                                                  size_t start_frame,
                                                  size_t count);

SOUXMAR_C_END
#endif /* SOUXMAR_C_TIMESERIES_H */
