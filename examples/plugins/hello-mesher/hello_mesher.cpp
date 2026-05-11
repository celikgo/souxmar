// SPDX-License-Identifier: Apache-2.0
//
// hello-mesher — the canonical reference plugin. The smallest possible
// thing that exercises the full plugin SDK contract.
//
// Sprint 3 push 2 upgraded `mesh_fn` to actually produce a mesh: a single
// unit tetrahedron, ignoring the input geometry and options. That's enough
// to exercise the C ABI mesh-handle accessors end-to-end (souxmar_mesh_new,
// souxmar_mesh_add_node, souxmar_mesh_add_cell) and to unblock the writer
// integration test. A real production mesher comes from the OpenCASCADE +
// Gmsh adapters in Sprint 6.

#include "souxmar-c/abi.h"
#include "souxmar-c/mesh.h"
#include "souxmar-c/mesher.h"
#include "souxmar-c/plugin.h"
#include "souxmar-c/registry.h"
#include "souxmar-c/status.h"

namespace {

souxmar_status_t hello_mesh(const souxmar_geometry_t*       /*geometry*/,
                            const souxmar_mesher_options_t* /*options*/,
                            souxmar_mesh_t**                out_mesh,
                            void*                           /*user_data*/) {
  if (out_mesh == nullptr) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "out_mesh is NULL");
  }

  souxmar_mesh_t* mesh = souxmar_mesh_new();
  if (mesh == nullptr) {
    return souxmar_status_error(SOUXMAR_E_OUT_OF_MEMORY, "souxmar_mesh_new failed");
  }

  // Unit tetrahedron with one corner at the origin.
  const double p0[3] = {0.0, 0.0, 0.0};
  const double p1[3] = {1.0, 0.0, 0.0};
  const double p2[3] = {0.0, 1.0, 0.0};
  const double p3[3] = {0.0, 0.0, 1.0};
  const uint64_t n0 = souxmar_mesh_add_node(mesh, p0);
  const uint64_t n1 = souxmar_mesh_add_node(mesh, p1);
  const uint64_t n2 = souxmar_mesh_add_node(mesh, p2);
  const uint64_t n3 = souxmar_mesh_add_node(mesh, p3);

  const uint64_t tet_nodes[4] = {n0, n1, n2, n3};
  const souxmar_status_t add_status = souxmar_mesh_add_cell(
      mesh, SOUXMAR_ET_TET4, tet_nodes, 4, /*tag=*/-1, /*out_cell_index=*/nullptr);
  if (add_status.code != SOUXMAR_OK) {
    souxmar_mesh_free(mesh);
    return add_status;
  }

  *out_mesh = mesh;
  return souxmar_status_ok();
}

constexpr souxmar_mesher_vtable_t kVtable = {
    SOUXMAR_ABI_VERSION_MAJOR,  // abi_version
    &hello_mesh,                // mesh_fn
    nullptr,                    // destroy_fn
};

}  // namespace

extern "C" SOUXMAR_PLUGIN_EXPORT
int souxmar_plugin_register_v1(souxmar_registry_t*        registry,
                               const souxmar_host_info_t* host) {
  if (host == nullptr || host->abi_version_major < SOUXMAR_ABI_VERSION_MAJOR) {
    return -1;  // ABI mismatch — refuse to register against an older host
  }

  const souxmar_status_t s =
      souxmar_registry_add_mesher(registry,
                                  "mesher.tetra.hello",
                                  &kVtable,
                                  /*user_data=*/nullptr);
  return s.code == SOUXMAR_OK ? 0 : 1;
}
