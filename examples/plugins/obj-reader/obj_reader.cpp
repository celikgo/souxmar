// SPDX-License-Identifier: Apache-2.0
//
// obj-reader — Sprint 8 push 3 always-on reader plugin.
//
// Parses Wavefront OBJ files into a Tri3 surface mesh through the C
// ABI. Sibling to stl-reader (push 4 of Sprint 6) and to the opt-in
// blender-reader (this push) which exports .blend → .obj via a
// Blender subprocess and then hands the .obj here.
//
// Wavefront OBJ is line-oriented:
//
//   # comment
//   v  x y z [w]                      -- vertex position
//   vn nx ny nz                       -- vertex normal
//   vt u v [w]                        -- texture coord
//   f  v1 v2 v3 [v4 ...]              -- face, vertex-only
//   f  v1/vt1 v2/vt2 ...              -- face, vertex+uv
//   f  v1/vt1/vn1 v2/vt2/vn2 ...      -- face, vertex+uv+normal
//   f  v1//vn1 v2//vn2 ...            -- face, vertex+normal
//   o  <name>                         -- object marker (ignored)
//   g  <name>                         -- group marker (ignored)
//   s  <smoothing>                    -- smoothing group (ignored)
//   mtllib / usemtl                   -- material refs (ignored)
//
// Vertex indices are 1-based. Negative indices count back from the
// current end of the vertex list — both forms are accepted. Polygons
// larger than triangles are fan-triangulated into Tri3 cells from the
// first vertex.
//
// Capability: `reader.obj`. Declared `reentrant` — the parser keeps no
// global state.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "souxmar-c/abi.h"
#include "souxmar-c/mesh.h"
#include "souxmar-c/plugin.h"
#include "souxmar-c/reader.h"
#include "souxmar-c/registry.h"
#include "souxmar-c/status.h"
#include "souxmar-c/value.h"

namespace {

bool ieq(std::string_view a, std::string_view b) noexcept {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    const char ca = static_cast<char>(std::tolower(static_cast<unsigned char>(a[i])));
    const char cb = static_cast<char>(std::tolower(static_cast<unsigned char>(b[i])));
    if (ca != cb) return false;
  }
  return true;
}

// Parses the first token of an `f` field — the `v/vt/vn` triple-form.
// Returns the 1-based vertex index, or 0 on parse failure.
//
// OBJ allows: `v`, `v/vt`, `v/vt/vn`, `v//vn`. Only the leading vertex
// index participates in topology; vt / vn are ignored.
std::int64_t parse_face_field(std::string_view tok) noexcept {
  // Find the first '/' or end.
  std::size_t slash = tok.find('/');
  std::string_view head = (slash == std::string_view::npos) ? tok : tok.substr(0, slash);
  if (head.empty()) return 0;
  // std::from_chars on a string_view is the clean path, but the
  // standard library variant has spotty <charconv> for double; use
  // strtoll on a NUL-terminated copy (face fields are < 32 bytes).
  char buf[64];
  std::size_t n = head.size() < sizeof(buf) - 1 ? head.size() : sizeof(buf) - 1;
  std::memcpy(buf, head.data(), n);
  buf[n] = '\0';
  char* end = nullptr;
  const long long v = std::strtoll(buf, &end, 10);
  if (end == buf) return 0;
  return static_cast<std::int64_t>(v);
}

// Resolve a 1-based or negative OBJ vertex index into a 0-based mesh
// node id. Returns -1 on out-of-range.
std::int64_t resolve_index(std::int64_t obj_idx, std::size_t num_verts) noexcept {
  if (obj_idx > 0) {
    const std::int64_t zero_based = obj_idx - 1;
    if (zero_based >= static_cast<std::int64_t>(num_verts)) return -1;
    return zero_based;
  }
  if (obj_idx < 0) {
    // -1 == last vertex. Resolve relative to current vertex count.
    const std::int64_t zero_based =
        static_cast<std::int64_t>(num_verts) + obj_idx;
    if (zero_based < 0) return -1;
    return zero_based;
  }
  return -1;  // 0 is not a valid OBJ index
}

