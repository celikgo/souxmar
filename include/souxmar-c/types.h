/* SPDX-License-Identifier: Apache-2.0
 *
 * Opaque handle types shared across capability vtables.
 *
 * These structs are NEVER defined in plugin code. All access goes through
 * accessor functions in the corresponding _accessors.h header (added in
 * Sprint 2 alongside the plugin host implementation).
 */

#ifndef SOUXMAR_C_TYPES_H
#define SOUXMAR_C_TYPES_H

#include "souxmar-c/abi.h"

SOUXMAR_C_BEGIN

typedef struct souxmar_geometry souxmar_geometry_t;
typedef struct souxmar_topology souxmar_topology_t;
typedef struct souxmar_mesh souxmar_mesh_t;
typedef struct souxmar_field souxmar_field_t;

/* Read-only Value tree handle, defined here so plugins compiling as
 * pure C can use it without pulling in value.h's accessors. Until
 * Sprint 5 push 4 this typedef was implicit (every in-tree plugin is
 * C++, where `struct souxmar_value` and `souxmar_value_t` aliasing is
 * automatic). Pure-C plugin authors hit a "unknown type" error without
 * this declaration. */
typedef struct souxmar_value souxmar_value_t;

/* Opaque writable byte buffer used for bulk mesh transfer across the
 * ABI (see buffer.h + mesh.h's souxmar_mesh_from_buffers). Sprint 5
 * push 4 v1 implementation is heap-backed; future mmap-backed
 * implementations will share this handle type. ADR-0006 documents the
 * rollout plan. */
typedef struct souxmar_buffer souxmar_buffer_t;

SOUXMAR_C_END

#endif /* SOUXMAR_C_TYPES_H */
