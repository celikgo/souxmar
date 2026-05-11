/* SPDX-License-Identifier: Apache-2.0
 *
 * Postproc capability vtable.
 *
 * A postproc consumes a Mesh + an input Field + a value-bag of options
 * and produces a derived Field. Canonical examples:
 *   - postproc.scalar_magnitude  : vector field -> scalar magnitude
 *   - postproc.von_mises         : stress tensor field -> scalar field
 *   - postproc.principal         : tensor field -> principal-component fields
 *
 * Why a separate vtable from solver.* : the solver vtable takes only
 * (mesh, value bag, options) — there's no input-field parameter. Adding
 * one would be an ABI break we deliberately avoid right before freeze
 * candidacy. Postproc is the new, input-field-aware capability surface.
 * See ADR-0005 for the design rationale + alternatives considered.
 *
 * Memory model: same as solver.* and writer.* — every pointer passed to
 * compute_fn is borrowed; the plugin transfers ownership of `*out_field`
 * to the host on success.
 */

#ifndef SOUXMAR_POSTPROC_H
#define SOUXMAR_POSTPROC_H

#include "souxmar-c/abi.h"
#include "souxmar-c/status.h"
#include "souxmar-c/types.h"

SOUXMAR_C_BEGIN

typedef struct souxmar_postproc_options {
  int64_t random_seed;     /* deterministic-mode seed; <0 means non-deterministic */
  double  tolerance;       /* postproc tolerance; <= 0 means use plugin default   */
  int32_t max_iterations;  /* iterative cap; <= 0 means use plugin default        */
} souxmar_postproc_options_t;

typedef souxmar_status_t (*souxmar_postproc_compute_fn)(
    const souxmar_mesh_t*               mesh,
    const souxmar_field_t*              input_field,  /* never NULL: dispatcher enforces */
    const souxmar_value_t*              inputs,       /* stage input map (mesh/field removed) */
    const souxmar_postproc_options_t*   options,
    souxmar_field_t**                   out_field,    /* on success: ownership transfers to host */
    void*                               user_data);

typedef void (*souxmar_postproc_destroy_fn)(void* user_data);

typedef struct souxmar_postproc_vtable {
  int32_t                       abi_version;  /* MUST equal SOUXMAR_ABI_VERSION_MAJOR */
  souxmar_postproc_compute_fn   compute_fn;
  souxmar_postproc_destroy_fn   destroy_fn;   /* may be NULL                          */
} souxmar_postproc_vtable_t;

SOUXMAR_C_END

#endif /* SOUXMAR_POSTPROC_H */
