/* SPDX-License-Identifier: Apache-2.0
 *
 * souxmar mesh handle accessors.
 *
 * Plugin authors call these to construct + read meshes through the opaque
 * souxmar_mesh_t handle. The host-side implementations live in
 * src/core/c_abi_mesh.cpp and operate on souxmar::core::Mesh.
 *
 * Memory model: the host owns meshes it allocates; plugins free with
 * souxmar_mesh_free. Plugins that produce a mesh as output transfer
 * ownership to the host via the out_mesh parameter (see mesher.h).
 */

#ifndef SOUXMAR_C_MESH_H
#define SOUXMAR_C_MESH_H

#include "souxmar-c/abi.h"
#include "souxmar-c/status.h"
#include "souxmar-c/types.h"

SOUXMAR_C_BEGIN

/* ---- Element types ---- */
/* Numeric values match souxmar::core::ElementType. STABLE — appears in
 * the on-disk mesh format and in plugin code; renumbering forbidden. */
#define SOUXMAR_ET_UNKNOWN     0
#define SOUXMAR_ET_VERTEX      1
#define SOUXMAR_ET_EDGE2       2
#define SOUXMAR_ET_EDGE3       3
#define SOUXMAR_ET_TRI3        4
#define SOUXMAR_ET_TRI6        5
#define SOUXMAR_ET_QUAD4       6
#define SOUXMAR_ET_QUAD8       7
#define SOUXMAR_ET_QUAD9       8
#define SOUXMAR_ET_TET4        9
#define SOUXMAR_ET_TET10      10
#define SOUXMAR_ET_HEX8       11
#define SOUXMAR_ET_HEX20      12
#define SOUXMAR_ET_HEX27      13
#define SOUXMAR_ET_PRISM6     14
#define SOUXMAR_ET_PRISM15    15
#define SOUXMAR_ET_PYRAMID5   16
#define SOUXMAR_ET_PYRAMID13  17

/* ---- Lifecycle ---- */

/* Allocate a fresh empty mesh. NULL on out-of-memory. */
souxmar_mesh_t* souxmar_mesh_new(void);

/* Free a mesh allocated by souxmar_mesh_new. NULL is a no-op. */
void souxmar_mesh_free(souxmar_mesh_t* mesh);

/* ---- Capacity hints ---- */

void souxmar_mesh_reserve_nodes(souxmar_mesh_t* mesh, size_t n);
void souxmar_mesh_reserve_cells(souxmar_mesh_t* mesh, size_t n);

/* ---- Build ---- */

/* Append a node at `position[3]`. Returns the new node's index. */
uint64_t souxmar_mesh_add_node(souxmar_mesh_t* mesh, const double position[3]);

/* Append a cell of type `element_type` (SOUXMAR_ET_*), referencing
 * `num_node_indices` nodes from `node_indices`. `tag` is the inherited
 * geometry-entity id (-1 for untagged). Out-parameter `out_cell_index`
 * receives the new cell's index (may be NULL). */
souxmar_status_t souxmar_mesh_add_cell(souxmar_mesh_t*  mesh,
                                       uint16_t         element_type,
                                       const uint64_t*  node_indices,
                                       size_t           num_node_indices,
                                       int32_t          tag,
                                       uint64_t*        out_cell_index);

/* ---- Read ---- */

size_t   souxmar_mesh_num_nodes(const souxmar_mesh_t* mesh);
size_t   souxmar_mesh_num_cells(const souxmar_mesh_t* mesh);

/* Write node coordinates into out_position[3]. Returns SOUXMAR_E_NOT_FOUND
 * if `index` is out of range. */
souxmar_status_t souxmar_mesh_node(const souxmar_mesh_t* mesh,
                                   uint64_t              index,
                                   double                out_position[3]);

/* Returns the cell's element type, or SOUXMAR_ET_UNKNOWN if out of range. */
uint16_t souxmar_mesh_cell_type(const souxmar_mesh_t* mesh, uint64_t cell_index);

/* Returns the cell's node-count (as derived from its element type), or 0
 * if the cell index is out of range. */
size_t   souxmar_mesh_cell_node_count(const souxmar_mesh_t* mesh, uint64_t cell_index);

/* Copy the cell's node indices into out_node_indices (capacity in nodes,
 * not bytes). Returns SOUXMAR_E_INVALID_ARGUMENT if capacity is too small,
 * SOUXMAR_E_NOT_FOUND if the cell index is out of range, SOUXMAR_OK otherwise. */
souxmar_status_t souxmar_mesh_cell_nodes(const souxmar_mesh_t* mesh,
                                         uint64_t              cell_index,
                                         uint64_t*             out_node_indices,
                                         size_t                out_capacity);

/* Returns the cell's tag, or -1 if out of range. */
int32_t  souxmar_mesh_cell_tag(const souxmar_mesh_t* mesh, uint64_t cell_index);

/* Zero-copy access to the flat 3*N node coordinate buffer. Pointer is
 * valid until the next mutation of the mesh. *out_size receives 3*N. */
const double* souxmar_mesh_nodes_flat(const souxmar_mesh_t* mesh, size_t* out_size);

SOUXMAR_C_END

#endif /* SOUXMAR_C_MESH_H */
