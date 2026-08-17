// SPDX-License-Identifier: Apache-2.0
//
// am-layered-mesher — layer-aligned voxel mesher for additive-
// manufacturing process simulation.
//
// Registers `mesher.am.layered`. Every AM capability in the block
// (`solver.am.thermal.lpbf`, `solver.am.distortion.inherent_strain`,
// `solver.am.overhang`, ...) resolves a cell's layer index from its
// cell tag first and only falls back to centroid binning when the tag
// is absent. This mesher is the producer of that tag: it emits a
// structured Hex8 grid whose cell layers are perpendicular to the
// build direction, and stamps **cell tag = layer index** (0 at the
// build plate).
//
// What it computes:
//   A structured axis-aligned voxel grid over the extent box:
//       nx = ceil(Lx / target_size), ny = ceil(Ly / target_size),
//       nz = ceil(Lz / target_size)      (each at least 1)
//       dx = Lx / nx,  dy = Ly / ny,  dz = Lz / nz
//   i.e. the requested `target_size` is an *upper bound* on the voxel
//   edge and the grid fills the extent box exactly rather than
//   overshooting it. dz is therefore the simulation layer thickness
//   (<= target_size). Nothing downstream depends on that number being
//   exactly target_size, because the layer index travels in the cell
//   tag; the AM solvers only fall back to `layer_height` binning for
//   meshes that arrive untagged (contract 2.2).
//
//   Node ordering per cell is the standard VTK_HEXAHEDRON / Gmsh Hex8
//   ordering: bottom quad CCW seen from +Z (0,1,2,3), then the top quad
//   directly above it (4,5,6,7), with node i+4 stacked over node i.
//
// Boundary-face tags (contract 2.3):
//       10 = -X   11 = +X   12 = -Y   13 = +Y
//       14 = -Z (build-plate interface / downskin)
//       15 = +Z (top surface / upskin)
//   Interior faces are left SOUXMAR_FACE_UNTAGGED. The local-face
//   index -> side mapping is NOT guessed: it is the host's canonical
//   Hex8 table, `kHex8Faces` in src/core/face_topology.cpp:22-29
//   ("VTK_HEXAHEDRON / souxmar internal convention", CCW from outside
//   the cell), which orders the six faces as
//       0 = -z (bottom)  {0,3,2,1}
//       1 = +z (top)     {4,5,6,7}
//       2 = -y (front)   {0,1,5,4}
//       3 = +y (back)    {3,7,6,2}
//       4 = -x (left)    {0,4,7,3}
//       5 = +x (right)   {1,2,6,5}
//   That is the same ordering souxmar_mesh_face_tag() indexes, so a
//   cell on the i == 0 column tags local face 4 with 10, and so on.
//
// What this is NOT: a geometry-conforming mesher. It reads the input
// geometry's *bounding box* only — no faces, no solids, no trimming, no
// staircase-free surface. A part is therefore represented by its box
// envelope, which over-predicts mass and cross-sectional area. It also
// does not inherit geometry face tags (there is nothing to inherit from
// a bounding box); the 10..15 tags above are generated, not propagated.
// Support structures, powder-bed cells, recoater interaction and
// multi-part builds are out of scope. For a conforming AM mesh, mesh
// with `mesher.tetra.gmsh` (or any conforming mesher) and let the AM
// solvers fall back to centroid layer binning.
//
// Inputs — `souxmar_mesher_options_t` only. The v1 mesher ABI hands a
// mesher no value bag (`souxmar_mesher_mesh_fn(geometry, options,
// out_mesh, user_data)`; contract 0.4), so the tunables are exactly the
// four YAML keys the host maps onto that struct:
//   target_size   : voxel edge AND simulation layer thickness, metres.
//                   <= 0 (or absent) means 0.005 m. Clamped to
//                   [1e-5, 1.0] m.
//   element_order : 1 only. 2 (or higher) returns
//                   SOUXMAR_E_NOT_IMPLEMENTED — Hex20/Hex27 output is
//                   not implemented, and silently downgrading would
//                   lie about the mesh order.
//   optimize      : ignored. A structured voxel grid is already at the
//                   optimum of every quality metric souxmar computes
//                   (unit Jacobian, aspect ratio 1 for a cubic voxel),
//                   so there is nothing for a smoothing pass to do.
//   random_seed   : ignored. Generation is fully deterministic; there
//                   is no randomness to seed.
//
// Extent: the input geometry's bounding box when a geometry is supplied
// and the box is non-degenerate on all three axes; otherwise the demo
// build box [0, 0, 0] .. [0.06, 0.02, 0.02] m (a 60 x 20 x 20 mm
// coupon). The fallback exists because the mesher ABI has no value bag
// (contract 0.4) — there is literally no other channel through which a
// pipeline could hand this mesher an explicit box, so the demo default
// keeps `mesher.am.layered` runnable with no reader stage in front of
// it.
//
// Build direction is +Z, and is not configurable — same reason: no
// value bag. Every downstream AM capability defaults `build_direction`
// to [0, 0, 1], so the pair agrees out of the box.
//
// Hard cap: 200 000 cells. Above that the mesher refuses with
// SOUXMAR_E_INVALID_ARGUMENT rather than allocating gigabytes.
//
// Capability: `mesher.am.layered`. Declared `reentrant` — pure
// functional over its inputs, no shared state.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "souxmar-c/abi.h"
#include "souxmar-c/geometry.h"
#include "souxmar-c/mesh.h"
#include "souxmar-c/mesher.h"
#include "souxmar-c/plugin.h"
#include "souxmar-c/registry.h"
#include "souxmar-c/status.h"

