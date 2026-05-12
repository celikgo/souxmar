// SPDX-License-Identifier: Apache-2.0
//
// Stub backing for sketch.h (v1.7 ABI ratchet, ADR-0040).
//
// Sprint 29 PR 1 lands this stub; PR 2 supplies the real impl via
// examples/plugins/sketch-solver-gcs (planegcs wrapper).
//
// The sketch data model itself is host-owned, but this stub holds no
// state — primitive add / constraint add return failure IDs. Real
// data lives in the host-side souxmar::core::Sketch class that lands
// alongside the bridge surface.

#include "souxmar-c/sketch.h"

#include <cstddef>
#include <cstdint>

struct souxmar_sketch_t {};

extern "C" {

souxmar_sketch_t* souxmar_sketch_new(void) {
  return nullptr;
}

void souxmar_sketch_free(souxmar_sketch_t* sketch) {
  delete sketch;
}

souxmar_status_t souxmar_sketch_anchor_world(souxmar_sketch_t* /*sketch*/,
                                              uint8_t /*plane*/) {
  return souxmar_status_error(SOUXMAR_E_NOT_IMPLEMENTED, "sketch stub");
}

souxmar_status_t souxmar_sketch_anchor_face(souxmar_sketch_t* /*sketch*/,
                                             const souxmar_brep_session_t* /*session*/,
                                             uint64_t /*face_id*/) {
  return souxmar_status_error(SOUXMAR_E_NOT_IMPLEMENTED, "sketch stub");
}

uint32_t souxmar_sketch_add_point(souxmar_sketch_t* /*sketch*/,
                                   double /*u*/, double /*v*/) {
  return 0;  // 0 == failure
}

uint32_t souxmar_sketch_add_line(souxmar_sketch_t* /*sketch*/,
                                  uint32_t /*start_point_id*/,
                                  uint32_t /*end_point_id*/) {
  return 0;
}

uint32_t souxmar_sketch_add_arc(souxmar_sketch_t* /*sketch*/,
                                 uint32_t /*start_point_id*/,
                                 uint32_t /*end_point_id*/,
                                 uint32_t /*centre_point_id*/) {
  return 0;
}

uint32_t souxmar_sketch_add_circle(souxmar_sketch_t* /*sketch*/,
                                    uint32_t /*centre_point_id*/,
                                    double /*radius*/) {
  return 0;
}

uint32_t souxmar_sketch_add_constraint(souxmar_sketch_t* /*sketch*/,
                                        uint8_t /*kind*/,
                                        const uint32_t* /*targets*/,
                                        size_t /*target_count*/,
                                        double /*value*/) {
  return 0;
}

size_t souxmar_sketch_num_primitives(const souxmar_sketch_t* /*sketch*/) {
  return 0;
}

size_t souxmar_sketch_num_constraints(const souxmar_sketch_t* /*sketch*/) {
  return 0;
}

souxmar_status_t souxmar_sketch_point_position(const souxmar_sketch_t* /*sketch*/,
                                                uint32_t /*point_id*/,
                                                double* /*out_u*/,
                                                double* /*out_v*/) {
  return souxmar_status_error(SOUXMAR_E_NOT_IMPLEMENTED, "sketch stub");
}

souxmar_status_t souxmar_sketch_solve(souxmar_sketch_t* /*sketch*/) {
  return souxmar_status_error(SOUXMAR_E_NOT_IMPLEMENTED, "sketch stub (no sketch.solver.* plugin loaded)");
}

}  // extern "C"
