// SPDX-License-Identifier: Apache-2.0
//
// heat-solver — Sprint 5 demonstration solver registering
// `solver.heat.linear`. The point of this plugin is to drive
// Field-time-series end to end through the C ABI; it is NOT a
// production heat solver.
//
// What it computes:
//   T(node_i, step_j) = T_steady * (1 - exp(-t_j / tau)) * f_spatial(node_i)
// where
//   t_j         = (j + 1) * dt
//   tau         = thermal relaxation constant (input, default 1.0)
//   T_steady    = steady-state temperature (input, default 1.0)
//   f_spatial   = 0.5 * (1 + cos(pi * x_norm)),  with x_norm ∈ [-1, 1]
//                 — peaks at the mesh's bbox center in x, falls to zero
//                   at the bbox edges. Cheap, smooth, demonstrative.
//
// Output: a nodal scalar Field of `num_time_steps` steps, each carrying
// one temperature value per mesh node.
//
// The real heat solver (FEM, sparse linear solve, real BC handling)
// lands with the FEniCSx adapter in Sprint 8.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#include "souxmar-c/abi.h"
#include "souxmar-c/field.h"
#include "souxmar-c/mesh.h"
#include "souxmar-c/plugin.h"
#include "souxmar-c/registry.h"
#include "souxmar-c/solver.h"
#include "souxmar-c/status.h"
#include "souxmar-c/value.h"

namespace {

constexpr double kPi = 3.14159265358979323846;

double read_number(const souxmar_value_t* inputs, const char* key, double default_value) {
  if (!inputs || souxmar_value_kind(inputs) != SOUXMAR_VK_MAP) return default_value;
  const souxmar_value_t* v = souxmar_value_map_get(inputs, key);
  if (!v || souxmar_value_kind(v) != SOUXMAR_VK_NUMBER) return default_value;
  return souxmar_value_as_number(v);
}

int read_int(const souxmar_value_t* inputs, const char* key, int default_value) {
  return static_cast<int>(read_number(inputs, key, static_cast<double>(default_value)));
}

souxmar_status_t heat_solve(const souxmar_mesh_t*           mesh,
                            const souxmar_value_t*          inputs,
                            const souxmar_solver_options_t* /*options*/,
                            souxmar_field_t**               out_field,
                            void*                           /*user_data*/) {
  if (!mesh) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "mesh is NULL");
  }
  if (!out_field) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "out_field is NULL");
  }

  const std::size_t num_nodes = souxmar_mesh_num_nodes(mesh);
  if (num_nodes == 0) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "mesh has no nodes");
  }

  // Pull options from the input bag with stable defaults so a caller
  // can run this solver with zero configuration.
  const int    num_time_steps = std::max(1, read_int(inputs, "num_time_steps", 4));
  const double dt             = read_number(inputs, "dt",          0.25);
  const double tau            = read_number(inputs, "tau",         1.0);
  const double t_steady       = read_number(inputs, "t_steady",    1.0);

  // Compute the mesh's x-bounding-box for f_spatial. Walking the flat
  // node array via the zero-copy accessor avoids per-node ABI calls.
  std::size_t flat_size = 0;
  const double* coords = souxmar_mesh_nodes_flat(mesh, &flat_size);
  if (!coords || flat_size != num_nodes * 3) {
    return souxmar_status_error(SOUXMAR_E_INTERNAL,
                                "souxmar_mesh_nodes_flat returned an inconsistent buffer");
  }
  double x_min =  std::numeric_limits<double>::infinity();
  double x_max = -std::numeric_limits<double>::infinity();
  for (std::size_t i = 0; i < num_nodes; ++i) {
    const double x = coords[i * 3 + 0];
    if (x < x_min) x_min = x;
    if (x > x_max) x_max = x;
  }
  const double x_range = (x_max > x_min) ? (x_max - x_min) : 1.0;

  souxmar_field_t* field =
      souxmar_field_new("temperature", SOUXMAR_FL_NODAL, SOUXMAR_FK_SCALAR,
                        num_nodes,
                        static_cast<std::size_t>(num_time_steps));
  if (!field) {
    return souxmar_status_error(SOUXMAR_E_OUT_OF_MEMORY, "souxmar_field_new failed");
  }
  double*       data      = souxmar_field_data(field);
  const std::size_t data_size = souxmar_field_data_size(field);
  if (!data || data_size != num_nodes * static_cast<std::size_t>(num_time_steps)) {
    souxmar_field_free(field);
    return souxmar_status_error(SOUXMAR_E_INTERNAL,
                                "souxmar_field_data buffer size mismatch");
  }

  // Layout: data[step * num_nodes + node_i] is t-th time-step value at node_i.
  for (int step = 0; step < num_time_steps; ++step) {
    const double t          = (step + 1) * dt;
    const double time_factor = 1.0 - std::exp(-t / std::max(tau, 1e-12));
    for (std::size_t i = 0; i < num_nodes; ++i) {
      const double x      = coords[i * 3 + 0];
      const double x_norm = 2.0 * ((x - x_min) / x_range) - 1.0;  // → [-1, 1]
      const double f_sp   = 0.5 * (1.0 + std::cos(kPi * x_norm));
      data[static_cast<std::size_t>(step) * num_nodes + i] =
          t_steady * time_factor * f_sp;
    }
  }

  *out_field = field;
  return souxmar_status_ok();
}

constexpr souxmar_solver_vtable_t kVtable = {
    SOUXMAR_ABI_VERSION_MAJOR,
    &heat_solve,
    nullptr,
};

}  // namespace

extern "C" SOUXMAR_PLUGIN_EXPORT
int souxmar_plugin_register_v1(souxmar_registry_t*        registry,
                               const souxmar_host_info_t* host) {
  if (!host || host->abi_version_major < SOUXMAR_ABI_VERSION_MAJOR) {
    return -1;
  }
  const souxmar_status_t s = souxmar_registry_add_solver(
      registry, "solver.heat.linear", &kVtable, /*user_data=*/nullptr);
  return s.code == SOUXMAR_OK ? 0 : 1;
}
