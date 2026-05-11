// SPDX-License-Identifier: Apache-2.0
//
// grid-mesher — Sprint 6 push 5 reference tetrahedral mesher.
//
// Reads the input geometry's bounding box, lays an N×N×N structured
// grid of nodes inside it, and emits 5 tetrahedra per cube using the
// standard "5-tet hex" decomposition (the same one the bulk-mesh
// benchmark uses, see benchmarks/bench_mesh_construction.cpp).
//
// Why this exists: it's the second always-on mesher. Hello-mesher
// produces a trivial 1-tet placeholder; grid-mesher actually consumes
// a Geometry and exercises the swap-test contract — any pipeline
// referencing `mesher.tetra.grid` can substitute `mesher.tetra.gmsh`
// (Sprint 6 push 5 opt-in) without touching anything else. That's the
// whole point of the mesher.* namespace.
//
// What this is NOT: a real production mesher. It ignores the
// geometry's edges / faces / solids entirely — only the bounding box
// matters. Tag inheritance: every cell carries `-1` (untagged). A
// real CAD-aware mesher (gmsh-mesher, occt+netgen, ...) propagates
// face tags from the source geometry per the docs/PLUGIN_SDK.md
// contract.
//
// Capability: `mesher.tetra.grid`. Declared `reentrant` — pure
// functional on its inputs.
//
// Inputs (via the souxmar_mesher_options_t + the stage input bag):
//   - target_size (option): characteristic edge length. <=0 means
//     "use the value-bag `resolution` or default 4".
//   - resolution (value bag): override target_size with an explicit
//     N (number of nodes per axis). Default 4.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "souxmar-c/abi.h"
#include "souxmar-c/geometry.h"
#include "souxmar-c/mesh.h"
#include "souxmar-c/mesher.h"
#include "souxmar-c/plugin.h"
#include "souxmar-c/registry.h"
#include "souxmar-c/status.h"

namespace {

constexpr std::array<std::array<std::uint64_t, 4>, 5> kHexToTets = {{
    {0, 1, 2, 5}, {0, 2, 3, 7}, {0, 5, 6, 7},
    {2, 5, 6, 7}, {0, 2, 5, 7},
}};

souxmar_status_t grid_mesh(const souxmar_geometry_t*       geometry,
                           const souxmar_mesher_options_t* options,
                           souxmar_mesh_t**                out_mesh,
                           void*                           /*user_data*/) {
  if (!out_mesh) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "out_mesh is NULL");
  }
  if (!geometry) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT,
        "grid mesher requires a Geometry input — wire it through "
        "mesher.tetra.grid:input.geometry = {from: <reader_stage>}");
  }

  double bbox[6] = {0, 0, 0, 1, 1, 1};
  const auto bb_status = souxmar_geometry_bounding_box(geometry, bbox);
  if (bb_status.code != SOUXMAR_OK) return bb_status;
  // bbox = {xmin, ymin, zmin, xmax, ymax, zmax}
  const double lx = bbox[3] - bbox[0];
  const double ly = bbox[4] - bbox[1];
  const double lz = bbox[5] - bbox[2];
  if (!(lx > 0.0 && ly > 0.0 && lz > 0.0)) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT,
        "grid mesher: geometry bounding box is degenerate (zero span on one or more axes)");
  }

  // Resolution: options.target_size is the characteristic edge length
  // (the standard mesher.* knob). N is derived from the largest axis;
  // default is 4 nodes per axis when target_size is unspecified.
  int N = 4;
  if (options && options->target_size > 0.0) {
    const double L = std::max({lx, ly, lz});
    N = static_cast<int>(std::ceil(L / options->target_size)) + 1;
    if (N < 2) N = 2;
  }

  const std::size_t nN = static_cast<std::size_t>(N);
  const std::size_t num_nodes = nN * nN * nN;
  const std::size_t cells_per_axis = nN - 1;
  const std::size_t num_cells = cells_per_axis * cells_per_axis * cells_per_axis
                                * kHexToTets.size();

  souxmar_mesh_t* mesh = souxmar_mesh_new();
  if (!mesh) {
    return souxmar_status_error(SOUXMAR_E_OUT_OF_MEMORY, "souxmar_mesh_new");
  }
  souxmar_mesh_reserve_nodes(mesh, num_nodes);
  souxmar_mesh_reserve_cells(mesh, num_cells);

  const double dx = lx / static_cast<double>(cells_per_axis);
  const double dy = ly / static_cast<double>(cells_per_axis);
  const double dz = lz / static_cast<double>(cells_per_axis);
  for (std::size_t k = 0; k < nN; ++k) {
    for (std::size_t j = 0; j < nN; ++j) {
      for (std::size_t i = 0; i < nN; ++i) {
        const double p[3] = {
            bbox[0] + dx * static_cast<double>(i),
            bbox[1] + dy * static_cast<double>(j),
            bbox[2] + dz * static_cast<double>(k)};
        souxmar_mesh_add_node(mesh, p);
      }
    }
  }

  const auto node_at = [&](std::size_t i, std::size_t j, std::size_t k) {
    return static_cast<std::uint64_t>((k * nN + j) * nN + i);
  };
  const std::array<std::uint64_t, 8> corner_offsets = {
      0, 1,
      nN, nN + 1,
      nN * nN, nN * nN + 1,
      nN * nN + nN, nN * nN + nN + 1};
  for (std::size_t k = 0; k + 1 < nN; ++k) {
    for (std::size_t j = 0; j + 1 < nN; ++j) {
      for (std::size_t i = 0; i + 1 < nN; ++i) {
        const std::uint64_t base = node_at(i, j, k);
        std::array<std::uint64_t, 8> corners;
        for (std::size_t c = 0; c < 8; ++c) corners[c] = base + corner_offsets[c];
        for (const auto& tet : kHexToTets) {
          const std::uint64_t nodes[4] = {
              corners[tet[0]], corners[tet[1]],
              corners[tet[2]], corners[tet[3]]};
          souxmar_mesh_add_cell(mesh, SOUXMAR_ET_TET4, nodes, 4,
                                /*tag=*/-1, nullptr);
        }
      }
    }
  }

  *out_mesh = mesh;
  return souxmar_status_ok();
}

constexpr souxmar_mesher_vtable_t kVtable = {
    SOUXMAR_ABI_VERSION_MAJOR,
    &grid_mesh,
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
      registry, "mesher.tetra.grid", &kVtable, /*user_data=*/nullptr);
  return s.code == SOUXMAR_OK ? 0 : 1;
}
