/* SPDX-License-Identifier: Apache-2.0
 *
 * include/souxmar-c/sketch.h — additive, v1.7 of the C ABI.
 *
 * A 2D sketch is a list of primitives in a local UV plane, plus a
 * list of constraints over those primitives, plus an anchor (the
 * placement of the UV plane in 3D). The kernel side owns the data;
 * solver plugins (capability sketch.solver.*) operate on it via the
 * single-entry vtable wired by the plugin host.
 *
 * Solver status uses three new codes added to status.h at v1.7:
 *   SOUXMAR_E_UNDERCONSTRAINED  (sketch has remaining DoF)
 *   SOUXMAR_E_OVERCONSTRAINED   (constraints are redundant)
 *   SOUXMAR_E_NO_CONVERGENCE    (solver gave up; usually time-limit)
 *
 * Threading: sketches are single-threaded. A sketch is one unit of
 * mutation; concurrent edits are caller-coordinated.
 *
 * See docs/rfcs/0004-2d-sketcher.md and ADR-0040.
 */

#ifndef SOUXMAR_C_SKETCH_H
#define SOUXMAR_C_SKETCH_H

#include "souxmar-c/abi.h"
#include "souxmar-c/brep.h"   /* anchor faces are BREP face IDs */
#include "souxmar-c/status.h"

SOUXMAR_C_BEGIN

typedef struct souxmar_sketch_t souxmar_sketch_t;

/* ---- Primitive kinds (stable numeric values) ---- */
#define SOUXMAR_SK_POINT      0
#define SOUXMAR_SK_LINE       1
#define SOUXMAR_SK_ARC        2
#define SOUXMAR_SK_CIRCLE     3
#define SOUXMAR_SK_POLYLINE   4
#define SOUXMAR_SK_SPLINE     5   /* cubic Bezier; control points in UV */

/* ---- Constraint kinds (stable numeric values) ---- */
#define SOUXMAR_SC_COINCIDENT      10
#define SOUXMAR_SC_PARALLEL        11
#define SOUXMAR_SC_PERPENDICULAR   12
#define SOUXMAR_SC_TANGENT         13
#define SOUXMAR_SC_EQUAL           14
#define SOUXMAR_SC_HORIZONTAL      15
#define SOUXMAR_SC_VERTICAL        16
#define SOUXMAR_SC_DISTANCE        17   /* dimensional; value carried */
#define SOUXMAR_SC_ANGLE           18
#define SOUXMAR_SC_RADIUS          19
#define SOUXMAR_SC_DIAMETER        20

/* ---- Lifecycle ---- */

souxmar_sketch_t* souxmar_sketch_new(void);
void              souxmar_sketch_free(souxmar_sketch_t* sketch);

/* ---- Anchor (where the sketch lives in 3D) ---- */

/* World-axis-aligned planes: 0 = XY, 1 = XZ, 2 = YZ. */
souxmar_status_t souxmar_sketch_anchor_world(souxmar_sketch_t* sketch,
                                              uint8_t            plane);

/* Anchor to a planar face on a BREP session. The face must be planar
 * (kernel rejects non-planar with SOUXMAR_E_INVALID_ARGUMENT). */
souxmar_status_t souxmar_sketch_anchor_face(souxmar_sketch_t*               sketch,
                                             const souxmar_brep_session_t*   session,
                                             uint64_t                         face_id);

/* ---- Add primitives (returns a u32 primitive ID, 0 on failure) ---- */

uint32_t souxmar_sketch_add_point(souxmar_sketch_t* sketch, double u, double v);

uint32_t souxmar_sketch_add_line(souxmar_sketch_t* sketch,
                                  uint32_t           start_point_id,
                                  uint32_t           end_point_id);

uint32_t souxmar_sketch_add_arc(souxmar_sketch_t* sketch,
                                 uint32_t           start_point_id,
                                 uint32_t           end_point_id,
                                 uint32_t           centre_point_id);

uint32_t souxmar_sketch_add_circle(souxmar_sketch_t* sketch,
                                    uint32_t           centre_point_id,
                                    double             radius);

/* ---- Add constraints (returns a u32 constraint ID, 0 on failure) ----
 *
 * `kind` is SOUXMAR_SC_*. `targets` is the primitive ID array the
 * constraint operates on (length depends on kind: 1 for
 * horizontal/vertical, 2 for parallel/perpendicular/distance, etc.).
 * `value` is the dimensional value (mm/rad/etc.); 0.0 for non-
 * dimensional constraints. */
uint32_t souxmar_sketch_add_constraint(souxmar_sketch_t* sketch,
                                        uint8_t            kind,
                                        const uint32_t*    targets,
                                        size_t             target_count,
                                        double             value);

/* ---- Queries ---- */

size_t souxmar_sketch_num_primitives(const souxmar_sketch_t* sketch);
size_t souxmar_sketch_num_constraints(const souxmar_sketch_t* sketch);

souxmar_status_t souxmar_sketch_point_position(const souxmar_sketch_t* sketch,
                                                uint32_t                 point_id,
                                                double*                  out_u,
                                                double*                  out_v);

/* ---- Solver invocation (handled by the host; dispatches to plugin) ----
 *
 * Returns: SOUXMAR_OK on solved, SOUXMAR_E_UNDERCONSTRAINED,
 * SOUXMAR_E_OVERCONSTRAINED, or SOUXMAR_E_NO_CONVERGENCE. */
souxmar_status_t souxmar_sketch_solve(souxmar_sketch_t* sketch);

SOUXMAR_C_END
#endif /* SOUXMAR_C_SKETCH_H */
