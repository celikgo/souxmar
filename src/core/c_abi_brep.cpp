// SPDX-License-Identifier: Apache-2.0
//
// Stub backing for brep.h (v1.6 session + v1.8 feature ops, ADRs
// 0039 and 0041).
//
// The real BREP session lives in examples/plugins/cad-occt — this
// in-core stub is what the host calls into when no cad.* plugin is
// loaded; every operation returns NOT_IMPLEMENTED. The conformance
// suite uses these stubs to verify the ABI surface compiles and
// links; real STEP/IGES import + feature ops land with the OCCT
// plugin in Sprint 28 PR 2 (sessions) and Sprint 30 PR 2 (features).

#include "souxmar-c/brep.h"

#include <cstddef>
#include <cstdint>

struct souxmar_brep_session_t {};

extern "C" {

/* ---- v1.6 surface (ADR-0039) ---- */

souxmar_brep_session_t* souxmar_brep_session_open(void) {
  return nullptr;
}

void souxmar_brep_session_close(souxmar_brep_session_t* session) {
  delete session;
}

souxmar_status_t souxmar_brep_import_step(souxmar_brep_session_t* /*session*/,
                                          const char* /*path*/) {
  return souxmar_status_error(SOUXMAR_E_NOT_IMPLEMENTED, "brep stub (no cad plugin loaded)");
}

souxmar_status_t souxmar_brep_import_iges(souxmar_brep_session_t* /*session*/,
                                          const char* /*path*/) {
  return souxmar_status_error(SOUXMAR_E_NOT_IMPLEMENTED, "brep stub (no cad plugin loaded)");
}

souxmar_geometry_t* souxmar_brep_to_geometry(const souxmar_brep_session_t* /*session*/) {
  return nullptr;
}

size_t souxmar_brep_num_bodies(const souxmar_brep_session_t* /*session*/) {
  return 0;
}

souxmar_status_t souxmar_brep_body_id(const souxmar_brep_session_t* /*session*/,
                                      size_t /*index*/,
                                      uint64_t* /*out_body_id*/) {
  return souxmar_status_error(SOUXMAR_E_NOT_IMPLEMENTED, "brep stub");
}

souxmar_status_t souxmar_brep_body_bounds(const souxmar_brep_session_t* /*session*/,
                                          uint64_t /*body_id*/,
                                          double /*out_min*/[3],
                                          double /*out_max*/[3]) {
  return souxmar_status_error(SOUXMAR_E_NOT_IMPLEMENTED, "brep stub");
}

const char* souxmar_brep_body_name(const souxmar_brep_session_t* /*session*/,
                                   uint64_t /*body_id*/) {
  return nullptr;
}

souxmar_status_t souxmar_brep_tessellate(souxmar_brep_session_t* /*session*/,
                                         uint64_t /*body_id*/,
                                         double /*deflection*/,
                                         souxmar_mesh_t** /*out_surface_mesh*/) {
  return souxmar_status_error(SOUXMAR_E_NOT_IMPLEMENTED, "brep stub");
}

/* ---- v1.8 surface (ADR-0041) ---- */

namespace {
souxmar_status_t not_impl(const char* what) {
  return souxmar_status_error(SOUXMAR_E_NOT_IMPLEMENTED, what);
}
}  // namespace

uint64_t souxmar_brep_extrude(souxmar_brep_session_t* /*session*/,
                              const struct souxmar_sketch_t* /*sketch*/,
                              double /*distance*/,
                              uint8_t /*direction*/,
                              uint8_t /*operation*/,
                              uint64_t /*target_body_id*/,
                              souxmar_status_t* out_status) {
  if (out_status)
    *out_status = not_impl("brep.extrude stub");
  return SOUXMAR_INVALID_ID;
}

uint64_t souxmar_brep_revolve(souxmar_brep_session_t* /*session*/,
                              const struct souxmar_sketch_t* /*sketch*/,
                              uint32_t /*axis_line_id*/,
                              double /*angle_rad*/,
                              uint8_t /*operation*/,
                              uint64_t /*target_body_id*/,
                              souxmar_status_t* out_status) {
  if (out_status)
    *out_status = not_impl("brep.revolve stub");
  return SOUXMAR_INVALID_ID;
}

uint64_t souxmar_brep_sweep(souxmar_brep_session_t* /*session*/,
                            const struct souxmar_sketch_t* /*profile*/,
                            const struct souxmar_sketch_t* /*path*/,
                            uint8_t /*operation*/,
                            uint64_t /*target_body_id*/,
                            souxmar_status_t* out_status) {
  if (out_status)
    *out_status = not_impl("brep.sweep stub");
  return SOUXMAR_INVALID_ID;
}

uint64_t souxmar_brep_loft(souxmar_brep_session_t* /*session*/,
                           const struct souxmar_sketch_t** /*profiles*/,
                           size_t /*profile_count*/,
                           uint8_t /*operation*/,
                           uint64_t /*target_body_id*/,
                           souxmar_status_t* out_status) {
  if (out_status)
    *out_status = not_impl("brep.loft stub");
  return SOUXMAR_INVALID_ID;
}

souxmar_status_t souxmar_brep_fillet(souxmar_brep_session_t* /*session*/,
                                     uint64_t /*body_id*/,
                                     const uint64_t* /*edge_ids*/,
                                     size_t /*edge_count*/,
                                     double /*radius*/) {
  return not_impl("brep.fillet stub");
}

souxmar_status_t souxmar_brep_chamfer(souxmar_brep_session_t* /*session*/,
                                      uint64_t /*body_id*/,
                                      const uint64_t* /*edge_ids*/,
                                      size_t /*edge_count*/,
                                      double /*distance*/) {
  return not_impl("brep.chamfer stub");
}

souxmar_status_t souxmar_brep_boolean(souxmar_brep_session_t* /*session*/,
                                      uint64_t /*target_body_id*/,
                                      uint64_t /*tool_body_id*/,
                                      uint8_t /*operation*/) {
  return not_impl("brep.boolean stub");
}

souxmar_status_t souxmar_brep_pattern_linear(souxmar_brep_session_t* /*session*/,
                                             const uint64_t* /*source_body_ids*/,
                                             size_t /*source_count*/,
                                             const double /*direction*/[3],
                                             double /*spacing*/,
                                             uint32_t /*count*/) {
  return not_impl("brep.pattern_linear stub");
}

souxmar_status_t souxmar_brep_pattern_circular(souxmar_brep_session_t* /*session*/,
                                               const uint64_t* /*source_body_ids*/,
                                               size_t /*source_count*/,
                                               const double /*axis_origin*/[3],
                                               const double /*axis_direction*/[3],
                                               double /*angle_rad*/,
                                               uint32_t /*count*/) {
  return not_impl("brep.pattern_circular stub");
}

souxmar_status_t souxmar_brep_delete_body(souxmar_brep_session_t* /*session*/,
                                          uint64_t /*body_id*/) {
  return not_impl("brep.delete_body stub");
}

}  // extern "C"
