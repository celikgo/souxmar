// SPDX-License-Identifier: Apache-2.0
//
// elasticity-stub — Sprint 7 push 2 reference linear-elasticity solver.
//
// Registers `solver.elasticity.linear` (the same capability id the
// FEniCSx adapter shipped beside this plugin claims). The point of
// this plugin is to give the agent eval suite, the cantilever-beam
// example, and the documentation tutorials a runnable solver in the
// default CI matrix — without dragging DOLFINx + PETSc into every
// build.
//
// What it computes:
//   Closed-form uniaxial tension along x.
//     eps_xx = load_magnitude / youngs_modulus
//     u_x(node) = eps_xx * x
//     u_y(node) = -nu * eps_xx * y
//     u_z(node) = -nu * eps_xx * z
//
// This is the analytical solution to a 1D bar pulled at one end —
// it's what a real linear-elasticity solver should converge to on a
// uniaxial-load mesh (the canonical patch test). The skill
// `validating-solver` describes how the FEniCSx adapter validates
// against this exact closed form.
//
// What this is NOT: a real FEM solver. It ignores boundary
// conditions (eat the load_magnitude as the only input), ignores
// mesh-dependent stiffness, ignores cell tags. A real linear-
// elasticity solver lands in Sprint 7 push 2's FEniCSx adapter when
// `-DSOUXMAR_WITH_FENICSX=ON` is enabled and DOLFINx is installed.
//
// Inputs (souxmar_value_t map):
//   load_magnitude  : number, defaults to 1.0 (Pa-equivalent units)
//   youngs_modulus  : number, defaults to 210e9 (steel-ish)
//   poisson_ratio   : number, defaults to 0.3   (steel-ish)
//
// Output: nodal vector Field "displacement" with 3 components, 1
// time step.

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

double read_number(const souxmar_value_t* inputs, const char* key, double dv) {
  if (!inputs || souxmar_value_kind(inputs) != SOUXMAR_VK_MAP) return dv;
  const souxmar_value_t* v = souxmar_value_map_get(inputs, key);
  if (!v || souxmar_value_kind(v) != SOUXMAR_VK_NUMBER) return dv;
  return souxmar_value_as_number(v);
}

souxmar_status_t elasticity_solve(const souxmar_mesh_t*           mesh,
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

  const double load   = read_number(inputs, "load_magnitude", 1.0);
  const double E      = std::max(read_number(inputs, "youngs_modulus", 210e9), 1.0);
  const double nu     = std::clamp(read_number(inputs, "poisson_ratio", 0.3),
                                   -0.99, 0.49);
  const double eps_xx = load / E;

  std::size_t flat_size = 0;
  const double* coords = souxmar_mesh_nodes_flat(mesh, &flat_size);
  if (!coords || flat_size != num_nodes * 3) {
    return souxmar_status_error(SOUXMAR_E_INTERNAL,
                                "souxmar_mesh_nodes_flat returned an inconsistent buffer");
  }

  souxmar_field_t* field =
      souxmar_field_new("displacement", SOUXMAR_FL_NODAL, SOUXMAR_FK_VECTOR,
                        num_nodes, /*num_time_steps=*/1);
  if (!field) {
    return souxmar_status_error(SOUXMAR_E_OUT_OF_MEMORY, "souxmar_field_new failed");
  }
  double*     data      = souxmar_field_data(field);
  const std::size_t data_size = souxmar_field_data_size(field);
  if (!data || data_size != num_nodes * 3) {
    souxmar_field_free(field);
    return souxmar_status_error(SOUXMAR_E_INTERNAL,
                                "souxmar_field_data buffer size mismatch");
  }

  // Layout: data[node*3 + 0..2] = (u_x, u_y, u_z) at that node.
  for (std::size_t i = 0; i < num_nodes; ++i) {
    const double x = coords[i * 3 + 0];
    const double y = coords[i * 3 + 1];
    const double z = coords[i * 3 + 2];
    data[i * 3 + 0] =        eps_xx * x;
    data[i * 3 + 1] = -nu  * eps_xx * y;
    data[i * 3 + 2] = -nu  * eps_xx * z;
  }
  *out_field = field;
  return souxmar_status_ok();
}

constexpr souxmar_solver_vtable_t kVtable = {
    SOUXMAR_ABI_VERSION_MAJOR,
    &elasticity_solve,
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
      registry, "solver.elasticity.linear", &kVtable, /*user_data=*/nullptr);
  return s.code == SOUXMAR_OK ? 0 : 1;
}
