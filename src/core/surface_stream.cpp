// SPDX-License-Identifier: Apache-2.0
//
// SurfaceStream — outer-shell extraction + triangulation + normals.
//
// Sprint 25 PR 1 (real implementation; replaces the stub landed in
// the post-v1.0 ABI scaffold commit).
//
// Algorithm:
//   1. Walk every cell. For each 3D linear cell (Tet4 / Hex8 / Prism6
//      / Pyramid5), enumerate its faces via the canonical local face
//      tables and key each face by (vertex count, sorted vertex IDs)
//      in an unordered_map.
//   2. For each 2D linear cell (Tri3 / Quad4), the cell itself *is*
//      the surface — emit directly without the dedup step.
//   3. Faces that landed in the map exactly once are boundary faces;
//      faces seen twice are interior. Collect the boundary set.
//   4. Build a compact vertex set: only mesh nodes that participate
//      in at least one boundary face. Record vertex_ids back to the
//      original mesh NodeIndex.
//   5. Triangulate boundary quads into two triangles each.
//   6. Compute per-triangle normals (unnormalized cross product;
//      magnitude is twice the triangle area and doubles as the
//      area weight). Accumulate onto each of the triangle's three
//      vertices, then normalize.
//   7. Pack everything into SoA buffers and stash on Impl.
//
// Face tables are duplicated from examples/plugins/openfoam-solver
// for now — ADR-0012's pre-mortem flagged centralization as a future
// follow-up; doing it here would expand the diff with unrelated
// refactoring.
//
// Quadratic elements (Tet10, Hex20, Hex27, Prism15, Pyramid13) are
// skipped silently — same posture as openfoam-solver. Mid-edge nodes
// are not reflected; a future minor can lower to the linear corner
// set if the user demand surfaces.

#include "souxmar/core/surface_stream.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <unordered_map>
#include <vector>

#include "souxmar/core/element_type.h"
#include "souxmar/core/mesh.h"
#include "souxmar/core/tag.h"

namespace souxmar::core {

namespace {

// ---------- Per-element-type local face tables ----------

struct LocalFace {
  std::uint8_t                  vertex_count;    // 3 (triangle) or 4 (quad)
  std::array<std::uint8_t, 4>   cell_local_idx;  // 4th slot unused for tri faces
};

// Tet4 — 4 triangular faces (opposite-vertex convention).
constexpr LocalFace kTet4Faces[4] = {
    {3, {{1, 2, 3, 0}}},   // opposite v0
    {3, {{0, 3, 2, 0}}},   // opposite v1
    {3, {{0, 1, 3, 0}}},   // opposite v2
    {3, {{0, 2, 1, 0}}},   // opposite v3
};

// Hex8 — 6 quadrilateral faces. Vertex ordering matches the VTK_HEXAHEDRON
// convention souxmar uses internally; faces are CCW from outside the cell.
constexpr LocalFace kHex8Faces[6] = {
    {4, {{0, 3, 2, 1}}},   // -z (bottom)
    {4, {{4, 5, 6, 7}}},   // +z (top)
    {4, {{0, 1, 5, 4}}},   // -y (front)
    {4, {{3, 7, 6, 2}}},   // +y (back)
    {4, {{0, 4, 7, 3}}},   // -x (left)
    {4, {{1, 2, 6, 5}}},   // +x (right)
};

// Prism6 — 2 triangular caps + 3 quadrilateral sides.
constexpr LocalFace kPrism6Faces[5] = {
    {3, {{0, 2, 1, 0}}},   // -z (bottom triangle)
    {3, {{3, 4, 5, 0}}},   // +z (top triangle)
    {4, {{0, 1, 4, 3}}},   // side 0-1
    {4, {{1, 2, 5, 4}}},   // side 1-2
    {4, {{2, 0, 3, 5}}},   // side 2-0
};

// Pyramid5 — 1 quadrilateral base + 4 triangular sides meeting at the apex.
constexpr LocalFace kPyramid5Faces[5] = {
    {4, {{0, 3, 2, 1}}},   // -z (base quad)
    {3, {{0, 1, 4, 0}}},   // side 0-1
    {3, {{1, 2, 4, 0}}},   // side 1-2
    {3, {{2, 3, 4, 0}}},   // side 2-3
    {3, {{3, 0, 4, 0}}},   // side 3-0
};

struct FaceTable {
  const LocalFace* faces;
  std::size_t      count;
};

FaceTable face_table_for(ElementType et) {
  switch (et) {
    case ElementType::Tet4:     return {kTet4Faces,     4};
    case ElementType::Hex8:     return {kHex8Faces,     6};
    case ElementType::Prism6:   return {kPrism6Faces,   5};
    case ElementType::Pyramid5: return {kPyramid5Faces, 5};
    default:                    return {nullptr, 0};
  }
}

// ---------- Canonical face key for dedup ----------

struct FaceKey {
  std::uint8_t                 size{};
  std::array<std::uint64_t, 4> v{{0, 0, 0, 0}};