souxmar_status_t parse_obj(std::istream&                in,
                           std::vector<double>&         flat_coords,
                           std::vector<std::uint64_t>&  tri_nodes) {
  std::string line;
  std::size_t line_no    = 0;
  std::size_t num_faces  = 0;
  std::vector<std::int64_t> face_indices;
  face_indices.reserve(8);

  while (std::getline(in, line)) {
    ++line_no;
    // Strip trailing '\r' for CRLF inputs.
    if (!line.empty() && line.back() == '\r') line.pop_back();
    // Trim leading whitespace.
    std::size_t s = 0;
    while (s < line.size() && (line[s] == ' ' || line[s] == '\t')) ++s;
    if (s >= line.size())   continue;
    if (line[s] == '#')     continue;

    std::istringstream iss(line.substr(s));
    std::string head;
    if (!(iss >> head)) continue;

    if (head == "v") {
      double x = 0, y = 0, z = 0;
      if (!(iss >> x >> y >> z)) {
        return souxmar_status_error(SOUXMAR_E_IO,
            "vertex line missing x y z");
      }
      // OBJ allows a trailing w; we ignore it.
      flat_coords.push_back(x);
      flat_coords.push_back(y);
      flat_coords.push_back(z);
      continue;
    }
    if (head == "vn" || head == "vt" || head == "vp") {
      // Normals / texcoords / parametric verts — ignored.
      continue;
    }
    if (head == "f") {
      face_indices.clear();
      std::string tok;
      while (iss >> tok) {
        const std::int64_t v_obj = parse_face_field(tok);
        if (v_obj == 0) {
          return souxmar_status_error(SOUXMAR_E_IO,
              "face line has malformed vertex reference");
        }
        const std::int64_t zero_based = resolve_index(
            v_obj, flat_coords.size() / 3);
        if (zero_based < 0) {
          static thread_local std::string msg;
          msg = "face references vertex " + std::to_string(v_obj) +
                " before it is defined";
          return souxmar_status_error(SOUXMAR_E_IO, msg.c_str());
        }
        face_indices.push_back(zero_based);
      }
      if (face_indices.size() < 3) {
        return souxmar_status_error(SOUXMAR_E_IO,
            "face has fewer than 3 vertices");
      }
      // Fan-triangulate around face_indices[0].
      for (std::size_t i = 1; i + 1 < face_indices.size(); ++i) {
        tri_nodes.push_back(static_cast<std::uint64_t>(face_indices[0]));
        tri_nodes.push_back(static_cast<std::uint64_t>(face_indices[i]));
        tri_nodes.push_back(static_cast<std::uint64_t>(face_indices[i + 1]));
      }
      ++num_faces;
      continue;
    }
    if (head == "o" || head == "g" || head == "s" ||
        ieq(head, "mtllib") || ieq(head, "usemtl")) {
      continue;
    }
    // Unknown keyword — silently ignore (OBJ has many vendor-specific
    // extensions; the parser stays permissive). State machine is implicit
    // (vertex list grows monotonically; faces resolve at point of use).
  }

  if (flat_coords.empty()) {
    return souxmar_status_error(SOUXMAR_E_IO,
        "OBJ file contained no `v` (vertex) lines");
  }
  if (num_faces == 0) {
    return souxmar_status_error(SOUXMAR_E_IO,
        "OBJ file contained no `f` (face) lines");
  }
  if (tri_nodes.empty()) {
    // Faces existed but every one was degenerate — defensive: above
    // we already rejected <3-vertex faces, so this shouldn't fire.
    return souxmar_status_error(SOUXMAR_E_IO,
        "OBJ triangulation produced zero cells");
  }
  return souxmar_status_ok();
}

souxmar_status_t obj_read(const char*                       path,
                          const souxmar_value_t*            /*inputs*/,
                          const souxmar_reader_options_t*   /*options*/,
                          souxmar_mesh_t**                  out_mesh,
                          souxmar_geometry_t**              out_geometry,
                          void*                             /*user_data*/) {
  if (!path) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "path is NULL");
  }
  if (!out_mesh || !out_geometry) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT,
        "out_mesh / out_geometry are NULL");
  }
  *out_mesh     = nullptr;
  *out_geometry = nullptr;

  std::ifstream in(path);
  if (!in.is_open()) {
    static thread_local std::string msg;
    msg = std::string("cannot open '") + path + "'";
    return souxmar_status_error(SOUXMAR_E_NOT_FOUND, msg.c_str());
  }

  std::vector<double>        flat_coords;   // 3*N doubles
  std::vector<std::uint64_t> tri_nodes;     // 3*M ids

  const auto parse_status = parse_obj(in, flat_coords, tri_nodes);
  if (parse_status.code != SOUXMAR_OK) return parse_status;

  const std::size_t num_nodes = flat_coords.size() / 3;
  const std::size_t num_tris  = tri_nodes.size()   / 3;

  souxmar_mesh_t* mesh = souxmar_mesh_new();
  if (!mesh) {
    return souxmar_status_error(SOUXMAR_E_OUT_OF_MEMORY, "souxmar_mesh_new");
  }
  souxmar_mesh_reserve_nodes(mesh, num_nodes);
  souxmar_mesh_reserve_cells(mesh, num_tris);

  for (std::size_t i = 0; i < num_nodes; ++i) {
    const double p[3] = {
        flat_coords[i * 3 + 0],
        flat_coords[i * 3 + 1],
        flat_coords[i * 3 + 2]};
    souxmar_mesh_add_node(mesh, p);
  }
  for (std::size_t i = 0; i < num_tris; ++i) {
    const std::uint64_t nodes[3] = {
        tri_nodes[i * 3 + 0],
        tri_nodes[i * 3 + 1],
        tri_nodes[i * 3 + 2]};
    souxmar_mesh_add_cell(mesh, SOUXMAR_ET_TRI3, nodes, 3, -1, nullptr);
  }

  *out_mesh = mesh;
  return souxmar_status_ok();
}

constexpr souxmar_reader_vtable_t kVtable = {
    SOUXMAR_ABI_VERSION_MAJOR,
    &obj_read,
    nullptr,
};

}  // namespace

extern "C" SOUXMAR_PLUGIN_EXPORT
int souxmar_plugin_register_v1(souxmar_registry_t*        registry,
                               const souxmar_host_info_t* host) {
  if (!host || host->abi_version_major < SOUXMAR_ABI_VERSION_MAJOR) {
    return -1;
  }
  const souxmar_status_t s = souxmar_registry_add_reader(
      registry, "reader.obj", &kVtable, /*user_data=*/nullptr);
  return s.code == SOUXMAR_OK ? 0 : 1;
}
