// SPDX-License-Identifier: Apache-2.0
//
// stl-reader — Sprint 6 push 4 reference reader plugin.
//
// Parses ASCII STL files into a Tri3 mesh through the C ABI. The
// always-on counterpart to the opt-in occt-reader (which handles
// STEP / IGES via OpenCASCADE).
//
// The STL format is brutally simple:
//
//   solid <name>
//     facet normal nx ny nz
//       outer loop
//         vertex x y z
//         vertex x y z
//         vertex x y z
//       endloop
//     endfacet
//     ...
//   endsolid <name>
//
// Each `facet` becomes one Tri3 cell. We deduplicate coincident
// vertices within a tolerance so adjacent facets share nodes (without
// this every cell carries 3 fresh nodes, blowing up the file by 3×
// and breaking topological adjacency).
//
// Capability: `reader.stl`. Declared `reentrant` — the parser is
// pure functional over its input.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "souxmar-c/abi.h"
#include "souxmar-c/mesh.h"
#include "souxmar-c/plugin.h"
#include "souxmar-c/reader.h"
#include "souxmar-c/registry.h"
#include "souxmar-c/status.h"
#include "souxmar-c/value.h"

namespace {

// Vertex deduplication tolerance. ASCII STL writes vertex positions
// with ~6 decimals of precision; quantising to 1e-7 catches identical
// vertices written by every common authoring tool without false-
// merging deliberately-distinct nearby vertices.
constexpr double kDedupQuantum = 1e-7;

struct Vec3Key {
  std::int64_t qx, qy, qz;
  bool operator==(const Vec3Key& o) const noexcept {
    return qx == o.qx && qy == o.qy && qz == o.qz;
  }
};
struct Vec3KeyHash {
  std::size_t operator()(const Vec3Key& k) const noexcept {
    std::size_t h = std::hash<std::int64_t>{}(k.qx);
    h ^= std::hash<std::int64_t>{}(k.qy) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    h ^= std::hash<std::int64_t>{}(k.qz) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    return h;
  }
};

Vec3Key quantize(double x, double y, double z) noexcept {
  return {static_cast<std::int64_t>(std::llround(x / kDedupQuantum)),
          static_cast<std::int64_t>(std::llround(y / kDedupQuantum)),
          static_cast<std::int64_t>(std::llround(z / kDedupQuantum))};
}

bool ieq(std::string_view a, std::string_view b) noexcept {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    const char ca = static_cast<char>(std::tolower(static_cast<unsigned char>(a[i])));
    const char cb = static_cast<char>(std::tolower(static_cast<unsigned char>(b[i])));
    if (ca != cb) return false;
  }
  return true;
}

// Returns true if the first non-whitespace token of `line` matches
// `keyword` (case-insensitive).
bool starts_with_keyword(const std::string& line, std::string_view keyword) {
  std::istringstream iss(line);
  std::string token;
  if (!(iss >> token)) return false;
  return ieq(token, keyword);
}

souxmar_status_t parse_ascii_stl(std::istream&             in,
                                 std::vector<double>&      flat_coords,
                                 std::vector<std::uint64_t>& tri_nodes) {
  std::unordered_map<Vec3Key, std::uint64_t, Vec3KeyHash> by_key;
  std::string line;
  std::size_t line_no  = 0;
  bool        saw_solid = false;
  bool        in_facet = false, in_loop = false;
  std::vector<std::uint64_t> current_tri;
  current_tri.reserve(3);

  while (std::getline(in, line)) {
    ++line_no;
    if (line.empty()) continue;
    std::istringstream iss(line);
    std::string head;
    if (!(iss >> head)) continue;
    if (ieq(head, "solid")) {
      if (saw_solid) {
        // Multi-solid STL — second `solid` line. We treat it as a
        // sentinel and keep parsing (concatenating into one mesh).
        continue;
      }
      saw_solid = true;
      continue;
    }
    if (ieq(head, "endsolid")) {
      // Tolerate trailing garbage after endsolid.
      continue;
    }
    if (ieq(head, "facet")) {
      if (in_facet) {
        return souxmar_status_error(SOUXMAR_E_IO,
            "nested 'facet' (missing endfacet)");
      }
      in_facet = true;
      current_tri.clear();
      continue;
    }
    if (ieq(head, "endfacet")) {
      if (!in_facet) {
        return souxmar_status_error(SOUXMAR_E_IO,
            "'endfacet' without preceding 'facet'");
      }
      if (current_tri.size() != 3) {
        return souxmar_status_error(SOUXMAR_E_IO,
            "STL facet did not carry exactly 3 vertices");
      }
      tri_nodes.push_back(current_tri[0]);
      tri_nodes.push_back(current_tri[1]);
      tri_nodes.push_back(current_tri[2]);
      in_facet = false;
      continue;
    }
    if (ieq(head, "outer")) {
      // "outer loop"
      std::string maybe_loop;
      iss >> maybe_loop;
      if (!ieq(maybe_loop, "loop")) {
        return souxmar_status_error(SOUXMAR_E_IO, "expected 'outer loop'");
      }
      in_loop = true;
      continue;
    }
    if (ieq(head, "endloop")) {
      in_loop = false;
      continue;
    }
    if (ieq(head, "vertex")) {
      if (!in_facet || !in_loop) {
        return souxmar_status_error(SOUXMAR_E_IO,
            "'vertex' outside an 'outer loop' block");
      }
      double x = 0, y = 0, z = 0;
      if (!(iss >> x >> y >> z)) {
        return souxmar_status_error(SOUXMAR_E_IO,
            "vertex line missing x y z");
      }
      const Vec3Key key = quantize(x, y, z);
      auto it = by_key.find(key);
      std::uint64_t node_id;
      if (it == by_key.end()) {
        node_id = static_cast<std::uint64_t>(flat_coords.size() / 3);
        flat_coords.push_back(x);
        flat_coords.push_back(y);
        flat_coords.push_back(z);
        by_key.emplace(key, node_id);
      } else {
        node_id = it->second;
      }
      current_tri.push_back(node_id);
      continue;
    }
    // Unknown keyword — silently ignore (tolerates `color`, `attribute`
    // lines from non-standard writers). The parser keeps state machine
    // intact via the explicit facet/loop tracking above.
  }

  if (!saw_solid) {
    return souxmar_status_error(SOUXMAR_E_IO,
        "no 'solid' header — not an ASCII STL file");
  }
  if (in_facet) {
    return souxmar_status_error(SOUXMAR_E_IO,
        "stream ended inside a facet (missing endfacet)");
  }
  if (tri_nodes.empty()) {
    return souxmar_status_error(SOUXMAR_E_IO,
        "STL produced zero facets");
  }
  return souxmar_status_ok();
}

souxmar_status_t stl_read(const char*                       path,
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

  std::vector<double>       flat_coords;   // 3*N doubles
  std::vector<std::uint64_t> tri_nodes;    // 3*M ids

  const auto parse_status = parse_ascii_stl(in, flat_coords, tri_nodes);
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
    &stl_read,
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
      registry, "reader.stl", &kVtable, /*user_data=*/nullptr);
  return s.code == SOUXMAR_OK ? 0 : 1;
}