  bool operator==(const FaceKey& o) const noexcept {
    return size == o.size &&
           v[0] == o.v[0] && v[1] == o.v[1] &&
           v[2] == o.v[2] && v[3] == o.v[3];
  }
};

struct FaceKeyHash {
  std::size_t operator()(const FaceKey& k) const noexcept {
    std::size_t h = std::hash<std::uint8_t>{}(k.size);
    for (std::uint8_t i = 0; i < k.size; ++i) {
      h ^= std::hash<std::uint64_t>{}(k.v[i]) +
           0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    }
    return h;
  }
};

struct BoundaryFace {
  std::uint8_t                 vertex_count = 0;
  std::array<std::uint64_t, 4> verts{{0, 0, 0, 0}};  // outward-CCW order
};

}  // namespace

// ---------- Impl ----------

class SurfaceStream::Impl {
 public:
  std::vector<float>         positions;
  std::vector<float>         normals;
  std::vector<std::uint32_t> indices;
  std::vector<std::uint32_t> face_ids;
  std::vector<std::uint64_t> vertex_ids;
  std::array<double, 3>      bounds_min{{0.0, 0.0, 0.0}};
  std::array<double, 3>      bounds_max{{0.0, 0.0, 0.0}};

  explicit Impl(const Mesh& mesh) {
    build(mesh);
  }

