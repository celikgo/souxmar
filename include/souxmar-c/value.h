/* SPDX-License-Identifier: Apache-2.0
 *
 * souxmar opaque value tree (read-only across the C ABI).
 *
 * Used by plugins to inspect their stage's typed input bag. Constructed by
 * the host during pipeline dispatch from the parsed YAML; plugins never
 * allocate or free souxmar_value_t — every pointer they receive is borrowed.
 *
 * Implementation in src/pipeline/c_abi_value.cpp (the implementation lives
 * in the pipeline library because Value is the pipeline-layer type).
 */

#ifndef SOUXMAR_C_VALUE_H
#define SOUXMAR_C_VALUE_H

#include "souxmar-c/abi.h"
#include "souxmar-c/types.h"

SOUXMAR_C_BEGIN

/* Value kinds — match souxmar::pipeline::Value::Kind. STABLE. */
#define SOUXMAR_VK_NULL 0
#define SOUXMAR_VK_BOOL 1
#define SOUXMAR_VK_NUMBER 2
#define SOUXMAR_VK_STRING 3
#define SOUXMAR_VK_STAGE 4
#define SOUXMAR_VK_LIST 5
#define SOUXMAR_VK_MAP 6

uint8_t souxmar_value_kind(const souxmar_value_t* value);

/* Scalar accessors. Behaviour is undefined if `value` does not have the
 * expected kind; check with souxmar_value_kind first. */
int souxmar_value_as_bool(const souxmar_value_t* value);
double souxmar_value_as_number(const souxmar_value_t* value);
const char* souxmar_value_as_string(const souxmar_value_t* value);
const char* souxmar_value_as_stage(const souxmar_value_t* value);

/* List access. Returned pointers are borrowed; valid as long as the parent
 * value is. */
size_t souxmar_value_list_size(const souxmar_value_t* value);
const souxmar_value_t* souxmar_value_list_at(const souxmar_value_t* value, size_t index);

/* Map access. Returns NULL if the key is missing OR if `value` is not a Map. */
const souxmar_value_t* souxmar_value_map_get(const souxmar_value_t* value, const char* key);
size_t souxmar_value_map_size(const souxmar_value_t* value);

SOUXMAR_C_END

#endif /* SOUXMAR_C_VALUE_H */
