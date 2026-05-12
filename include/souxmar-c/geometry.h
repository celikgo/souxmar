/* SPDX-License-Identifier: Apache-2.0
 *
 * souxmar geometry handle accessors.
 *
 * Plugins (typically readers) construct Geometries, plugins (typically
 * meshers) consume them. Implementations in src/core/c_abi_geometry.cpp.
 */

#ifndef SOUXMAR_C_GEOMETRY_H
#define SOUXMAR_C_GEOMETRY_H

#include "souxmar-c/abi.h"
#include "souxmar-c/status.h"
#include "souxmar-c/types.h"

SOUXMAR_C_BEGIN

/* ---- Entity kinds ---- */
/* Numeric values match souxmar::core::EntityKind. STABLE. */
#define SOUXMAR_GK_VERTEX 0
#define SOUXMAR_GK_EDGE 1
#define SOUXMAR_GK_FACE 2
#define SOUXMAR_GK_SOLID 3

/* ---- Lifecycle ---- */

souxmar_geometry_t* souxmar_geometry_new(void);
void souxmar_geometry_free(souxmar_geometry_t* geometry);

/* ---- Build (typically used by reader plugins) ---- */

uint32_t souxmar_geometry_add_vertex(souxmar_geometry_t* g, const double position[3]);
uint32_t souxmar_geometry_add_edge(souxmar_geometry_t* g);
uint32_t souxmar_geometry_add_face(souxmar_geometry_t* g);
uint32_t souxmar_geometry_add_solid(souxmar_geometry_t* g);

/* `kind` is one of SOUXMAR_GK_*. */
souxmar_status_t souxmar_geometry_set_tag(souxmar_geometry_t* g,
                                          uint8_t kind,
                                          uint32_t index,
                                          int32_t tag);
souxmar_status_t souxmar_geometry_set_name(souxmar_geometry_t* g,
                                           uint8_t kind,
                                           uint32_t index,
                                           const char* name);

/* ---- Read ---- */

size_t souxmar_geometry_count(const souxmar_geometry_t* g, uint8_t kind);
size_t souxmar_geometry_num_vertices(const souxmar_geometry_t* g);
size_t souxmar_geometry_num_edges(const souxmar_geometry_t* g);
size_t souxmar_geometry_num_faces(const souxmar_geometry_t* g);
size_t souxmar_geometry_num_solids(const souxmar_geometry_t* g);

souxmar_status_t souxmar_geometry_vertex_position(const souxmar_geometry_t* g,
                                                  uint32_t vertex_index,
                                                  double out_position[3]);

/* Returns the entity's tag (-1 if untagged or out of range). */
int32_t souxmar_geometry_tag(const souxmar_geometry_t* g, uint8_t kind, uint32_t index);

/* Returns the entity's name (NULL if unnamed or out of range). The pointer
 * is valid until the entity's name is reassigned. */
const char* souxmar_geometry_name(const souxmar_geometry_t* g, uint8_t kind, uint32_t index);

/* { xmin, ymin, zmin, xmax, ymax, zmax }. All zero for an empty geometry. */
souxmar_status_t souxmar_geometry_bounding_box(const souxmar_geometry_t* g, double out_box[6]);

SOUXMAR_C_END

#endif /* SOUXMAR_C_GEOMETRY_H */
