// SPDX-License-Identifier: Apache-2.0
//
// C ABI implementations of souxmar_mesh_* — opaque pointer cast to
// souxmar::core::Mesh*. Lives in libsouxmar-core because that's where the
// implementation type is defined.

#include "souxmar/core/element_type.h"
#include "souxmar/core/mesh.h"
#include "souxmar/core/tag.h"

#include "souxmar-c/buffer.h"
#include "souxmar-c/mesh.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>

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
  if (!mesh)
    return;
  try {
    as_cpp(mesh)->reserve_nodes(n);
  } catch (...) {}
}

void souxmar_mesh_reserve_cells(souxmar_mesh_t* mesh, size_t n) {
  if (!mesh)
    return;
  try {
    as_cpp(mesh)->reserve_cells(n);
  } catch (...) {}
}

uint64_t souxmar_mesh_add_node(souxmar_mesh_t* mesh, const double position[3]) {
  if (!mesh || !position)
    return UINT64_MAX;
  try {
    auto i = as_cpp(mesh)->add_node({position[0], position[1], position[2]});
    return i.value;
  } catch (...) {
    return UINT64_MAX;
  }
}

souxmar_status_t souxmar_mesh_add_cell(souxmar_mesh_t* mesh,
                                       uint16_t element_type,
                                       const uint64_t* node_indices,
                                       size_t num_node_indices,
                                       int32_t tag,
                                       uint64_t* out_cell_index) {
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
    auto cell_idx = as_cpp(mesh)->add_cell(static_cast<souxmar::core::ElementType>(element_type),
                                           scratch,
                                           souxmar::core::EntityTag{tag});
    if (out_cell_index)
      *out_cell_index = cell_idx.value;
    return souxmar_status_ok();
  } catch (const std::invalid_argument&) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT,
                                "node count does not match element type");
  } catch (const std::out_of_range&) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "node index references missing node");
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
                                   uint64_t index,
                                   double out_position[3]) {
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
  if (!mesh)
    return SOUXMAR_ET_UNKNOWN;
  return static_cast<uint16_t>(as_cpp(mesh)->cell_type(souxmar::core::CellIndex{cell_index}));
}

size_t souxmar_mesh_cell_node_count(const souxmar_mesh_t* mesh, uint64_t cell_index) {
  if (!mesh)
    return 0;
  try {
    return as_cpp(mesh)->cell_nodes(souxmar::core::CellIndex{cell_index}).size();
  } catch (...) {
    return 0;
  }
}

souxmar_status_t souxmar_mesh_cell_nodes(const souxmar_mesh_t* mesh,
                                         uint64_t cell_index,
                                         uint64_t* out_node_indices,
                                         size_t out_capacity) {
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
  if (!mesh)
    return -1;
  return as_cpp(mesh)->cell_tag(souxmar::core::CellIndex{cell_index}).value;
}

size_t souxmar_mesh_cell_face_count(const souxmar_mesh_t* mesh, uint64_t cell_index) {
  if (!mesh)
    return 0;
  if (cell_index >= as_cpp(mesh)->num_cells())
    return 0;
  const auto type = as_cpp(mesh)->cell_type(souxmar::core::CellIndex{cell_index});
  return souxmar::core::num_faces(type);
}

int32_t souxmar_mesh_face_tag(const souxmar_mesh_t* mesh,
                              uint64_t cell_index,
                              uint8_t local_face_index) {
  if (!mesh)
    return SOUXMAR_FACE_UNTAGGED;
  return as_cpp(mesh)->face_tag(souxmar::core::CellIndex{cell_index}, local_face_index).value;
}

souxmar_status_t souxmar_mesh_set_face_tag(souxmar_mesh_t* mesh,
                                           uint64_t cell_index,
                                           uint8_t local_face_index,
                                           int32_t tag) {
  if (!mesh) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "mesh is NULL");
  }
  try {
    as_cpp(mesh)->set_face_tag(
        souxmar::core::CellIndex{cell_index}, local_face_index, souxmar::core::EntityTag{tag});
    return souxmar_status_ok();
  } catch (const std::out_of_range&) {
    return souxmar_status_error(SOUXMAR_E_NOT_FOUND, "cell index out of range");
  } catch (const std::invalid_argument&) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT,
                                "local face index exceeds cell's face count");
  } catch (const std::bad_alloc&) {
    return souxmar_status_error(SOUXMAR_E_OUT_OF_MEMORY, "out of memory");
  } catch (...) {
    return souxmar_status_error(SOUXMAR_E_INTERNAL, "internal error");
  }
}

const double* souxmar_mesh_nodes_flat(const souxmar_mesh_t* mesh, size_t* out_size) {
  if (!mesh) {
    if (out_size)
      *out_size = 0;
    return nullptr;
  }
  const auto span = as_cpp(mesh)->nodes_flat();
  if (out_size)
    *out_size = span.size();
  return span.data();
}

}  // extern "C"

