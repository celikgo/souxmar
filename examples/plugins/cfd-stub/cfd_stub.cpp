// SPDX-License-Identifier: Apache-2.0
//
// cfd-stub — Sprint 8 push 2 reference CFD solver.
//
// Registers `solver.cfd.simple`. The point of this plugin is the
// same as elasticity-stub from Sprint 7 push 2: give the agent eval
// suite, the pipe-bend example, and the documentation tutorials a
// runnable CFD solver in the default CI matrix — without dragging
// OpenFOAM into every build. The validation-grade companion is the
// opt-in fenicsx-/OpenFOAM-backed adapter sibling.
//
// What it computes:
//   Closed-form uniform velocity field along x.
//     U_x(node) = velocity_magnitude
//     U_y(node) = 0
//     U_z(node) = 0
//
// What this is NOT: a real CFD solver. It ignores boundary conditions,
// ignores mesh topology, ignores viscosity, ignores everything except
// the requested magnitude. It's exactly enough for downstream stages
// (postproc.scalar_magnitude, writer.vtu) to ingest a vector field
// whose shape matches what a real solver would emit.
//
// Inputs (souxmar_value_t map):
//   velocity_magnitude  : number, defaults to 1.0 (m/s)
//   flow_direction      : optional list [x, y, z]; defaults to [1, 0, 0]
//
// Output: nodal vector Field "velocity" with 3 components, 1 time step.

#include <array>
#include <cstddef>
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

std::array<double, 3>
read_direction(const souxmar_value_t* inputs) {
  std::array<double, 3> d{1.0, 0.0, 0.0};
  if (!inputs || souxmar_value_kind(inputs) != SOUXMAR_VK_MAP) return d;
  const souxmar_value_t* v = souxmar_value_map_get(inputs, "flow_direction");
  if (!v || souxmar_value_kind(v) != SOUXMAR_VK_LIST) return d;
  const std::size_t n = souxmar_value_list_size(v);
  if (n != 3) return d;
  for (std::size_t i = 0; i < 3; ++i) {
    const souxmar_value_t* c = souxmar_value_list_at(v, i);
    if (c && souxmar_value_kind(c) == SOUXMAR_VK_NUMBER) {
      d[i] = souxmar_value_as_number(c);
    }
  }
  return d;
}

souxmar_status_t cfd_solve(const souxmar_mesh_t*           mesh,
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

  const double magnitude = read_number(inputs, "velocity_magnitude", 1.0);
  const auto   direction = read_direction(inputs);

  souxmar_field_t* field =
      souxmar_field_new("velocity", SOUXMAR_FL_NODAL, SOUXMAR_FK_VECTOR,
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

  for (std::size_t i = 0; i < num_nodes; ++i) {
    data[i * 3 + 0] = magnitude * direction[0];
    data[i * 3 + 1] = magnitude * direction[1];
    data[i * 3 + 2] = magnitude * direction[2];
  }
  *out_field = field;
  return souxmar_status_ok();
}

constexpr souxmar_solver_vtable_t kVtable = {
    SOUXMAR_ABI_VERSION_MAJOR,
    &cfd_solve,
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
      registry, "solver.cfd.simple", &kVtable, /*user_data=*/nullptr);
  return s.code == SOUXMAR_OK ? 0 : 1;
}
