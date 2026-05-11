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
typedef struct souxmar_mesh     souxmar_mesh_t;
typedef struct souxmar_field    souxmar_field_t;

SOUXMAR_C_END

#endif /* SOUXMAR_C_TYPES_H */
