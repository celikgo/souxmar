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
#include "souxmar-c/buffer.h"
#include "souxmar-c/status.h"
#include "souxmar-c/types.h"

SOUXMAR_C_BEGIN

/* ---- Element types ---- */
/* Numeric values match souxmar::core::ElementType. STABLE — appears in
 * the on-disk mesh format and in plugin code; renumbering forbidden. */
#define SOUXMAR_ET_UNKNOWN 0
#define SOUXMAR_ET_VERTEX 1
#define SOUXMAR_ET_EDGE2 2
#define SOUXMAR_ET_EDGE3 3
#define SOUXMAR_ET_TRI3 4
#define SOUXMAR_ET_TRI6 5
#define SOUXMAR_ET_QUAD4 6
#define SOUXMAR_ET_QUAD8 7
#define SOUXMAR_ET_QUAD9 8
#define SOUXMAR_ET_TET4 9
#define SOUXMAR_ET_TET10 10
#define SOUXMAR_ET_HEX8 11
#define SOUXMAR_ET_HEX20 12
#define SOUXMAR_ET_HEX27 13
#define SOUXMAR_ET_PRISM6 14
#define SOUXMAR_ET_PRISM15 15
#define SOUXMAR_ET_PYRAMID5 16
#define SOUXMAR_ET_PYRAMID13 17

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
souxmar_status_t souxmar_mesh_add_cell(souxmar_mesh_t* mesh,
                                       uint16_t element_type,
                                       const uint64_t* node_indices,
                                       size_t num_node_indices,
                                       int32_t tag,
                                       uint64_t* out_cell_index);

/* ---- Read ---- */

size_t souxmar_mesh_num_nodes(const souxmar_mesh_t* mesh);
size_t souxmar_mesh_num_cells(const souxmar_mesh_t* mesh);

/* Write node coordinates into out_position[3]. Returns SOUXMAR_E_NOT_FOUND
 * if `index` is out of range. */
souxmar_status_t souxmar_mesh_node(const souxmar_mesh_t* mesh,
                                   uint64_t index,
                                   double out_position[3]);

/* Returns the cell's element type, or SOUXMAR_ET_UNKNOWN if out of range. */
uint16_t souxmar_mesh_cell_type(const souxmar_mesh_t* mesh, uint64_t cell_index);

/* Returns the cell's node-count (as derived from its element type), or 0
 * if the cell index is out of range. */
size_t souxmar_mesh_cell_node_count(const souxmar_mesh_t* mesh, uint64_t cell_index);

/* Copy the cell's node indices into out_node_indices (capacity in nodes,
 * not bytes). Returns SOUXMAR_E_INVALID_ARGUMENT if capacity is too small,
 * SOUXMAR_E_NOT_FOUND if the cell index is out of range, SOUXMAR_OK otherwise. */
souxmar_status_t souxmar_mesh_cell_nodes(const souxmar_mesh_t* mesh,
                                         uint64_t cell_index,
                                         uint64_t* out_node_indices,
                                         size_t out_capacity);

/* Returns the cell's tag, or -1 if out of range. */
int32_t souxmar_mesh_cell_tag(const souxmar_mesh_t* mesh, uint64_t cell_index);

/* Zero-copy access to the flat 3*N node coordinate buffer. Pointer is
 * valid until the next mutation of the mesh. *out_size receives 3*N. */
const double* souxmar_mesh_nodes_flat(const souxmar_mesh_t* mesh, size_t* out_size);

/* ---- Per-face tags (ABI v1.3 — ADR-0012) -----------------------------
 *
 * Per-face tags discriminate boundary faces by their source geometry
 * entity (e.g. an inlet face vs. a wall face on the same volume mesh).
 * Storage on the host is sparse: only explicitly-tagged faces consume
 * a slot. An unset face reads SOUXMAR_FACE_UNTAGGED.
 *
 * Local face indices run 0 .. souxmar_mesh_cell_face_count(mesh, cell) - 1.
 * The per-element-type face count comes from the element taxonomy:
 *
 *   Vertex / Edge*          : 0  (no face concept)
 *   Tri3 / Tri6             : 3  (bounding edges)
 *   Quad4 / Quad8 / Quad9   : 4  (bounding edges)
 *   Tet4 / Tet10            : 4
 *   Hex8 / Hex20 / Hex27    : 6
 *   Prism6 / Prism15        : 5
 *   Pyramid5 / Pyramid13    : 5
 *
 * Convention matches Gmsh / VTK / OpenFOAM side-set ordering. */