 private:
  void build(const Mesh& mesh) {
    std::vector<BoundaryFace> boundary;
    extract_boundary_faces(mesh, boundary);

    if (boundary.empty()) {
      return;  // bounds default to zeros; everything else stays empty
    }

    // Build compact vertex set: only mesh nodes referenced by boundary faces.
    std::unordered_map<std::uint64_t, std::uint32_t> node_to_compact;
    node_to_compact.reserve(boundary.size() * 3);  // ~upper bound for tris

    for (const auto& bf : boundary) {
      for (std::uint8_t i = 0; i < bf.vertex_count; ++i) {
        const std::uint64_t node = bf.verts[i];
        auto [it, inserted] = node_to_compact.try_emplace(
            node, static_cast<std::uint32_t>(vertex_ids.size()));
        if (inserted) {
          vertex_ids.push_back(node);
        }
      }
    }

    // Positions buffer + bounds.
    positions.resize(vertex_ids.size() * 3);
    bounds_min = {{ std::numeric_limits<double>::infinity(),
                    std::numeric_limits<double>::infinity(),
                    std::numeric_limits<double>::infinity() }};
    bounds_max = {{ -std::numeric_limits<double>::infinity(),
                    -std::numeric_limits<double>::infinity(),
                    -std::numeric_limits<double>::infinity() }};

    for (std::size_t i = 0; i < vertex_ids.size(); ++i) {
      const auto p = mesh.node(NodeIndex{vertex_ids[i]});
      positions[3 * i + 0] = static_cast<float>(p[0]);
      positions[3 * i + 1] = static_cast<float>(p[1]);
      positions[3 * i + 2] = static_cast<float>(p[2]);
      for (int d = 0; d < 3; ++d) {
        if (p[d] < bounds_min[d]) bounds_min[d] = p[d];
        if (p[d] > bounds_max[d]) bounds_max[d] = p[d];
      }
    }

    // Triangulate boundary faces. face_ids are 1-based so the renderer can
    // use 0 as a "no hit" sentinel without colliding with a real face.
    normals.assign(vertex_ids.size() * 3, 0.0f);

    for (std::size_t face_idx = 0; face_idx < boundary.size(); ++face_idx) {
      const auto& bf = boundary[face_idx];
      std::array<std::uint32_t, 4> ci{{0, 0, 0, 0}};
      for (std::uint8_t i = 0; i < bf.vertex_count; ++i) {
        ci[i] = node_to_compact.at(bf.verts[i]);
      }

      const auto fid = static_cast<std::uint32_t>(face_idx + 1);
      if (bf.vertex_count == 3) {
        emit_triangle(ci[0], ci[1], ci[2], fid);
      } else if (bf.vertex_count == 4) {
        // Split along the (0,2) diagonal — same convention OpenFOAM uses
        // for quad polyMesh faces when a downstream tool needs tris.
        emit_triangle(ci[0], ci[1], ci[2], fid);
        emit_triangle(ci[0], ci[2], ci[3], fid);
      }
    }

    // Normalize per-vertex normals.
    for (std::size_t i = 0; i < vertex_ids.size(); ++i) {
      const float nx = normals[3 * i + 0];
      const float ny = normals[3 * i + 1];
      const float nz = normals[3 * i + 2];
      const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
      if (len > 0.0f) {
        normals[3 * i + 0] = nx / len;
        normals[3 * i + 1] = ny / len;
        normals[3 * i + 2] = nz / len;
      }
    }
  }

  void emit_triangle(std::uint32_t a, std::uint32_t b, std::uint32_t c,
                     std::uint32_t face_id) {
    indices.push_back(a);
    indices.push_back(b);
    indices.push_back(c);
    face_ids.push_back(face_id);

    // Unnormalized cross product = 2 * triangle_area * unit_normal — the
    // magnitude doubles as an area weight when we accumulate onto each
    // vertex, so larger faces contribute proportionally more to the
    // shared-vertex normal direction.
    const float ax = positions[3 * a + 0];
    const float ay = positions[3 * a + 1];
    const float az = positions[3 * a + 2];
    const float ux = positions[3 * b + 0] - ax;
    const float uy = positions[3 * b + 1] - ay;
    const float uz = positions[3 * b + 2] - az;
    const float vx = positions[3 * c + 0] - ax;
    const float vy = positions[3 * c + 1] - ay;
    const float vz = positions[3 * c + 2] - az;
    const float nx = uy * vz - uz * vy;
    const float ny = uz * vx - ux * vz;
    const float nz = ux * vy - uy * vx;

    for (const std::uint32_t v : {a, b, c}) {
      normals[3 * v + 0] += nx;
      normals[3 * v + 1] += ny;
      normals[3 * v + 2] += nz;
    }
  }

