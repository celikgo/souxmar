/* SPDX-License-Identifier: Apache-2.0
 *
 * souxmar field handle accessors.
 *
 * Plugins (typically solvers) produce fields and (typically writers /
 * post-processors) consume them. Implementation in
 * src/core/c_abi_field.cpp.
 */

#ifndef SOUXMAR_C_FIELD_H
#define SOUXMAR_C_FIELD_H

#include "souxmar-c/abi.h"
#include "souxmar-c/status.h"
#include "souxmar-c/types.h"

SOUXMAR_C_BEGIN

/* Field locations — match souxmar::core::FieldLocation. STABLE. */
#define SOUXMAR_FL_NODAL        0
#define SOUXMAR_FL_CELL         1
#define SOUXMAR_FL_FACE         2
#define SOUXMAR_FL_GAUSS_POINT  3

/* Field kinds — match souxmar::core::FieldKind. STABLE. */
#define SOUXMAR_FK_SCALAR  0
#define SOUXMAR_FK_VECTOR  1
#define SOUXMAR_FK_TENSOR  2

/* ---- Lifecycle ---- */

/* Allocate a field of `count` locations × `components(kind)` doubles ×
 * `num_time_steps`. Zero-initialised. NULL on allocation failure or
 * num_time_steps == 0. */
souxmar_field_t* souxmar_field_new(const char* name,
                                   uint8_t     location,
                                   uint8_t     kind,
                                   size_t      count,
                                   size_t      num_time_steps);

void souxmar_field_free(souxmar_field_t* field);

/* ---- Metadata ---- */

const char* souxmar_field_name(const souxmar_field_t* field);
uint8_t     souxmar_field_location(const souxmar_field_t* field);
uint8_t     souxmar_field_kind(const souxmar_field_t* field);
uint8_t     souxmar_field_components(const souxmar_field_t* field);
size_t      souxmar_field_count(const souxmar_field_t* field);
size_t      souxmar_field_num_time_steps(const souxmar_field_t* field);

/* ---- Bulk data ---- */

/* Mutable / const access to the entire flat buffer. Size is
 * count * components * num_time_steps. */
double*       souxmar_field_data(souxmar_field_t* field);
const double* souxmar_field_data_const(const souxmar_field_t* field);
size_t        souxmar_field_data_size(const souxmar_field_t* field);

SOUXMAR_C_END

#endif /* SOUXMAR_C_FIELD_H */
