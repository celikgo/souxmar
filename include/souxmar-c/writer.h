/* SPDX-License-Identifier: Apache-2.0
 *
 * Writer capability vtable.
 *
 * A writer consumes a Mesh, an optional Field (multi-field writers slated
 * for Sprint 4), and a value-bag of inputs (typically `path`, format flags),
 * and produces side-effect output (typically a file on disk). The host
 * orchestrator extracts the mesh + field handles from upstream stages;
 * remaining inputs come through souxmar_value_t.
 */

#ifndef SOUXMAR_WRITER_H
#define SOUXMAR_WRITER_H

#include "souxmar-c/abi.h"
#include "souxmar-c/status.h"
#include "souxmar-c/types.h"

SOUXMAR_C_BEGIN

typedef souxmar_status_t (*souxmar_writer_write_fn)(
    const souxmar_mesh_t*       mesh,    /* required */
    const souxmar_field_t*      field,   /* may be NULL (mesh-only writers) */
    const souxmar_value_t*      inputs,  /* the stage input map (mesh + field removed) */
    void*                       user_data);

typedef void (*souxmar_writer_destroy_fn)(void* user_data);

typedef struct souxmar_writer_vtable {
  int32_t                    abi_version;  /* MUST equal SOUXMAR_ABI_VERSION_MAJOR */
  souxmar_writer_write_fn    write_fn;
  souxmar_writer_destroy_fn  destroy_fn;   /* may be NULL                          */
} souxmar_writer_vtable_t;

SOUXMAR_C_END

#endif /* SOUXMAR_WRITER_H */
