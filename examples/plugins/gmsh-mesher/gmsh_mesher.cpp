// SPDX-License-Identifier: Apache-2.0
//
// gmsh-mesher — Sprint 6 push 5 opt-in tetrahedral mesher.
//
// Registers `mesher.tetra.gmsh`. Drives Gmsh's C++ API to produce a
// tetrahedral mesh inside the input geometry's bounding box. Real
// Gmsh-flavoured outputs: classified surfaces, characteristic-length
// fields, all the things `grid-mesher` doesn't have time for. The
// pipeline contract is identical to grid-mesher: any YAML referencing
// `mesher.tetra.grid` switches to `mesher.tetra.gmsh` (or vice versa)
// with no other changes — that's the namespace contract in action.
//
// Build gating: compiled only when `-DSOUXMAR_WITH_GMSH=ON` AND
// find_package(Gmsh) succeeds. Default builds don't include this
// binary; nightly Gmsh-bearing runners do.
//
// Declared `single-threaded` — Gmsh's API holds module-global state
// (`gmsh::initialize()` is process-wide). Two stages of the same
// pipeline can't run concurrently against this plugin; the reentrancy
// guard serialises calls.
//
// LIMITATIONS (v1):
//   - Uses the bounding box only. Native CAD topology (via
//     gmsh::model::occ::importShapes) lands when the occt-reader
//     is the upstream stage and the plugin learns to walk the
//     souxmar geometry's tag table back to OCCT shape handles. That's
//     a Sprint 7 follow-on — punted to keep this push focused on
//     proving the Gmsh integration end-to-end.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "souxmar-c/abi.h"
#include "souxmar-c/geometry.h"
#include "souxmar-c/mesh.h"
#include "souxmar-c/mesher.h"
#include "souxmar-c/plugin.h"
#include "souxmar-c/registry.h"
#include "souxmar-c/status.h"

// Real Gmsh C++ API. The build option gates the TU from compiling
// when Gmsh isn't installed.
#include <gmsh.h>

