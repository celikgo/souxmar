// SPDX-License-Identifier: Apache-2.0
//
// C ABI implementations of souxmar_mesh_* — opaque pointer cast to
// souxmar::core::Mesh*. Lives in libsouxmar-core because that's where the
// implementation type is defined.

#include "souxmar-c/mesh.h"

#include <cstring>
#include <new>

#include "souxmar/core/mesh.h"

namespace {

souxmar::core::Mesh* as_cpp(souxmar_mesh_t* p) noexcept {
  return reinterpret_cast<souxmar::core::Mesh*>(p);
}

const souxmar::core::Mesh* as_cpp(const souxmar_mesh_t* p) noexcept {
  return reinterpret_cast<const souxmar::core::Mesh*>(p);
}

souxmar_mesh_t* as_c(souxmar::core::Mesh* p) noexcept {
  return reinterpret_cast<souxmar_mesh_t*>(p);
}

}  // namespace

extern "C" {

souxmar_mesh_t* souxmar_mesh_new(void) {
  try {
    return as_c(new souxmar::core::Mesh{});
  } catch (...) {
    return nullptr;
  }
}

void souxmar_mesh_free(souxmar_mesh_t* mesh) {
  delete as_cpp(mesh);
}

void souxmar_mesh_reserve_nodes(souxmar_mesh_t* mesh, size_t n) {
  if (!mesh) return;
  try { as_cpp(mesh)->reserve_nodes(n); } catch (...) {}
}

void souxmar_mesh_reserve_cells(souxmar_mesh_t* mesh, size_t n) {
  if (!mesh) return;
  try { as_cpp(mesh)->reserve_cells(n); } catch (...) {}
}

uint64_t souxmar_mesh_add_node(souxmar_mesh_t* mesh, const double position[3]) {
  if (!mesh || !position) return UINT64_MAX;
  try {
    auto i = as_cpp(mesh)->add_node({position[0], position[1], position[2]});
    return i.value;
  } catch (...) {
    return UINT64_MAX;
  }
}

souxmar_status_t souxmar_mesh_add_cell(souxmar_mesh_t*  mesh,
                                       uint16_t         element_type,
                                       const uint64_t*  node_indices,
                                       size_t           num_node_indices,
                                       int32_t          tag,
                                       uint64_t*        out_cell_index) {
  if (!mesh) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "mesh is NULL");
  }
  if (num_node_indices > 0 && !node_indices) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "node_indices is NULL");
  }
  // The Mesh::add_cell signature wants std::span<const NodeIndex>; build that.
  static thread_local std::vector<souxmar::core::NodeIndex> scratch;
  scratch.assign(num_node_indices, souxmar::core::NodeIndex{});
  for (size_t i = 0; i < num_node_indices; ++i) {
    scratch[i].value = node_indices[i];
  }
  try {
    auto cell_idx = as_cpp(mesh)->add_cell(
        static_cast<souxmar::core::ElementType>(element_type),
        scratch,
        souxmar::core::EntityTag{tag});
    if (out_cell_index) *out_cell_index = cell_idx.value;
    return souxmar_status_ok();
  } catch (const std::invalid_argument&) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT,
                                "node count does not match element type");
  } catch (const std::out_of_range&) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT,
                                "node index references missing node");
  } catch (const std::bad_alloc&) {
    return souxmar_status_error(SOUXMAR_E_OUT_OF_MEMORY, "out of memory");
  } catch (...) {
    return souxmar_status_error(SOUXMAR_E_INTERNAL, "internal error");
  }
}

size_t souxmar_mesh_num_nodes(const souxmar_mesh_t* mesh) {
  return mesh ? as_cpp(mesh)->num_nodes() : 0;
}

size_t souxmar_mesh_num_cells(const souxmar_mesh_t* mesh) {
  return mesh ? as_cpp(mesh)->num_cells() : 0;
}

souxmar_status_t souxmar_mesh_node(const souxmar_mesh_t* mesh,
                                   uint64_t              index,
                                   double                out_position[3]) {
  if (!mesh || !out_position) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "NULL mesh or out_position");
  }
  try {
    const auto p = as_cpp(mesh)->node(souxmar::core::NodeIndex{index});
    out_position[0] = p[0];
    out_position[1] = p[1];
    out_position[2] = p[2];
    return souxmar_status_ok();
  } catch (const std::out_of_range&) {
    return souxmar_status_error(SOUXMAR_E_NOT_FOUND, "node index out of range");
  } catch (...) {
    return souxmar_status_error(SOUXMAR_E_INTERNAL, "internal error");
  }
}

uint16_t souxmar_mesh_cell_type(const souxmar_mesh_t* mesh, uint64_t cell_index) {
  if (!mesh) return SOUXMAR_ET_UNKNOWN;
  return static_cast<uint16_t>(as_cpp(mesh)->cell_type(souxmar::core::CellIndex{cell_index}));
}

size_t souxmar_mesh_cell_node_count(const souxmar_mesh_t* mesh, uint64_t cell_index) {
  if (!mesh) return 0;
  try {
    return as_cpp(mesh)->cell_nodes(souxmar::core::CellIndex{cell_index}).size();
  } catch (...) {
    return 0;
  }
}

souxmar_status_t souxmar_mesh_cell_nodes(const souxmar_mesh_t* mesh,
                                         uint64_t              cell_index,
                                         uint64_t*             out_node_indices,
                                         size_t                out_capacity) {
  if (!mesh || !out_node_indices) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "NULL pointer");
  }
  try {
    const auto nodes = as_cpp(mesh)->cell_nodes(souxmar::core::CellIndex{cell_index});
    if (out_capacity < nodes.size()) {
      return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT,
                                  "out_capacity smaller than cell's node count");
    }
    for (size_t i = 0; i < nodes.size(); ++i) {
      out_node_indices[i] = nodes[i].value;
    }
    return souxmar_status_ok();
  } catch (const std::out_of_range&) {
    return souxmar_status_error(SOUXMAR_E_NOT_FOUND, "cell index out of range");
  } catch (...) {
    return souxmar_status_error(SOUXMAR_E_INTERNAL, "internal error");
  }
}

int32_t souxmar_mesh_cell_tag(const souxmar_mesh_t* mesh, uint64_t cell_index) {
  if (!mesh) return -1;
  return as_cpp(mesh)->cell_tag(souxmar::core::CellIndex{cell_index}).value;
}

const double* souxmar_mesh_nodes_flat(const souxmar_mesh_t* mesh, size_t* out_size) {
  if (!mesh) {
    if (out_size) *out_size = 0;
    return nullptr;
  }
  const auto span = as_cpp(mesh)->nodes_flat();
  if (out_size) *out_size = span.size();
  return span.data();
}

}  // extern "C"
