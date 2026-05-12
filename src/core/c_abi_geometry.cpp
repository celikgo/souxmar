// SPDX-License-Identifier: Apache-2.0

#include "souxmar/core/geometry.h"

#include "souxmar-c/geometry.h"

namespace {

souxmar::core::Geometry* as_cpp(souxmar_geometry_t* p) noexcept {
  return reinterpret_cast<souxmar::core::Geometry*>(p);
}

const souxmar::core::Geometry* as_cpp(const souxmar_geometry_t* p) noexcept {
  return reinterpret_cast<const souxmar::core::Geometry*>(p);
}

souxmar_geometry_t* as_c(souxmar::core::Geometry* p) noexcept {
  return reinterpret_cast<souxmar_geometry_t*>(p);
}

souxmar::core::EntityKind to_kind(uint8_t k) noexcept {
  return static_cast<souxmar::core::EntityKind>(k);
}

}  // namespace

extern "C" {

souxmar_geometry_t* souxmar_geometry_new(void) {
  try {
    return as_c(new souxmar::core::Geometry{});
  } catch (...) {
    return nullptr;
  }
}

void souxmar_geometry_free(souxmar_geometry_t* g) {
  delete as_cpp(g);
}

uint32_t souxmar_geometry_add_vertex(souxmar_geometry_t* g, const double position[3]) {
  if (!g || !position)
    return UINT32_MAX;
  try {
    auto i = as_cpp(g)->add_vertex({position[0], position[1], position[2]});
    return i.value;
  } catch (...) {
    return UINT32_MAX;
  }
}

uint32_t souxmar_geometry_add_edge(souxmar_geometry_t* g) {
  if (!g)
    return UINT32_MAX;
  try {
    return as_cpp(g)->add_edge().value;
  } catch (...) {
    return UINT32_MAX;
  }
}

uint32_t souxmar_geometry_add_face(souxmar_geometry_t* g) {
  if (!g)
    return UINT32_MAX;
  try {
    return as_cpp(g)->add_face().value;
  } catch (...) {
    return UINT32_MAX;
  }
}

uint32_t souxmar_geometry_add_solid(souxmar_geometry_t* g) {
  if (!g)
    return UINT32_MAX;
  try {
    return as_cpp(g)->add_solid().value;
  } catch (...) {
    return UINT32_MAX;
  }
}

souxmar_status_t souxmar_geometry_set_tag(souxmar_geometry_t* g,
                                          uint8_t kind,
                                          uint32_t index,
                                          int32_t tag) {
  if (!g)
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "geometry is NULL");
  try {
    as_cpp(g)->set_tag({to_kind(kind), index}, souxmar::core::EntityTag{tag});
    return souxmar_status_ok();
  } catch (const std::out_of_range&) {
    return souxmar_status_error(SOUXMAR_E_NOT_FOUND, "ref out of range");
  } catch (...) {
    return souxmar_status_error(SOUXMAR_E_INTERNAL, "internal error");
  }
}

souxmar_status_t souxmar_geometry_set_name(souxmar_geometry_t* g,
                                           uint8_t kind,
                                           uint32_t index,
                                           const char* name) {
  if (!g || !name)
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "NULL pointer");
  try {
    as_cpp(g)->set_name({to_kind(kind), index}, name);
    return souxmar_status_ok();
  } catch (const std::out_of_range&) {
    return souxmar_status_error(SOUXMAR_E_NOT_FOUND, "ref out of range");
  } catch (const std::bad_alloc&) {
    return souxmar_status_error(SOUXMAR_E_OUT_OF_MEMORY, "out of memory");
  } catch (...) {
    return souxmar_status_error(SOUXMAR_E_INTERNAL, "internal error");
  }
}

size_t souxmar_geometry_count(const souxmar_geometry_t* g, uint8_t kind) {
  return g ? as_cpp(g)->count(to_kind(kind)) : 0;
}

size_t souxmar_geometry_num_vertices(const souxmar_geometry_t* g) {
  return g ? as_cpp(g)->num_vertices() : 0;
}

size_t souxmar_geometry_num_edges(const souxmar_geometry_t* g) {
  return g ? as_cpp(g)->num_edges() : 0;
}

size_t souxmar_geometry_num_faces(const souxmar_geometry_t* g) {
  return g ? as_cpp(g)->num_faces() : 0;
}

size_t souxmar_geometry_num_solids(const souxmar_geometry_t* g) {
  return g ? as_cpp(g)->num_solids() : 0;
}

souxmar_status_t souxmar_geometry_vertex_position(const souxmar_geometry_t* g,
                                                  uint32_t vertex_index,
                                                  double out_position[3]) {
  if (!g || !out_position)
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "NULL pointer");
  try {
    auto p = as_cpp(g)->vertex_position(souxmar::core::VertexIndex{vertex_index});
    out_position[0] = p[0];
    out_position[1] = p[1];
    out_position[2] = p[2];
    return souxmar_status_ok();
  } catch (const std::out_of_range&) {
    return souxmar_status_error(SOUXMAR_E_NOT_FOUND, "vertex index out of range");
  }
}

int32_t souxmar_geometry_tag(const souxmar_geometry_t* g, uint8_t kind, uint32_t index) {
  if (!g)
    return -1;
  return as_cpp(g)->tag({to_kind(kind), index}).value;
}

const char* souxmar_geometry_name(const souxmar_geometry_t* g, uint8_t kind, uint32_t index) {
  if (!g)
    return nullptr;
  auto sv = as_cpp(g)->name({to_kind(kind), index});
  if (!sv.has_value())
    return nullptr;
  // The string_view points into a stable std::string in Geometry::Impl::names —
  // safe as long as the entity's name is not reassigned (documented contract).
  return sv->data();
}

souxmar_status_t souxmar_geometry_bounding_box(const souxmar_geometry_t* g, double out_box[6]) {
  if (!g || !out_box)
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "NULL pointer");
  auto box = as_cpp(g)->bounding_box();
  for (std::size_t i = 0; i < 6; ++i)
    out_box[i] = box[i];
  return souxmar_status_ok();
}

}  // extern "C"
