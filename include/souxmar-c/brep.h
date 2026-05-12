/* SPDX-License-Identifier: Apache-2.0
 *
 * include/souxmar-c/brep.h — additive surface introduced at v1.6,
 * extended at v1.8.
 *
 * A live BREP session owned by a cad.* plugin. The session is the
 * unit of mutation: feature operations (RFC-005 / ADR-0041) take a
 * session + arguments and either extend or modify the session's
 * body graph.
 *
 * Bodies inside a session are identified by stable u64 IDs — stable
 * across edits within the same session, NOT promised across sessions
 * (re-importing a STEP file gives fresh IDs). Face/edge IDs within
 * a body may shift on mutating feature ops; design.yaml's replay
 * (RFC-005) re-binds via a fingerprint heuristic.
 *
 * Threading: a session is single-threaded. Multiple sessions in one
 * process are allowed; calls into one session from multiple threads
 * are not.
 *
 * See docs/rfcs/0003-cad-kernel.md (ADR-0039) for the v1.6 surface
 * and docs/rfcs/0005-feature-tree.md (ADR-0041) for the v1.8 feature
 * ops extension.
 */

#ifndef SOUXMAR_C_BREP_H
#define SOUXMAR_C_BREP_H

#include "souxmar-c/abi.h"
#include "souxmar-c/geometry.h"
#include "souxmar-c/mesh.h"
#include "souxmar-c/status.h"

SOUXMAR_C_BEGIN

typedef struct souxmar_brep_session_t souxmar_brep_session_t;

/* Forward declaration — souxmar_sketch_t is fully defined in
 * souxmar-c/sketch.h (v1.7). Feature ops below take sketches by
 * opaque pointer; users invoking feature ops include sketch.h. */
struct souxmar_sketch_t;

/* Sentinel for "no body" — returned by feature ops on failure.
 * Body IDs of value 0 are never assigned to a real body. */
#define SOUXMAR_INVALID_ID ((uint64_t)0)

/* ====================================================================
 * v1.6 — Session lifecycle, import, queries, tessellation
 * (ADR-0039)
 * ==================================================================== */

souxmar_brep_session_t* souxmar_brep_session_open(void);
void                     souxmar_brep_session_close(souxmar_brep_session_t* session);

souxmar_status_t souxmar_brep_import_step(souxmar_brep_session_t* session,
                                          const char*              path);

souxmar_status_t souxmar_brep_import_iges(souxmar_brep_session_t* session,
                                          const char*              path);

/* Snapshot the session's body graph into a souxmar_geometry_t. The
 * session retains ownership of its BREP; the geometry is a discrete
 * read-only view consumable by mesher plugins. Calling this is the
 * only legal way for a non-cad plugin to see CAD output. */
souxmar_geometry_t* souxmar_brep_to_geometry(const souxmar_brep_session_t* session);

size_t souxmar_brep_num_bodies(const souxmar_brep_session_t* session);

souxmar_status_t souxmar_brep_body_id(const souxmar_brep_session_t* session,
                                       size_t                         index,
                                       uint64_t*                      out_body_id);

souxmar_status_t souxmar_brep_body_bounds(const souxmar_brep_session_t* session,
                                           uint64_t                       body_id,
                                           double                         out_min[3],
                                           double                         out_max[3]);

const char* souxmar_brep_body_name(const souxmar_brep_session_t* session,
                                    uint64_t                       body_id);

/* Tessellation for the renderer; deflection in model units. */
souxmar_status_t souxmar_brep_tessellate(souxmar_brep_session_t* session,
                                          uint64_t                 body_id,
                                          double                   deflection,
                                          souxmar_mesh_t**         out_surface_mesh);

/* ====================================================================
 * v1.8 — Feature operations
 * (ADR-0041)
 *
 * Each feature op mutates the session. Operation codes:
 *   0 = new_body, 1 = add (union with target),
 *   2 = subtract (cut from target), 3 = intersect.
 * Direction codes for extrude:
 *   0 = normal, 1 = reversed, 2 = symmetric.
 *
 * Ops that create a body return its u64 ID (or SOUXMAR_INVALID_ID +
 * out_status on failure). Modifying ops return souxmar_status_t.
 * ==================================================================== */

uint64_t souxmar_brep_extrude(souxmar_brep_session_t* session,
                               const struct souxmar_sketch_t* sketch,
                               double                   distance,
                               uint8_t                  direction,
                               uint8_t                  operation,
                               uint64_t                 target_body_id,
                               souxmar_status_t*        out_status);

uint64_t souxmar_brep_revolve(souxmar_brep_session_t* session,
                               const struct souxmar_sketch_t* sketch,
                               uint32_t                 axis_line_id,
                               double                   angle_rad,
                               uint8_t                  operation,
                               uint64_t                 target_body_id,
                               souxmar_status_t*        out_status);

uint64_t souxmar_brep_sweep(souxmar_brep_session_t* session,
                             const struct souxmar_sketch_t* profile,
                             const struct souxmar_sketch_t* path,
                             uint8_t                  operation,
                             uint64_t                 target_body_id,
                             souxmar_status_t*        out_status);

uint64_t souxmar_brep_loft(souxmar_brep_session_t* session,
                            const struct souxmar_sketch_t** profiles,
                            size_t                    profile_count,
                            uint8_t                   operation,
                            uint64_t                  target_body_id,
                            souxmar_status_t*         out_status);

souxmar_status_t souxmar_brep_fillet(souxmar_brep_session_t* session,
                                      uint64_t                 body_id,
                                      const uint64_t*          edge_ids,
                                      size_t                   edge_count,
                                      double                   radius);

souxmar_status_t souxmar_brep_chamfer(souxmar_brep_session_t* session,
                                       uint64_t                 body_id,
                                       const uint64_t*          edge_ids,
                                       size_t                   edge_count,
                                       double                   distance);

souxmar_status_t souxmar_brep_boolean(souxmar_brep_session_t* session,
                                       uint64_t                 target_body_id,
                                       uint64_t                 tool_body_id,
                                       uint8_t                  operation);

souxmar_status_t souxmar_brep_pattern_linear(souxmar_brep_session_t* session,
                                              const uint64_t*          source_body_ids,
                                              size_t                   source_count,
                                              const double             direction[3],
                                              double                   spacing,
                                              uint32_t                 count);

souxmar_status_t souxmar_brep_pattern_circular(souxmar_brep_session_t* session,
                                                const uint64_t*         source_body_ids,
                                                size_t                  source_count,
                                                const double            axis_origin[3],
                                                const double            axis_direction[3],
                                                double                  angle_rad,
                                                uint32_t                count);

souxmar_status_t souxmar_brep_delete_body(souxmar_brep_session_t* session,
                                           uint64_t                 body_id);

SOUXMAR_C_END
#endif /* SOUXMAR_C_BREP_H */