// ============================================================================
// Bulk construction (Sprint 5 push 4 — ADR-0006)
// ============================================================================
//
// souxmar_mesh_from_buffers consumes the buffer descriptor, validates
// every shape invariant the C ABI promises (sizes, offsets, element
// types, node-index ranges), then walks the buffers once to build the
// Mesh. All four allocations (4 std::vector growths inside the Mesh
// impl) are reserved up front from the declared counts so the hot
// loop is amortised O(1) per element.
//
// Validation is mandatory: a malformed buffer reaches us through a
// trusted-but-buggy plugin, and silently corrupting the Mesh would
// leak into downstream solvers / writers as a hard-to-debug crash.

namespace {

// Helper: write a structured status iff out_status is non-null. Returns
// nullptr unconditionally so callers can `return write_err(...)` in
// one expression.
souxmar_mesh_t* write_err(souxmar_status_t* out_status, int code, const char* msg) {
  if (out_status)
    *out_status = souxmar_status_error(code, msg);
  return nullptr;
}

}  // namespace

extern "C" {

souxmar_mesh_t* souxmar_mesh_from_buffers(const souxmar_mesh_buffers_t* buffers,
                                          souxmar_status_t* out_status) {
  if (!buffers) {
    return write_err(
        out_status, SOUXMAR_E_INVALID_ARGUMENT, "souxmar_mesh_from_buffers: buffers is NULL");
  }
  if (!buffers->node_coords || !buffers->cell_types || !buffers->cell_connectivity
      || !buffers->cell_offsets) {
    return write_err(out_status,
                     SOUXMAR_E_INVALID_ARGUMENT,
                     "souxmar_mesh_from_buffers: a required buffer is NULL");
  }

  // Size checks: each buffer must match its declared element count.
  const std::size_t expected_coords_bytes = buffers->num_nodes * 3 * sizeof(double);
  if (souxmar_buffer_size(buffers->node_coords) != expected_coords_bytes) {
    return write_err(out_status,
                     SOUXMAR_E_INVALID_ARGUMENT,
                     "souxmar_mesh_from_buffers: node_coords size != 3*num_nodes*sizeof(double)");
  }
  const std::size_t expected_types_bytes = buffers->num_cells * sizeof(std::uint16_t);
  if (souxmar_buffer_size(buffers->cell_types) != expected_types_bytes) {
    return write_err(out_status,
                     SOUXMAR_E_INVALID_ARGUMENT,
                     "souxmar_mesh_from_buffers: cell_types size != num_cells*sizeof(uint16_t)");
  }
  const std::size_t expected_offsets_bytes = (buffers->num_cells + 1) * sizeof(std::uint64_t);
  if (souxmar_buffer_size(buffers->cell_offsets) != expected_offsets_bytes) {
    return write_err(
        out_status,
        SOUXMAR_E_INVALID_ARGUMENT,
        "souxmar_mesh_from_buffers: cell_offsets size != (num_cells+1)*sizeof(uint64_t)");
  }
  if (buffers->cell_tags) {
    const std::size_t expected_tags_bytes = buffers->num_cells * sizeof(std::int32_t);
    if (souxmar_buffer_size(buffers->cell_tags) != expected_tags_bytes) {
      return write_err(out_status,
                       SOUXMAR_E_INVALID_ARGUMENT,
                       "souxmar_mesh_from_buffers: cell_tags size != num_cells*sizeof(int32_t)");
    }
  }

  // Resolve buffer data pointers. const_cast'ing through is safe; the
  // accessors return void* but our typed const_cast keeps the underlying
  // memory const within this function.
  const auto* coords = static_cast<const double*>(souxmar_buffer_data_const(buffers->node_coords));
  const auto* types =
      static_cast<const std::uint16_t*>(souxmar_buffer_data_const(buffers->cell_types));
  const auto* connectivity =
      static_cast<const std::uint64_t*>(souxmar_buffer_data_const(buffers->cell_connectivity));
  const auto* offsets =
      static_cast<const std::uint64_t*>(souxmar_buffer_data_const(buffers->cell_offsets));
  const auto* tags =
      buffers->cell_tags
          ? static_cast<const std::int32_t*>(souxmar_buffer_data_const(buffers->cell_tags))
          : nullptr;

  // Offset invariants: monotonic non-decreasing, leading zero, terminator
  // = total connectivity entries. We pre-check before touching cells so a
  // malformed offsets buffer can't cascade into out-of-range reads in
  // the connectivity buffer.
  if (buffers->num_cells > 0 && offsets[0] != 0) {
    return write_err(out_status,
                     SOUXMAR_E_INVALID_ARGUMENT,
                     "souxmar_mesh_from_buffers: cell_offsets[0] must be 0");
  }
  for (std::size_t i = 0; i < buffers->num_cells; ++i) {
    if (offsets[i + 1] < offsets[i]) {
      return write_err(
          out_status,
          SOUXMAR_E_INVALID_ARGUMENT,
          "souxmar_mesh_from_buffers: cell_offsets is not monotonically non-decreasing");
    }
  }
  const std::uint64_t total_node_refs =
      (buffers->num_cells == 0) ? 0u : offsets[buffers->num_cells];
  const std::size_t expected_conn_bytes =
      static_cast<std::size_t>(total_node_refs) * sizeof(std::uint64_t);
  if (souxmar_buffer_size(buffers->cell_connectivity) != expected_conn_bytes) {
    return write_err(
        out_status,
        SOUXMAR_E_INVALID_ARGUMENT,
        "souxmar_mesh_from_buffers: cell_connectivity size != offsets[num_cells]*sizeof(uint64_t)");
  }

  // Per-cell semantic validation: the element type must be known and its
  // declared node count must match the slice length.
  for (std::size_t i = 0; i < buffers->num_cells; ++i) {
    const auto et = static_cast<souxmar::core::ElementType>(types[i]);
    const auto info = souxmar::core::info(et);
    if (info.num_nodes == 0 || et == souxmar::core::ElementType::Unknown) {
      if (out_status) {
        *out_status = souxmar_status_error(
            SOUXMAR_E_INVALID_ARGUMENT,
            "souxmar_mesh_from_buffers: cell_types[i] is not a known SOUXMAR_ET_*");
      }
      return nullptr;
    }
    const std::uint64_t this_cell_node_count = offsets[i + 1] - offsets[i];
    if (this_cell_node_count != info.num_nodes) {
      if (out_status) {
        *out_status = souxmar_status_error(
            SOUXMAR_E_INVALID_ARGUMENT,
            "souxmar_mesh_from_buffers: a cell's node count doesn't match its element type");
      }
      return nullptr;
    }
    // Range-check every node index in this cell's slice.
    for (std::uint64_t j = offsets[i]; j < offsets[i + 1]; ++j) {
      if (connectivity[j] >= buffers->num_nodes) {
        if (out_status) {
          *out_status = souxmar_status_error(
              SOUXMAR_E_INVALID_ARGUMENT,
              "souxmar_mesh_from_buffers: cell connectivity references an out-of-range node");
        }
        return nullptr;
      }
    }
  }

  // Build the Mesh. Reserve up-front to avoid mid-loop reallocations.
  std::unique_ptr<souxmar::core::Mesh> mesh;
  try {
    mesh = std::make_unique<souxmar::core::Mesh>();
    mesh->reserve_nodes(buffers->num_nodes);
    mesh->reserve_cells(buffers->num_cells);
  } catch (const std::bad_alloc&) {
    return write_err(
        out_status, SOUXMAR_E_OUT_OF_MEMORY, "souxmar_mesh_from_buffers: Mesh allocation failed");
  }

  // Map plugin-supplied node ids (0..num_nodes-1, contiguous) to host
  // NodeIndex values returned by add_node. The two are equal in practice
  // (the Mesh appends contiguously and returns sequential indices) but
  // we don't rely on that — looking up via the lookup table guards
  // against future Mesh-impl changes.
  std::vector<souxmar::core::NodeIndex> node_lookup;
  try {
    node_lookup.resize(buffers->num_nodes);
    for (std::size_t i = 0; i < buffers->num_nodes; ++i) {
      node_lookup[i] = mesh->add_node({coords[i * 3 + 0], coords[i * 3 + 1], coords[i * 3 + 2]});
    }
  } catch (const std::bad_alloc&) {
    return write_err(
        out_status, SOUXMAR_E_OUT_OF_MEMORY, "souxmar_mesh_from_buffers: node insertion failed");
  }

  // Cells: walk per-cell slices, translate node ids, dispatch to add_cell.
  // The std::vector scratch is reused across iterations to avoid
  // num_cells allocations.
  std::vector<souxmar::core::NodeIndex> cell_nodes;
  cell_nodes.reserve(27);  // hex27 is the largest standard element
  for (std::size_t i = 0; i < buffers->num_cells; ++i) {
    const auto et = static_cast<souxmar::core::ElementType>(types[i]);
    const auto begin = offsets[i];
    const auto end = offsets[i + 1];
    cell_nodes.clear();
    cell_nodes.reserve(end - begin);
    for (std::uint64_t j = begin; j < end; ++j) {
      cell_nodes.push_back(node_lookup[static_cast<std::size_t>(connectivity[j])]);
    }
    const souxmar::core::EntityTag tag{tags ? tags[i] : -1};
    try {
      (void)mesh->add_cell(et, cell_nodes, tag);
    } catch (const std::bad_alloc&) {
      return write_err(
          out_status, SOUXMAR_E_OUT_OF_MEMORY, "souxmar_mesh_from_buffers: cell insertion failed");
    } catch (...) {
      // The Mesh impl validates element-type / node-count match too;
      // we already pre-checked, but a Mesh-side rejection still maps
      // to INVALID_ARGUMENT here for consistency.
      return write_err(out_status,
                       SOUXMAR_E_INVALID_ARGUMENT,
                       "souxmar_mesh_from_buffers: Mesh::add_cell rejected a cell");
    }
  }

  if (out_status)
    *out_status = souxmar_status_ok();
  return as_c(mesh.release());
}

}  // extern "C"
