/* SPDX-License-Identifier: Apache-2.0
 *
 * Reader capability vtable. (Sprint 6 push 4 — ABI minor v1.1.)
 *
 * A reader consumes a path on disk + a value-bag of options and
 * produces EITHER a Mesh (for tessellated formats: STL / OBJ / PLY)
 * OR a Geometry (for CAD formats: STEP / IGES / BREP). The plugin
 * fills exactly one of `out_mesh` / `out_geometry`; the host's
 * dispatcher routes the result to the matching StageOutput slot.
 *
 * Why a single vtable with two output slots, instead of two vtables
 * (reader.mesh / reader.geometry):
 *   - the plugin's read_fn naturally dispatches on the file format
 *     itself (look at the extension / magic bytes), so it knows which
 *     output is correct;
 *   - a single namespace `reader.*` keeps the manifest, the agent's
 *     `list_plugins`, and the marketplace index simple;
 *   - the dispatcher branches on `out_mesh vs out_geometry != NULL`
 *     once at the boundary, then routes through the same StageOutput
 *     plumbing the rest of the pipeline already uses.
 *
 * Memory model: same as the other capability vtables. The path string
 * is a borrow (NUL-terminated UTF-8, lifetime tied to the call). On
 * success the plugin transfers ownership of the produced handle to
 * the host; on failure both out-pointers are left NULL.
 */

#ifndef SOUXMAR_READER_H
#define SOUXMAR_READER_H

#include "souxmar-c/abi.h"
#include "souxmar-c/status.h"
#include "souxmar-c/types.h"

SOUXMAR_C_BEGIN

typedef struct souxmar_reader_options {
  int32_t merge_coincident_nodes; /* 0 / 1 — collapse duplicate vertices    */
  double coincidence_tolerance;   /* world-space; <= 0 means plugin default */
  int32_t preserve_tags;          /* 0 / 1 — keep solid / surface labels    */
  int64_t random_seed;            /* deterministic dedup; <0 → nondeterministic */
} souxmar_reader_options_t;

typedef souxmar_status_t (*souxmar_reader_read_fn)(
    const char* path,              /* NUL-terminated UTF-8 */
    const souxmar_value_t* inputs, /* stage input map      */
    const souxmar_reader_options_t* options,
    souxmar_mesh_t** out_mesh,         /* set xor out_geometry */
    souxmar_geometry_t** out_geometry, /* set xor out_mesh     */
    void* user_data);

typedef void (*souxmar_reader_destroy_fn)(void* user_data);

typedef struct souxmar_reader_vtable {
  int32_t abi_version; /* MUST equal SOUXMAR_ABI_VERSION_MAJOR */
  souxmar_reader_read_fn read_fn;
  souxmar_reader_destroy_fn destroy_fn; /* may be NULL                          */
} souxmar_reader_vtable_t;

SOUXMAR_C_END

#endif /* SOUXMAR_READER_H */