namespace {

// Default voxel edge / layer thickness, metres. 5 mm is a part-scale
// process-simulation layer: LPBF machine layers are 30-60 um, and
// nobody simulates those one-by-one on a real part, so process
// simulation lumps ~100 machine layers into one simulation layer. The
// AM solvers keep the two apart (`layer_height` vs
// `process_layer_height`, contract 3.3).
constexpr double kDefaultVoxelEdge = 0.005;

// Voxel-edge clamp, metres. 1e-5 m = 10 um is the finest that means
// anything physically (~ one LPBF powder layer / one melt-pool depth);
// 1.0 m is larger than any machine build volume shipped today, so
// anything above it is a units mistake (mm entered as m, etc.).
constexpr double kMinVoxelEdge = 1e-5;
constexpr double kMaxVoxelEdge = 1.0;

// Hard cell cap (contract 3.1). 200 000 Hex8 cells is ~ 210 000 nodes:
// the largest mesh the always-on AM stack can push through a
// per-layer nodal field (num_nodes x num_layers doubles) without
// becoming the slowest thing in CI.
constexpr double kMaxCells = 200000.0;

// Demo build box, metres: a 60 x 20 x 20 mm coupon. See the header
// comment for why a mesher needs a hard-coded default extent at all.
constexpr double kDemoBox[6] = {0.0, 0.0, 0.0, 0.06, 0.02, 0.02};

// Outer-boundary face tags, contract 2.3. Values are part of the
// frozen contract — every AM/marine capability that discriminates
// build-plate faces from side walls reads these numbers.
constexpr std::int32_t kTagMinusX = 10;
constexpr std::int32_t kTagPlusX  = 11;
constexpr std::int32_t kTagMinusY = 12;
constexpr std::int32_t kTagPlusY  = 13;
constexpr std::int32_t kTagMinusZ = 14;  // build-plate interface / downskin
constexpr std::int32_t kTagPlusZ  = 15;  // top surface / upskin

// Local Hex8 face indices, from src/core/face_topology.cpp:22-29 (the
// host's canonical table; souxmar_mesh_set_face_tag indexes faces in
// exactly this order).
constexpr std::uint8_t kFaceMinusZ = 0;
constexpr std::uint8_t kFacePlusZ  = 1;
constexpr std::uint8_t kFaceMinusY = 2;
constexpr std::uint8_t kFacePlusY  = 3;
constexpr std::uint8_t kFaceMinusX = 4;
constexpr std::uint8_t kFacePlusX  = 5;

souxmar_status_t am_layered_mesh(const souxmar_geometry_t*       geometry,
                                 const souxmar_mesher_options_t* options,
                                 souxmar_mesh_t**                out_mesh,
                                 void*                           /*user_data*/) {
  if (!out_mesh) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "out_mesh is NULL");
  }
  if (options && options->element_order > 1) {
    return souxmar_status_error(SOUXMAR_E_NOT_IMPLEMENTED,
        "mesher.am.layered emits linear Hex8 only — set element_order: 1 "
        "(quadratic voxel output is not implemented)");
  }

  // Voxel edge = simulation layer thickness.
  double h = kDefaultVoxelEdge;
  if (options && options->target_size > 0.0) h = options->target_size;
  h = std::clamp(h, kMinVoxelEdge, kMaxVoxelEdge);

  // Extent: geometry bounding box when usable, demo box otherwise.
  double box[6] = {kDemoBox[0], kDemoBox[1], kDemoBox[2],
                   kDemoBox[3], kDemoBox[4], kDemoBox[5]};
  if (geometry) {
    double gbox[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    const souxmar_status_t bb = souxmar_geometry_bounding_box(geometry, gbox);
    // A failed / degenerate box is not an error: an empty Geometry
    // reads all-zero per souxmar-c/geometry.h, and falling back to the
    // demo box keeps the stage runnable. Documented in the header.
    if (bb.code == SOUXMAR_OK && gbox[3] > gbox[0] && gbox[4] > gbox[1] &&
        gbox[5] > gbox[2]) {
      for (std::size_t i = 0; i < 6; ++i) box[i] = gbox[i];
    }
  }

  const double lx = box[3] - box[0];
  const double ly = box[4] - box[1];
  const double lz = box[5] - box[2];
  if (!(lx > 0.0 && ly > 0.0 && lz > 0.0)) {
    return souxmar_status_error(SOUXMAR_E_INTERNAL,
        "mesher.am.layered: extent box is degenerate (should be unreachable — "
        "both the geometry box and the demo box are validated above)");
  }

  // Voxel counts. Computed in double first so an absurd
  // extent / target_size ratio trips the cap instead of overflowing.
  const double fx = std::max(1.0, std::ceil(lx / h));
  const double fy = std::max(1.0, std::ceil(ly / h));
  const double fz = std::max(1.0, std::ceil(lz / h));
  if (!(fx * fy * fz <= kMaxCells)) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT,
        "mesher.am.layered: requested voxel count exceeds the 200000-cell cap "
        "— raise target_size or mesh a smaller extent");
  }

  const std::size_t nx = static_cast<std::size_t>(fx);
  const std::size_t ny = static_cast<std::size_t>(fy);
  const std::size_t nz = static_cast<std::size_t>(fz);  // = number of layers
  const double      dx = lx / static_cast<double>(nx);
  const double      dy = ly / static_cast<double>(ny);
  const double      dz = lz / static_cast<double>(nz);  // layer thickness

  const std::size_t nnx = nx + 1;
  const std::size_t nny = ny + 1;
  const std::size_t nnz = nz + 1;

  souxmar_mesh_t* mesh = souxmar_mesh_new();
  if (!mesh) {
    return souxmar_status_error(SOUXMAR_E_OUT_OF_MEMORY, "souxmar_mesh_new");
  }
  souxmar_mesh_reserve_nodes(mesh, nnx * nny * nnz);
  souxmar_mesh_reserve_cells(mesh, nx * ny * nz);

  // Nodes: i fastest, then j, then k — so node id
  // (k*nny + j)*nnx + i. Iteration is in index order (determinism).
  for (std::size_t k = 0; k < nnz; ++k) {
    for (std::size_t j = 0; j < nny; ++j) {
      for (std::size_t i = 0; i < nnx; ++i) {
        const double p[3] = {box[0] + dx * static_cast<double>(i),
                            box[1] + dy * static_cast<double>(j),
                            box[2] + dz * static_cast<double>(k)};
        souxmar_mesh_add_node(mesh, p);
      }
    }
  }

  const auto node_at = [nnx, nny](std::size_t i, std::size_t j, std::size_t k) {
    return static_cast<std::uint64_t>((k * nny + j) * nnx + i);
  };

  // Cells: layer-major (k outermost) so cell ids run bottom-up in
  // build order — the order every AM solver walks layers in.
  for (std::size_t k = 0; k < nz; ++k) {
    for (std::size_t j = 0; j < ny; ++j) {
      for (std::size_t i = 0; i < nx; ++i) {
        // Standard VTK/Gmsh Hex8: bottom quad CCW from +Z, then top.
        const std::uint64_t nodes[8] = {
            node_at(i,     j,     k),
            node_at(i + 1, j,     k),
            node_at(i + 1, j + 1, k),
            node_at(i,     j + 1, k),
            node_at(i,     j,     k + 1),
            node_at(i + 1, j,     k + 1),
            node_at(i + 1, j + 1, k + 1),
            node_at(i,     j + 1, k + 1),
        };
        std::uint64_t cell = 0;
        // Cell tag = layer index, 0 at the build plate (contract 3.1).
        const souxmar_status_t cs = souxmar_mesh_add_cell(
            mesh, SOUXMAR_ET_HEX8, nodes, 8,
            /*tag=*/static_cast<std::int32_t>(k), &cell);
        if (cs.code != SOUXMAR_OK) {
          souxmar_mesh_free(mesh);
          return cs;
        }

        // Outer boundary faces only; interior faces stay untagged.
        struct { bool on_boundary; std::uint8_t local_face; std::int32_t tag; }
        sides[6] = {
            {i == 0,      kFaceMinusX, kTagMinusX},
            {i + 1 == nx, kFacePlusX,  kTagPlusX},
            {j == 0,      kFaceMinusY, kTagMinusY},
            {j + 1 == ny, kFacePlusY,  kTagPlusY},
            {k == 0,      kFaceMinusZ, kTagMinusZ},
            {k + 1 == nz, kFacePlusZ,  kTagPlusZ},
        };
        for (const auto& s : sides) {
          if (!s.on_boundary) continue;
          const souxmar_status_t fs =
              souxmar_mesh_set_face_tag(mesh, cell, s.local_face, s.tag);
          if (fs.code != SOUXMAR_OK) {
            souxmar_mesh_free(mesh);
            return fs;
          }
        }
      }
    }
  }

  *out_mesh = mesh;
  return souxmar_status_ok();
}

constexpr souxmar_mesher_vtable_t kVtable = {
    SOUXMAR_ABI_VERSION_MAJOR,
    &am_layered_mesh,
    nullptr,
};

}  // namespace

extern "C" SOUXMAR_PLUGIN_EXPORT
int souxmar_plugin_register_v1(souxmar_registry_t*        registry,
                               const souxmar_host_info_t* host) {
  if (!host || host->abi_version_major < SOUXMAR_ABI_VERSION_MAJOR) {
    return -1;
  }
  const souxmar_status_t s = souxmar_registry_add_mesher(
      registry, "mesher.am.layered", &kVtable, /*user_data=*/nullptr);
  return s.code == SOUXMAR_OK ? 0 : 1;
}