#define SOUXMAR_FACE_UNTAGGED (-1)

/* Returns the bounding-side count for the cell's element type, or 0 if
 * the cell index is out of range / the cell has no face concept. */
size_t souxmar_mesh_cell_face_count(const souxmar_mesh_t* mesh, uint64_t cell_index);

/* Returns the tag stored for (cell_index, local_face_index), or
 * SOUXMAR_FACE_UNTAGGED if the slot is untagged, the cell index is out
 * of range, or the local face index exceeds the cell's face count. */
int32_t souxmar_mesh_face_tag(const souxmar_mesh_t* mesh,
                              uint64_t cell_index,
                              uint8_t local_face_index);

/* Sets the tag stored for (cell_index, local_face_index). Setting
 * SOUXMAR_FACE_UNTAGGED clears the slot. Returns SOUXMAR_E_NOT_FOUND
 * if the cell index is out of range, SOUXMAR_E_INVALID_ARGUMENT if the
 * local face index exceeds the cell's face count. */
souxmar_status_t souxmar_mesh_set_face_tag(souxmar_mesh_t* mesh,
                                           uint64_t cell_index,
                                           uint8_t local_face_index,
                                           int32_t tag);

/* ---- Bulk construction (Sprint 5 push 4 — ADR-0006) ---- */

/* Bulk-construction descriptor. Every buffer is borrowed by the host
 * during the souxmar_mesh_from_buffers call and may be freed by the
 * plugin immediately on return. Layout contract:
 *
 *   node_coords        : 3 * num_nodes contiguous doubles, row-major (xyz, xyz, ...).
 *                        size_bytes = 3 * num_nodes * sizeof(double).
 *
 *   cell_types         : num_cells uint16_t values, each one of SOUXMAR_ET_*.
 *                        size_bytes = num_cells * sizeof(uint16_t).
 *
 *   cell_connectivity  : flat uint64_t buffer of node indices. The slice
 *                        for cell i is connectivity[offsets[i] .. offsets[i+1]).
 *                        size_bytes = total_node_refs * sizeof(uint64_t),
 *                        where total_node_refs = offsets[num_cells].
 *
 *   cell_offsets       : (num_cells + 1) uint64_t values. offsets[0] = 0;
 *                        offsets[i+1] - offsets[i] = number of nodes in cell i;
 *                        offsets[num_cells] = total_node_refs.
 *                        size_bytes = (num_cells + 1) * sizeof(uint64_t).
 *
 *   cell_tags          : OPTIONAL (NULL allowed). num_cells int32_t values
 *                        (-1 for untagged). size_bytes = num_cells * sizeof(int32_t).
 */
typedef struct souxmar_mesh_buffers {
  const souxmar_buffer_t* node_coords;
  size_t num_nodes;
  const souxmar_buffer_t* cell_types;
  const souxmar_buffer_t* cell_connectivity;
  const souxmar_buffer_t* cell_offsets;
  size_t num_cells;
  const souxmar_buffer_t* cell_tags; /* may be NULL */
} souxmar_mesh_buffers_t;

/* Build a Mesh from pre-populated buffers in a single ABI call.
 *
 * On success returns a fresh souxmar_mesh_t* (ownership transferred to
 * the caller, free with souxmar_mesh_free) and writes SOUXMAR_OK to
 * *out_status.
 *
 * On failure returns NULL and writes a structured status to *out_status
 * describing what was wrong:
 *   * SOUXMAR_E_INVALID_ARGUMENT  — buffers struct or required pointer NULL
 *   * SOUXMAR_E_INVALID_ARGUMENT  — buffer size doesn't match declared count
 *   * SOUXMAR_E_INVALID_ARGUMENT  — cell_offsets non-monotonic / wrong terminator
 *   * SOUXMAR_E_INVALID_ARGUMENT  — cell_types[i] is not a known SOUXMAR_ET_*
 *   * SOUXMAR_E_INVALID_ARGUMENT  — per-cell node count doesn't match its element type
 *   * SOUXMAR_E_INVALID_ARGUMENT  — cell connectivity references an out-of-range node
 *   * SOUXMAR_E_OUT_OF_MEMORY     — host couldn't allocate the new Mesh
 *
 * out_status may be NULL if the caller doesn't care about the reason. */
souxmar_mesh_t* souxmar_mesh_from_buffers(const souxmar_mesh_buffers_t* buffers,
                                          souxmar_status_t* out_status);

SOUXMAR_C_END

#endif /* SOUXMAR_C_MESH_H */
