/* SPDX-License-Identifier: Apache-2.0
 *
 * include/souxmar-c/field_stream.h — additive, v1.5 of the C ABI.
 *
 * Renderer-friendly view over an existing souxmar_field_t. The view
 * is *derived* and read-only; the host computes per-component min/max
 * on first open and caches alongside the field.
 *
 * Range is reported in f64 (source precision preserved for the
 * legend); values are reported in f32 SoA (renderer-friendly, one
 * down-cast on the C side).
 *
 * Lifetime: the stream holds an internal reference to the field.
 * Freeing the field while the stream is open is undefined behavior.
 *
 * Threading: not thread-safe; marshal via the existing host
 * dispatcher.
 *
 * See docs/rfcs/0002-field-stream-protocol.md and ADR-0038.
 */

#ifndef SOUXMAR_C_FIELD_STREAM_H
#define SOUXMAR_C_FIELD_STREAM_H

#include "souxmar-c/abi.h"
#include "souxmar-c/field.h"
#include "souxmar-c/status.h"

SOUXMAR_C_BEGIN

typedef struct souxmar_field_stream_t souxmar_field_stream_t;

souxmar_field_stream_t* souxmar_field_stream_open(const souxmar_field_t* field);
void souxmar_field_stream_close(souxmar_field_stream_t* stream);

size_t souxmar_field_stream_count(const souxmar_field_stream_t* s);
uint8_t souxmar_field_stream_components(const souxmar_field_stream_t* s);

/* out_min / out_max must each point to a buffer of `components` doubles. */
souxmar_status_t souxmar_field_stream_range(const souxmar_field_stream_t* s,
                                            double* out_min,
                                            double* out_max);

/* Borrowed C string. Owned by the stream; valid until close.
 * Returns "" if the field has no unit attached. */
const char* souxmar_field_stream_units(const souxmar_field_stream_t* s);

/* SoA read. out_capacity must be >= count * components. */
souxmar_status_t souxmar_field_stream_values(const souxmar_field_stream_t* s,
                                             float* out,
                                             size_t out_capacity);

SOUXMAR_C_END
#endif /* SOUXMAR_C_FIELD_STREAM_H */