  static void extract_boundary_faces(const Mesh& mesh,
                                     std::vector<BoundaryFace>& out) {
    struct FaceEntry {
      std::array<std::uint64_t, 4> verts_owner{{0, 0, 0, 0}};
      std::uint8_t                 vertex_count = 0;
      std::int64_t                 neighbour    = -1;
    };

    std::unordered_map<FaceKey, FaceEntry, FaceKeyHash> face_map;
    const std::size_t n_cells = mesh.num_cells();
    face_map.reserve(n_cells * 3);  // ~ faces/cell × sharing factor

    // 2D linear cells are emitted directly. Reserve some headroom.
    out.reserve(n_cells / 4);  // rough lower-bound guess

    for (std::size_t c = 0; c < n_cells; ++c) {
      const ElementType et = mesh.cell_type(CellIndex{c});

      // 2D linear: the cell is the surface.
      if (et == ElementType::Tri3) {
        const auto nodes = mesh.cell_nodes(CellIndex{c});
        if (nodes.size() >= 3) {
          BoundaryFace bf;
          bf.vertex_count = 3;
          bf.verts[0] = nodes[0].value;
          bf.verts[1] = nodes[1].value;
          bf.verts[2] = nodes[2].value;
          out.push_back(bf);
        }
        continue;
      }
      if (et == ElementType::Quad4) {
        const auto nodes = mesh.cell_nodes(CellIndex{c});
        if (nodes.size() >= 4) {
          BoundaryFace bf;
          bf.vertex_count = 4;
          for (std::uint8_t i = 0; i < 4; ++i) bf.verts[i] = nodes[i].value;
          out.push_back(bf);
        }
        continue;
      }

      // 3D linear: walk the face table.
      const FaceTable ft = face_table_for(et);
      if (ft.faces == nullptr) {
        continue;  // skip quadratic / unsupported types
      }

      const auto cell_nodes = mesh.cell_nodes(CellIndex{c});
      for (std::size_t f = 0; f < ft.count; ++f) {
        const LocalFace& lf = ft.faces[f];

        std::array<std::uint64_t, 4> face_verts{{0, 0, 0, 0}};
        for (std::uint8_t i = 0; i < lf.vertex_count; ++i) {
          face_verts[i] = cell_nodes[lf.cell_local_idx[i]].value;
        }

        FaceKey key;
        key.size = lf.vertex_count;
        key.v    = face_verts;
        std::sort(key.v.begin(), key.v.begin() + key.size);

        auto it = face_map.find(key);
        if (it == face_map.end()) {
          FaceEntry e;
          e.verts_owner  = face_verts;
          e.vertex_count = lf.vertex_count;
          face_map.emplace(key, e);
        } else {
          it->second.neighbour = static_cast<std::int64_t>(c);
        }
      }
    }

    // Collect boundary faces (no neighbour).
    for (const auto& [_, fe] : face_map) {
      if (fe.neighbour < 0) {
        BoundaryFace bf;
        bf.vertex_count = fe.vertex_count;
        bf.verts        = fe.verts_owner;
        out.push_back(bf);
      }
    }
  }
};

// ---------- SurfaceStream public API ----------

SurfaceStream::SurfaceStream(const Mesh& mesh)
    : impl_(std::make_unique<Impl>(mesh)) {}

SurfaceStream::~SurfaceStream() = default;
SurfaceStream::SurfaceStream(SurfaceStream&&) noexcept = default;
SurfaceStream& SurfaceStream::operator=(SurfaceStream&&) noexcept = default;

std::size_t SurfaceStream::vertex_count() const noexcept {
  return impl_->vertex_ids.size();
}

std::size_t SurfaceStream::triangle_count() const noexcept {
  return impl_->face_ids.size();
}

std::array<double, 3> SurfaceStream::bounds_min() const noexcept {
  return impl_->bounds_min;
}

std::array<double, 3> SurfaceStream::bounds_max() const noexcept {
  return impl_->bounds_max;
}

std::span<const float> SurfaceStream::positions() const noexcept {
  return impl_->positions;
}

std::span<const float> SurfaceStream::normals() const noexcept {
  return impl_->normals;
}

std::span<const std::uint32_t> SurfaceStream::indices() const noexcept {
  return impl_->indices;
}

std::span<const std::uint32_t> SurfaceStream::face_ids() const noexcept {
  return impl_->face_ids;
}

std::span<const std::uint64_t> SurfaceStream::vertex_ids() const noexcept {
  return impl_->vertex_ids;
}

}  // namespace souxmar::core