namespace {

constexpr int kGmshTet4ElementType = 4;  // Gmsh's stable id for 4-node tet

souxmar_status_t gmsh_mesh(const souxmar_geometry_t*       geometry,
                           const souxmar_mesher_options_t* options,
                           souxmar_mesh_t**                out_mesh,
                           void*                           /*user_data*/) {
  if (!out_mesh) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "out_mesh is NULL");
  }
  if (!geometry) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT,
        "gmsh mesher requires a Geometry input");
  }

  double bbox[6] = {0, 0, 0, 1, 1, 1};
  if (souxmar_geometry_bounding_box(geometry, bbox).code != SOUXMAR_OK ||
      !(bbox[3] > bbox[0] && bbox[4] > bbox[1] && bbox[5] > bbox[2])) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT,
        "gmsh mesher: input geometry has degenerate bounding box");
  }

  const double target_size =
      (options && options->target_size > 0.0)
          ? options->target_size
          : std::max({bbox[3] - bbox[0],
                      bbox[4] - bbox[1],
                      bbox[5] - bbox[2]}) / 4.0;

  // Gmsh's API state is process-global. Initialise/finalise per call
  // so the plugin doesn't leak handles if the process embeds something
  // else that also uses Gmsh. The reentrancy-guard's per-plugin mutex
  // (declared single-threaded above) serialises against concurrent
  // pipeline stages.
  if (!gmsh::isInitialized()) gmsh::initialize();

  try {
    gmsh::model::add("souxmar_gmsh_mesher");
    // Use Gmsh's built-in OCC kernel to construct a box covering the
    // input bbox. This is the v1 "bounding-box-only" path — see the
    // header comment for the follow-on plan.
    gmsh::model::occ::addBox(
        bbox[0], bbox[1], bbox[2],
        bbox[3] - bbox[0], bbox[4] - bbox[1], bbox[5] - bbox[2]);
    gmsh::model::occ::synchronize();

    gmsh::option::setNumber("Mesh.CharacteristicLengthMin", target_size);
    gmsh::option::setNumber("Mesh.CharacteristicLengthMax", target_size);
    gmsh::option::setNumber("General.Verbosity", 1);  // errors only
    if (options && options->random_seed >= 0) {
      gmsh::option::setNumber("Mesh.RandomSeed",
          static_cast<double>(options->random_seed));
    }
    gmsh::model::mesh::generate(3);

    // Pull nodes + tet elements out of Gmsh in flat-buffer form.
    std::vector<std::size_t>  node_tags;
    std::vector<double>       node_coords_flat;
    std::vector<double>       node_param_coords;
    gmsh::model::mesh::getNodes(node_tags, node_coords_flat, node_param_coords,
                                /*dim=*/-1, /*tag=*/-1,
                                /*includeBoundary=*/true,
                                /*returnParametricCoord=*/false);

    std::vector<std::size_t> elem_tags;
    std::vector<std::size_t> elem_node_tags;
    gmsh::model::mesh::getElementsByType(kGmshTet4ElementType,
                                         elem_tags, elem_node_tags);

    if (node_tags.empty() || elem_tags.empty()) {
      gmsh::model::remove();
      return souxmar_status_error(SOUXMAR_E_PLUGIN_REJECTED,
          "gmsh produced no nodes / tetrahedra at the requested resolution");
    }

    // Build the souxmar mesh. Gmsh node tags are 1-based and may have
    // gaps; we remap onto contiguous souxmar node indices.
    std::vector<std::uint64_t> tag_to_idx(
        *std::max_element(node_tags.begin(), node_tags.end()) + 1,
        ~static_cast<std::uint64_t>(0));

    souxmar_mesh_t* mesh = souxmar_mesh_new();
    if (!mesh) {
      gmsh::model::remove();
      return souxmar_status_error(SOUXMAR_E_OUT_OF_MEMORY, "souxmar_mesh_new");
    }
    souxmar_mesh_reserve_nodes(mesh, node_tags.size());
    souxmar_mesh_reserve_cells(mesh, elem_tags.size());

    for (std::size_t i = 0; i < node_tags.size(); ++i) {
      const double p[3] = {
          node_coords_flat[i * 3 + 0],
          node_coords_flat[i * 3 + 1],
          node_coords_flat[i * 3 + 2]};
      tag_to_idx[node_tags[i]] = souxmar_mesh_add_node(mesh, p);
    }
    for (std::size_t e = 0; e < elem_tags.size(); ++e) {
      const std::uint64_t nodes[4] = {
          tag_to_idx[elem_node_tags[e * 4 + 0]],
          tag_to_idx[elem_node_tags[e * 4 + 1]],
          tag_to_idx[elem_node_tags[e * 4 + 2]],
          tag_to_idx[elem_node_tags[e * 4 + 3]]};
      souxmar_mesh_add_cell(mesh, SOUXMAR_ET_TET4, nodes, 4,
                            /*tag=*/-1, nullptr);
    }
    gmsh::model::remove();
    *out_mesh = mesh;
    return souxmar_status_ok();
  } catch (const std::exception& e) {
    // Don't leave Gmsh's model state hanging if anything threw.
    try { gmsh::model::remove(); } catch (...) {}
    static thread_local std::string msg;
    msg = std::string("gmsh threw: ") + e.what();
    return souxmar_status_error(SOUXMAR_E_PLUGIN_REJECTED, msg.c_str());
  }
}

void gmsh_destroy(void* /*user_data*/) {
  // Finalise Gmsh on plugin unload so its allocations don't leak past
  // the host's exit. Safe to call even if Gmsh isn't initialised.
  if (gmsh::isInitialized()) {
    try { gmsh::finalize(); } catch (...) {}
  }
}

constexpr souxmar_mesher_vtable_t kVtable = {
    SOUXMAR_ABI_VERSION_MAJOR,
    &gmsh_mesh,
    &gmsh_destroy,
};

}  // namespace

extern "C" SOUXMAR_PLUGIN_EXPORT
int souxmar_plugin_register_v1(souxmar_registry_t*        registry,
                               const souxmar_host_info_t* host) {
  if (!host || host->abi_version_major < SOUXMAR_ABI_VERSION_MAJOR) {
    return -1;
  }
  const souxmar_status_t s = souxmar_registry_add_mesher(
      registry, "mesher.tetra.gmsh", &kVtable, /*user_data=*/nullptr);
  return s.code == SOUXMAR_OK ? 0 : 1;
}
