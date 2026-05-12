/* SPDX-License-Identifier: Apache-2.0
 *
 * Solver capability vtable.
 *
 * A solver consumes a Mesh + a value-bag of inputs (boundary conditions,
 * materials, ...) and produces a Field. The host orchestrator extracts the
 * mesh handle from the upstream stage's output; remaining inputs come
 * through the souxmar_value_t bag.
 */

#ifndef SOUXMAR_SOLVER_H
#define SOUXMAR_SOLVER_H

#include "souxmar-c/abi.h"
#include "souxmar-c/status.h"
#include "souxmar-c/types.h"

SOUXMAR_C_BEGIN

typedef struct souxmar_solver_options {
  int64_t random_seed;    /* deterministic-mode seed; <0 means non-deterministic */
  double tolerance;       /* solver tolerance; <= 0 means use plugin default     */
  int32_t max_iterations; /* iterative-solver cap; <= 0 means use plugin default */
} souxmar_solver_options_t;

typedef souxmar_status_t (*souxmar_solver_solve_fn)(
    const souxmar_mesh_t* mesh,
    const souxmar_value_t* inputs, /* the stage input map (mesh removed) */
    const souxmar_solver_options_t* options,
    souxmar_field_t** out_field, /* on success: ownership transfers to host */
    void* user_data);

typedef void (*souxmar_solver_destroy_fn)(void* user_data);

typedef struct souxmar_solver_vtable {
  int32_t abi_version; /* MUST equal SOUXMAR_ABI_VERSION_MAJOR */
  souxmar_solver_solve_fn solve_fn;
  souxmar_solver_destroy_fn destroy_fn; /* may be NULL                          */
} souxmar_solver_vtable_t;

SOUXMAR_C_END

#endif /* SOUXMAR_SOLVER_H */
