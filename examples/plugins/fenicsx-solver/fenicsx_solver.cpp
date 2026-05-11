// SPDX-License-Identifier: Apache-2.0
//
// fenicsx-solver — Sprint 7 push 2 opt-in FEM solver.
//
// Registers `solver.heat.fenicsx`. The real heat-equation (Poisson)
// solver behind the docs/SPRINT_PLAN.md Sprint 7 deliverable: convert
// a souxmar Mesh into a `dolfinx::mesh::Mesh`, assemble the Poisson
// problem with PETSc-backed linear algebra, solve, and read the
// solution back into a souxmar `Field`.
//
// Build gating: compiled only when `-DSOUXMAR_WITH_FENICSX=ON` AND
// `find_package(DOLFINX)` succeeds. Default builds do NOT include
// this binary. The default-CI counterpart is the always-on
// `solver.heat.linear` (Sprint 5 push 3 heat-solver, analytical
// closed-form) and `solver.elasticity.linear` (Sprint 7 push 2
// elasticity-stub, also closed-form). FEniCSx-bearing nightly
// runners are the validation-grade environment for this plugin.
//
// Declared `single-threaded`: DOLFINx uses PETSc which holds
// MPI / process-global state. Two pipeline stages can't run this
// plugin concurrently.
//
// LIMITATIONS (v1):
//   - Poisson only. The elasticity vector-equation path needs UFL
//     forms compiled via FFCx — see the comment near `assemble_poisson`
//     for the path to either (a) a FFCx-prebuild step or (b) the
//     hand-rolled tet-element kernels the agent-eval suite expects in
//     Sprint 8.
//   - Homogeneous Dirichlet BCs only (u = 0 on boundary). The full
//     BC manifest from `set_bc` lands when the solver C ABI carries
//     a structured BC array — an additive minor ratchet event
//     (ADR-0008-compliant) tracked for Sprint 8.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "souxmar-c/abi.h"
#include "souxmar-c/field.h"
#include "souxmar-c/mesh.h"
#include "souxmar-c/plugin.h"
#include "souxmar-c/registry.h"
#include "souxmar-c/solver.h"
#include "souxmar-c/status.h"
#include "souxmar-c/value.h"

// Real DOLFINx + PETSc headers. The build option gates this TU from
// compiling when DOLFINx isn't installed.
#include <dolfinx.h>
#include <dolfinx/common/MPI.h>
#include <dolfinx/fem/petsc.h>
#include <dolfinx/la/petsc.h>
#include <dolfinx/mesh/Mesh.h>
#include <dolfinx/mesh/utils.h>
#include <petscksp.h>

namespace {

double read_number(const souxmar_value_t* inputs, const char* key, double dv) {
  if (!inputs || souxmar_value_kind(inputs) != SOUXMAR_VK_MAP) return dv;
  const souxmar_value_t* v = souxmar_value_map_get(inputs, key);
  if (!v || souxmar_value_kind(v) != SOUXMAR_VK_NUMBER) return dv;
  return souxmar_value_as_number(v);
}

// Convert a souxmar mesh (Tet4 cells via the C ABI accessors) into the
// flat node-coordinate + cell-connectivity arrays DOLFINx ingests.
//
// DOLFINx expects:
//   - coords:  row-major (3*num_nodes) double buffer
//   - cells :  flat (4*num_cells) int64 buffer of node indices
//   - shape :  dolfinx::mesh::CellType::tetrahedron
struct DolfinxIngestBuffers {
  std::vector<double>   coords;
  std::vector<std::int64_t> cells;
  std::size_t num_nodes = 0;
  std::size_t num_cells = 0;
};

souxmar_status_t build_ingest_buffers(const souxmar_mesh_t*       mesh,
                                      DolfinxIngestBuffers&       out) {
  out.num_nodes = souxmar_mesh_num_nodes(mesh);
  out.num_cells = souxmar_mesh_num_cells(mesh);
  if (out.num_nodes == 0 || out.num_cells == 0) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT,
        "fenicsx_solver requires a non-empty mesh");
  }

  std::size_t flat_size = 0;
  const double* src_coords = souxmar_mesh_nodes_flat(mesh, &flat_size);
  if (!src_coords || flat_size != out.num_nodes * 3) {
    return souxmar_status_error(SOUXMAR_E_INTERNAL,
        "souxmar_mesh_nodes_flat returned an inconsistent buffer");
  }
  out.coords.assign(src_coords, src_coords + flat_size);

  out.cells.reserve(out.num_cells * 4);
  std::uint64_t cell_ids[4] = {0, 0, 0, 0};
  for (std::size_t c = 0; c < out.num_cells; ++c) {
    if (souxmar_mesh_cell_type(mesh, c) != SOUXMAR_ET_TET4) {
      return souxmar_status_error(SOUXMAR_E_NOT_IMPLEMENTED,
          "fenicsx_solver currently supports Tet4 cells only");
    }
    const auto cs = souxmar_mesh_cell_nodes(mesh, c, cell_ids, 4);
    if (cs.code != SOUXMAR_OK) return cs;
    for (int k = 0; k < 4; ++k) {
      out.cells.push_back(static_cast<std::int64_t>(cell_ids[k]));
    }
  }
  return souxmar_status_ok();
}

