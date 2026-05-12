/* SPDX-License-Identifier: Apache-2.0
 *
 * Mesher capability vtable.
 *
 * A mesher takes a Geometry and produces a Mesh. Tag inheritance is part
 * of the contract — every cell-face descended from a tagged geometric face
 * MUST carry that face's tag. A mesher that loses tags is non-conforming
 * (see docs/PLUGIN_SDK.md and the `validating-solver` skill).
 */

#ifndef SOUXMAR_MESHER_H
#define SOUXMAR_MESHER_H

#include "souxmar-c/abi.h"
#include "souxmar-c/status.h"
#include "souxmar-c/types.h"

SOUXMAR_C_BEGIN

/* Mesher input options. Sprint-1 surface; expanded by RFC as needs land. */
typedef struct souxmar_mesher_options {
  double target_size;    /* characteristic edge length; <= 0 means "auto"      */
  int32_t optimize;      /* 0 / 1 — apply mesh-quality optimisation passes     */
  int32_t element_order; /* 1 (linear) or 2 (quadratic)                        */
  int64_t random_seed;   /* deterministic-mode seed; <0 means non-deterministic */
} souxmar_mesher_options_t;

/* Mesher entry point.
 *
 * On success: writes a freshly-allocated mesh to *out_mesh and returns
 * SOUXMAR_OK. The host takes ownership and will call souxmar_mesh_free().
 *
 * On failure: leaves *out_mesh untouched (do not allocate then return error)
 * and returns a populated souxmar_status_t. */
typedef souxmar_status_t (*souxmar_mesher_mesh_fn)(const souxmar_geometry_t* geometry,
                                                   const souxmar_mesher_options_t* options,
                                                   souxmar_mesh_t** out_mesh,
                                                   void* user_data);

/* Optional teardown — called once when the plugin is unloaded. May be NULL. */
typedef void (*souxmar_mesher_destroy_fn)(void* user_data);

typedef struct souxmar_mesher_vtable {
  int32_t abi_version; /* MUST equal SOUXMAR_ABI_VERSION_MAJOR */
  souxmar_mesher_mesh_fn mesh_fn;
  souxmar_mesher_destroy_fn destroy_fn; /* may be NULL                          */
} souxmar_mesher_vtable_t;

SOUXMAR_C_END

#endif /* SOUXMAR_MESHER_H */
