// SPDX-License-Identifier: Apache-2.0
//
// scalar-magnitude — the first reference postproc plugin (Sprint 5
// push 3). Registers `postproc.scalar_magnitude` and converts any-kind
// input field into a scalar magnitude field with the same location and
// time-step count.
//
// Per-component logic:
//   * scalar (1 component)   → abs(value)
//   * vector (3 components)  → sqrt(vx² + vy² + vz²)
//   * tensor (9 components)  → Frobenius norm: sqrt(sum_ij T_ij²)
//
// The output Field is allocated by the plugin; ownership transfers to
// the host (which frees it via souxmar_field_free). This plugin
// declares itself reentrant — pure functional transform, no shared
// state — so the parallel runner can fan out concurrent calls.

#include <cmath>
#include <cstdint>

#include "souxmar-c/abi.h"
#include "souxmar-c/field.h"
#include "souxmar-c/mesh.h"
#include "souxmar-c/plugin.h"
#include "souxmar-c/postproc.h"
#include "souxmar-c/registry.h"
#include "souxmar-c/status.h"
#include "souxmar-c/value.h"

namespace {

souxmar_status_t scalar_magnitude_compute(
    const souxmar_mesh_t*               /*mesh*/,
    const souxmar_field_t*              input_field,
    const souxmar_value_t*              /*inputs*/,
    const souxmar_postproc_options_t*   /*options*/,
    souxmar_field_t**                   out_field,
    void*                               /*user_data*/) {
  if (!input_field) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "input_field is NULL");
  }
  if (!out_field) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "out_field is NULL");
  }

  const std::size_t  count          = souxmar_field_count(input_field);
  const std::size_t  num_time_steps = souxmar_field_num_time_steps(input_field);
  const std::uint8_t location       = souxmar_field_location(input_field);
  const std::uint8_t components     = souxmar_field_components(input_field);
  if (count == 0 || num_time_steps == 0 || components == 0) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT,
                                "input_field has empty count / num_time_steps / components");
  }

  const double* in_data       = souxmar_field_data_const(input_field);
  const std::size_t in_size   = souxmar_field_data_size(input_field);
  if (!in_data ||
      in_size != count * static_cast<std::size_t>(components) * num_time_steps) {
    return souxmar_status_error(SOUXMAR_E_INTERNAL,
                                "input_field flat buffer size mismatch");
  }

  souxmar_field_t* out = souxmar_field_new(
      "magnitude", location, SOUXMAR_FK_SCALAR, count, num_time_steps);
  if (!out) {
    return souxmar_status_error(SOUXMAR_E_OUT_OF_MEMORY, "souxmar_field_new failed");
  }
  double* out_data = souxmar_field_data(out);
  if (!out_data) {
    souxmar_field_free(out);
    return souxmar_status_error(SOUXMAR_E_INTERNAL,
                                "souxmar_field_data returned NULL");
  }

  // The input layout is [time_step, location, component] in row-major
  // order — matches the Sprint 1 Field contract. We walk the flat
  // buffer in stride `components` and write one scalar per (step, loc).
  const std::size_t total_locations = count * num_time_steps;
  for (std::size_t i = 0; i < total_locations; ++i) {
    double sum_sq = 0.0;
    for (std::size_t c = 0; c < components; ++c) {
      const double v = in_data[i * components + c];
      sum_sq += v * v;
    }
    out_data[i] = std::sqrt(sum_sq);
  }

  *out_field = out;
  return souxmar_status_ok();
}

constexpr souxmar_postproc_vtable_t kVtable = {
    SOUXMAR_ABI_VERSION_MAJOR,
    &scalar_magnitude_compute,
    nullptr,
};

}  // namespace

extern "C" SOUXMAR_PLUGIN_EXPORT
int souxmar_plugin_register_v1(souxmar_registry_t*        registry,
                               const souxmar_host_info_t* host) {
  if (!host || host->abi_version_major < SOUXMAR_ABI_VERSION_MAJOR) {
    return -1;
  }
  const souxmar_status_t s = souxmar_registry_add_postproc(
      registry, "postproc.scalar_magnitude", &kVtable, /*user_data=*/nullptr);
  return s.code == SOUXMAR_OK ? 0 : 1;
}