// Hand the buffers to dolfinx + assemble + solve the Poisson problem
//
//     -Δu = f      in Ω
//        u = 0     on ∂Ω
//
// with f = source_term (constant scalar, read from the input bag).
// Returns the per-node solution u as a flat double vector.
//
// The matrix assembly path here uses the dolfinx::fem helpers + a
// pre-built FFCx form `poisson_a` / `poisson_L` linked from the
// FEniCSx adapter's own form-compilation step (see CMakeLists.txt).
// The form itself is the standard P1 Poisson weak form; the only
// reason it lives outside this file is that FFCx requires a separate
// compilation pass.
std::vector<double> assemble_poisson(const DolfinxIngestBuffers& bufs,
                                     double                       source_term) {
  using namespace dolfinx;

  auto comm = MPI_COMM_WORLD;
  // Build the dolfinx mesh from the ingested flat buffers. Element
  // ordering convention: souxmar Tet4 → dolfinx tetrahedron (same).
  auto mesh = std::make_shared<mesh::Mesh<double>>(
      mesh::create_mesh(comm,
                        bufs.cells,
                        mesh::CellType::tetrahedron,
                        bufs.coords,
                        mesh::GhostMode::none));

  // FFCx-compiled forms are linked from the build step alongside this
  // TU. The `poisson_a` form is the bilinear stiffness; `poisson_L` is
  // the linear functional carrying the source term.
  extern ufcx_form poisson_a, poisson_L;

  auto V = std::make_shared<fem::FunctionSpace<double>>(
      fem::create_functionspace(mesh, poisson_a, "u"));

  auto f = std::make_shared<fem::Constant<double>>(source_term);
  auto a = std::make_shared<fem::Form<double>>(
      fem::create_form<double>(poisson_a, {V, V}, {}, {}, {}));
  auto L = std::make_shared<fem::Form<double>>(
      fem::create_form<double>(poisson_L, {V}, {}, {{"f", f}}, {}));

  // Homogeneous Dirichlet BC on the whole boundary.
  auto facets = mesh::locate_entities_boundary(
      *mesh, mesh->topology()->dim() - 1,
      [](auto) { return true; });
  auto dofs = fem::locate_dofs_topological(
      {*V->mesh()->topology()}, *V->dofmap(), mesh->topology()->dim() - 1,
      facets);
  std::vector<std::shared_ptr<const fem::DirichletBC<double>>> bcs;
  bcs.push_back(std::make_shared<fem::DirichletBC<double>>(
      0.0, dofs, V));

  // PETSc-backed assemble + KSP solve.
  auto A = la::petsc::Matrix(fem::petsc::create_matrix(*a), false);
  la::Vector<double> b(L->function_spaces()[0]->dofmap()->index_map,
                        L->function_spaces()[0]->dofmap()->index_map_bs());
  fem::assemble_matrix(la::petsc::Matrix::set_block_fn(A.mat(), ADD_VALUES),
                       *a, bcs);
  MatAssemblyBegin(A.mat(), MAT_FINAL_ASSEMBLY);
  MatAssemblyEnd(A.mat(),   MAT_FINAL_ASSEMBLY);
  fem::assemble_vector(b.mutable_array(), *L);
  fem::apply_lifting<double>(b.mutable_array(), {a}, {bcs}, {}, 1.0);
  b.scatter_rev(std::plus<>());
  fem::set_bc<double>(b.mutable_array(), bcs);

  la::Vector<double> u(L->function_spaces()[0]->dofmap()->index_map,
                        L->function_spaces()[0]->dofmap()->index_map_bs());
  la::petsc::KrylovSolver solver(comm);
  solver.set_operator(A.mat());
  solver.set_options_prefix("souxmar_fenicsx_");
  solver.set_from_options();
  solver.solve(u.mutable_array().data(), b.array().data());

  return std::vector<double>(u.array().begin(), u.array().end());
}

souxmar_status_t fenicsx_solve(const souxmar_mesh_t*           mesh,
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

  DolfinxIngestBuffers bufs;
  if (const auto s = build_ingest_buffers(mesh, bufs); s.code != SOUXMAR_OK) {
    return s;
  }

  const double source_term = read_number(inputs, "source_term", 1.0);

  std::vector<double> u_flat;
  try {
    u_flat = assemble_poisson(bufs, source_term);
  } catch (const std::exception& e) {
    static thread_local std::string msg;
    msg = std::string("dolfinx threw: ") + e.what();
    return souxmar_status_error(SOUXMAR_E_PLUGIN_REJECTED, msg.c_str());
  }
  if (u_flat.size() != bufs.num_nodes) {
    return souxmar_status_error(SOUXMAR_E_INTERNAL,
        "fenicsx_solver: DOLFINx returned a solution vector of unexpected size");
  }

  souxmar_field_t* field = souxmar_field_new(
      "temperature", SOUXMAR_FL_NODAL, SOUXMAR_FK_SCALAR,
      bufs.num_nodes, /*num_time_steps=*/1);
  if (!field) {
    return souxmar_status_error(SOUXMAR_E_OUT_OF_MEMORY, "souxmar_field_new failed");
  }
  double* data = souxmar_field_data(field);
  const std::size_t data_size = souxmar_field_data_size(field);
  if (!data || data_size != bufs.num_nodes) {
    souxmar_field_free(field);
    return souxmar_status_error(SOUXMAR_E_INTERNAL,
        "souxmar_field_data buffer size mismatch");
  }
  std::copy(u_flat.begin(), u_flat.end(), data);
  *out_field = field;
  return souxmar_status_ok();
}

constexpr souxmar_solver_vtable_t kVtable = {
    SOUXMAR_ABI_VERSION_MAJOR,
    &fenicsx_solve,
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
      registry, "solver.heat.fenicsx", &kVtable, /*user_data=*/nullptr);
  return s.code == SOUXMAR_OK ? 0 : 1;
}
