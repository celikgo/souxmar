// SPDX-License-Identifier: Apache-2.0
//
// mesh-quality — Sprint 6 push 1 reference postproc plugin. Registers
// `postproc.mesh_quality`: takes a mesh, ignores the input field (the
// postproc ABI passes one for uniformity), and produces a per-cell
// FieldKind::Vector Field with three components per cell:
//
//   c[0] = signed_volume     (3D) or signed area (2D), NaN otherwise
//   c[1] = edge_ratio        (max edge / min edge)
//   c[2] = min_dihedral_deg  (Tet4 only; NaN for Tri3 and unsupported)
//
// The metric ids match `souxmar::core::quality::Metric` — the agent
// tool and unit tests consume the same component layout.
//
// The plugin links libsouxmar-core for the metric math. That's
// permissible because this plugin ships in-tree; it does not establish
// a precedent for out-of-tree plugins.
//
// Declares itself reentrant: pure functional transform over the input
// mesh, no shared state.

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

#include "souxmar-c/abi.h"
#include "souxmar-c/field.h"
#include "souxmar-c/mesh.h"
#include "souxmar-c/plugin.h"
#include "souxmar-c/postproc.h"
#include "souxmar-c/registry.h"
#include "souxmar-c/status.h"
#include "souxmar-c/value.h"

#include "souxmar/core/element_type.h"
#include "souxmar/core/mesh_quality.h"

namespace {

using Vec3 = std::array<double, 3>;

souxmar_status_t mesh_quality_compute(
    const souxmar_mesh_t*               mesh,
    const souxmar_field_t*              /*input_field*/,
    const souxmar_value_t*              /*inputs*/,
    const souxmar_postproc_options_t*   /*options*/,
    souxmar_field_t**                   out_field,
    void*                               /*user_data*/) {
  if (!mesh) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "mesh is NULL");
  }
  if (!out_field) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "out_field is NULL");
  }

  const std::size_t num_cells = souxmar_mesh_num_cells(mesh);
  const std::size_t num_nodes = souxmar_mesh_num_nodes(mesh);
  if (num_cells == 0) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "mesh has no cells");
  }

  std::size_t flat_size = 0;
  const double* coords = souxmar_mesh_nodes_flat(mesh, &flat_size);
  if (!coords || flat_size != num_nodes * 3) {
    return souxmar_status_error(SOUXMAR_E_INTERNAL,
                                "souxmar_mesh_nodes_flat returned an inconsistent buffer");
  }

  // FieldKind::Vector has exactly 3 components — one per metric in v1.
  // The Sprint 6 push 2/3 catalogue addition will likely move to a
  // dedicated FieldKind (or to an array-of-scalar fields) when we need
  // a 4th metric. Documented in include/souxmar/core/mesh_quality.h.
  souxmar_field_t* out = souxmar_field_new(
      "mesh_quality", SOUXMAR_FL_CELL, SOUXMAR_FK_VECTOR, num_cells,
      /*num_time_steps=*/1);
  if (!out) {
    return souxmar_status_error(SOUXMAR_E_OUT_OF_MEMORY, "souxmar_field_new failed");
  }
  double* out_data = souxmar_field_data(out);
  if (!out_data) {
    souxmar_field_free(out);
    return souxmar_status_error(SOUXMAR_E_INTERNAL,
                                "souxmar_field_data returned NULL");
  }

  // Per-cell scratch — sized to the largest element type we handle.
  std::vector<Vec3>           nodes_xyz;
  std::vector<std::uint64_t>  node_ids;
  nodes_xyz.reserve(27);  // Hex27 worst case
  node_ids.reserve(27);

  for (std::size_t cell = 0; cell < num_cells; ++cell) {
    const std::uint16_t type_u16   = souxmar_mesh_cell_type(mesh, cell);
    const auto          type       = static_cast<souxmar::core::ElementType>(type_u16);
    const std::size_t   node_count = souxmar_mesh_cell_node_count(mesh, cell);

    if (node_count == 0) {
      // Unknown / out-of-range cell — emit NaNs and move on. The
      // upstream histogram already names what's in the mesh.
      for (std::size_t m = 0; m < souxmar::core::quality::kNumMetrics; ++m) {
        out_data[cell * souxmar::core::quality::kNumMetrics + m] =
            std::numeric_limits<double>::quiet_NaN();
      }
      continue;
    }

    node_ids.assign(node_count, 0);
    const souxmar_status_t cs =
        souxmar_mesh_cell_nodes(mesh, cell, node_ids.data(), node_count);
    if (cs.code != SOUXMAR_OK) {
      souxmar_field_free(out);
      return cs;
    }

    nodes_xyz.assign(node_count, Vec3{});
    for (std::size_t k = 0; k < node_count; ++k) {
      const std::uint64_t nid = node_ids[k];
      if (nid >= num_nodes) {
        souxmar_field_free(out);
        return souxmar_status_error(SOUXMAR_E_INTERNAL,
                                    "cell references out-of-range node id");
      }
      nodes_xyz[k] = {coords[nid * 3 + 0],
                      coords[nid * 3 + 1],
                      coords[nid * 3 + 2]};
    }

    std::span<double> out_cell{out_data + cell * souxmar::core::quality::kNumMetrics,
                               souxmar::core::quality::kNumMetrics};
    souxmar::core::quality::evaluate_all(type, nodes_xyz, out_cell);
  }

  *out_field = out;
  return souxmar_status_ok();
}

constexpr souxmar_postproc_vtable_t kVtable = {
    SOUXMAR_ABI_VERSION_MAJOR,
    &mesh_quality_compute,
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
      registry, "postproc.mesh_quality", &kVtable, /*user_data=*/nullptr);
  return s.code == SOUXMAR_OK ? 0 : 1;
}
