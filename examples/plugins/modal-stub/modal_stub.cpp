// SPDX-License-Identifier: Apache-2.0
//
// modal-stub — reference modal-analysis solver.
//
// Registers `solver.modal.linear` — the same capability id a future
// FEniCSx + SLEPc adapter will claim. Purpose: give the agent eval
// suite, the cantilever-beam example, and Sprint 33 documentation a
// runnable modal solver in default CI without dragging SLEPc into
// every build.
//
// What it computes:
//   Closed-form Euler-Bernoulli cantilever beam, fixed at x = x_min,
//   free at x = x_max. The bounding-box span along x is treated as
//   the beam length L; nodal y/z coordinates are ignored for the mode
//   shape (a 1-D beam idealization).
//
//   Natural angular frequencies:
//       omega_n = (beta_n)^2 * sqrt(E * I / (rho * A))
//       f_n     = omega_n / (2*pi)
//   where (beta_n * L) are roots of cos(bL)*cosh(bL) + 1 = 0:
//       1.8751040687119612
//       4.694091132974174
//       7.854757438237612
//      10.995540734875467
//
//   Mode shape (transverse y-displacement at axial coordinate x):
//       psi_n(x) = cosh(beta x) - cos(beta x)
//                  - sigma_n * (sinh(beta x) - sin(beta x))
//       sigma_n  = (cosh(bL) + cos(bL)) / (sinh(bL) + sin(bL))
//   Each mode is normalised so |psi_n(L)| = 1 (unit tip deflection).
//
// What this is NOT: a real eigensolver. It ignores connectivity,
// material assignment per region, fixed-vs-free boundary conditions
// other than "fixed at x_min", and any cross-section that isn't the
// (E, I, rho, A) provided. A real linear-modal solver lands when the
// FEniCSx adapter gains SLEPc-based eigenvalue support (Sprint 33).
//
// Inputs (souxmar_value_t map):
//   num_modes       : int,    defaults to 4   (clamped to [1, 4])
//   youngs_modulus  : number, defaults to 210e9
//   density         : number, defaults to 7850   (kg/m^3)
//   area            : number, defaults to 1e-4   (m^2; 1cm x 1cm)
//   inertia         : number, defaults to 8.333e-10 (m^4; b*h^3/12 for the
//                                                    same 1cm x 1cm section)
//
// Output: nodal vector Field "modal_displacement" with 3 components
// and `num_modes` time steps. Step `n` carries the n-th mode shape,
// with deflection in the y component. The host frequency-table
// accessor lands with the field-metadata follow-up tracked in
// RFC-002 Open Q1; for now, computed frequencies are documented via
// the formula above.

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "souxmar-c/abi.h"
#include "souxmar-c/field.h"
#include "souxmar-c/mesh.h"
#include "souxmar-c/plugin.h"
#include "souxmar-c/registry.h"
#include "souxmar-c/solver.h"
#include "souxmar-c/status.h"
#include "souxmar-c/value.h"

namespace {

constexpr int    kMaxModes        = 4;
constexpr double kBetaL[kMaxModes] = {
    1.8751040687119612,
    4.694091132974174,
    7.854757438237612,
    10.995540734875467,
};

double read_number(const souxmar_value_t* inputs, const char* key, double dv) {
  if (!inputs || souxmar_value_kind(inputs) != SOUXMAR_VK_MAP) return dv;
  const souxmar_value_t* v = souxmar_value_map_get(inputs, key);
  if (!v || souxmar_value_kind(v) != SOUXMAR_VK_NUMBER) return dv;
  return souxmar_value_as_number(v);
}

int read_int(const souxmar_value_t* inputs, const char* key, int dv) {
  return static_cast<int>(read_number(inputs, key, static_cast<double>(dv)));
}

// Mode shape psi_n(x) on the *unit* interval (x in [0, 1]) — beta·L absorbs L.
double mode_shape(double x_norm, double bL, double sigma) {
  const double b = bL * x_norm;
  return std::cosh(b) - std::cos(b) - sigma * (std::sinh(b) - std::sin(b));
}

souxmar_status_t modal_solve(const souxmar_mesh_t*           mesh,
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

  const int n_modes = std::clamp(read_int(inputs, "num_modes", 4), 1, kMaxModes);
  // E, I, rho, A enter only through the natural frequencies; mode
  // shapes are geometry-only. They are read here so that future
  // versions emitting an omega-table see the same input contract.
  (void) std::max(read_number(inputs, "youngs_modulus", 210e9), 1.0);
  (void) std::max(read_number(inputs, "density",        7850.0), 1.0);
  (void) std::max(read_number(inputs, "area",           1e-4),   1e-12);
  (void) std::max(read_number(inputs, "inertia",        8.333e-10), 1e-18);

  std::size_t flat_size = 0;
  const double* coords = souxmar_mesh_nodes_flat(mesh, &flat_size);
  if (!coords || flat_size != num_nodes * 3) {
    return souxmar_status_error(SOUXMAR_E_INTERNAL,
                                "souxmar_mesh_nodes_flat returned an inconsistent buffer");
  }

  // Beam axis = x. Compute span and bail if degenerate.
  double x_min = coords[0];
  double x_max = coords[0];
  for (std::size_t i = 1; i < num_nodes; ++i) {
    const double x = coords[i * 3 + 0];
    if (x < x_min) x_min = x;
    if (x > x_max) x_max = x;
  }
  const double L = x_max - x_min;
  if (!(L > 0.0)) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT,
                                "mesh has zero extent along x; modal-stub needs a beam-like geometry");
  }

  souxmar_field_t* field = souxmar_field_new(
      "modal_displacement", SOUXMAR_FL_NODAL, SOUXMAR_FK_VECTOR,
      num_nodes, static_cast<std::size_t>(n_modes));
  if (!field) {
    return souxmar_status_error(SOUXMAR_E_OUT_OF_MEMORY, "souxmar_field_new failed");
  }
  double*           data      = souxmar_field_data(field);
  const std::size_t data_size = souxmar_field_data_size(field);
  if (!data || data_size != num_nodes * 3 * static_cast<std::size_t>(n_modes)) {
    souxmar_field_free(field);
    return souxmar_status_error(SOUXMAR_E_INTERNAL,
                                "souxmar_field_data buffer size mismatch");
  }

  // Layout (step-major, matches heat-solver):
  //   data[step*num_nodes*3 + node_i*3 + c] is component c of node_i in mode `step`.
  for (int step = 0; step < n_modes; ++step) {
    const double bL    = kBetaL[step];
    const double sigma = (std::cosh(bL) + std::cos(bL)) /
                         (std::sinh(bL) + std::sin(bL));
    const double tip   = mode_shape(1.0, bL, sigma);
    const double inv   = (std::abs(tip) > 0.0) ? (1.0 / tip) : 0.0;
    const std::size_t step_off = static_cast<std::size_t>(step) * num_nodes * 3;
    for (std::size_t i = 0; i < num_nodes; ++i) {
      const double x_norm = (coords[i * 3 + 0] - x_min) / L;
      const double psi    = mode_shape(x_norm, bL, sigma) * inv;
      data[step_off + i * 3 + 0] = 0.0;
      data[step_off + i * 3 + 1] = psi;
      data[step_off + i * 3 + 2] = 0.0;
    }
  }
  *out_field = field;
  return souxmar_status_ok();
}

constexpr souxmar_solver_vtable_t kVtable = {
    SOUXMAR_ABI_VERSION_MAJOR,
    &modal_solve,
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
      registry, "solver.modal.linear", &kVtable, /*user_data=*/nullptr);
  return s.code == SOUXMAR_OK ? 0 : 1;
}
