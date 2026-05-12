// SPDX-License-Identifier: Apache-2.0
//
// cfd-stub — Sprint 8 push 2 reference CFD solver.
// Sprint 13 push 4 — per-patch BC routing carry-over from
// Sprint 10. Closes the Sprint 10 retro's open action item.
//
// Registers `solver.cfd.simple`. The point of this plugin is the
// same as elasticity-stub from Sprint 7 push 2: give the agent eval
// suite, the pipe-bend example, and the documentation tutorials a
// runnable CFD solver in the default CI matrix — without dragging
// OpenFOAM into every build. The validation-grade companion is the
// opt-in fenicsx-/OpenFOAM-backed adapter sibling.
//
// What it computes (no BCs):
//   Closed-form uniform velocity field along the requested direction.
//     U(node) = velocity_magnitude * flow_direction
//
// What it computes (with `patches:` + `boundary_conditions:`):
//   Per-patch routing — each node receives:
//     * inlet patch    → patch.velocity (or magnitude*direction if absent)
//     * wall  patch    → (0, 0, 0)            (no-slip)
//     * outlet patch   → magnitude*direction  (passes through)
//     * (no patch)     → magnitude*direction  (bulk default)
//
// Routing precedence (highest first): wall > inlet > outlet > bulk.
// Wall dominance is the conservative choice — a node on a wall face
// AND an inlet face is at a corner; a real solver disambiguates by
// the surface mesh edge orientation, the stub conservatively pins
// it to zero so downstream postproc never sees a spurious velocity
// at a wall corner.
//
// Patch resolution:
//   `patches: [{ name, tag, bc }, ...]` is a list — each entry maps
//   a human name + face_tag pair to a BC spec. List form lets us
//   iterate without needing a map-key-at API (which the v1.3 ABI
//   doesn't expose; adding one is post-1.0 work).
//
// What this is NOT: a real CFD solver. It still ignores viscosity,
// mesh topology beyond face tags, and time-step physics. Per-patch
// routing structure is correct; per-patch physics is not.
//
// Inputs (souxmar_value_t map):
//   velocity_magnitude  : number, defaults to 1.0 (m/s)
//   flow_direction      : optional list [x, y, z]; defaults to [1, 0, 0]
//   patches             : optional list, each entry:
//                           name      : string (informational)
//                           tag       : int (face_tag this patch covers)
//                           bc.type   : "wall" | "inlet" | "outlet"
//                           bc.velocity? : [x,y,z] (inlet only)
//
// Output: nodal vector Field "velocity" with 3 components, 1 time step.

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "souxmar-c/abi.h"
#include "souxmar-c/field.h"
#include "souxmar-c/mesh.h"
#include "souxmar-c/plugin.h"
#include "souxmar-c/registry.h"
#include "souxmar-c/solver.h"
#include "souxmar-c/status.h"
#include "souxmar-c/value.h"

namespace {

// Per-node routing decision. Wall > inlet > outlet > bulk; encoded
// as an enum to keep `if`-chains readable rather than juggling
// magic ints.
enum class NodeRouting : uint8_t {
  Bulk = 0,
  Outlet,
  Inlet,
  Wall,
};

bool routing_supersedes(NodeRouting incoming, NodeRouting current) {
  return static_cast<uint8_t>(incoming) > static_cast<uint8_t>(current);
}

// BC spec resolved at the patch level. The velocity field is
// optional — if absent for an inlet, we fall back to the bulk
// magnitude*direction (i.e. the patch declares "use bulk flow
// through this surface").
struct PatchBc {
  NodeRouting           routing = NodeRouting::Bulk;
  std::array<double, 3> velocity{0.0, 0.0, 0.0};
  bool                  velocity_set = false;
};

double read_number(const souxmar_value_t* inputs, const char* key, double dv) {
  if (!inputs || souxmar_value_kind(inputs) != SOUXMAR_VK_MAP) return dv;
  const souxmar_value_t* v = souxmar_value_map_get(inputs, key);
  if (!v || souxmar_value_kind(v) != SOUXMAR_VK_NUMBER) return dv;
  return souxmar_value_as_number(v);
}

std::array<double, 3>
read_direction(const souxmar_value_t* inputs) {
  std::array<double, 3> d{1.0, 0.0, 0.0};
  if (!inputs || souxmar_value_kind(inputs) != SOUXMAR_VK_MAP) return d;
  const souxmar_value_t* v = souxmar_value_map_get(inputs, "flow_direction");
  if (!v || souxmar_value_kind(v) != SOUXMAR_VK_LIST) return d;
  const std::size_t n = souxmar_value_list_size(v);
  if (n != 3) return d;
  for (std::size_t i = 0; i < 3; ++i) {
    const souxmar_value_t* c = souxmar_value_list_at(v, i);
    if (c && souxmar_value_kind(c) == SOUXMAR_VK_NUMBER) {
      d[i] = souxmar_value_as_number(c);
    }
  }
  return d;
}

bool read_vec3(const souxmar_value_t* v, std::array<double, 3>* out) {
  if (!v || souxmar_value_kind(v) != SOUXMAR_VK_LIST) return false;
  if (souxmar_value_list_size(v) != 3) return false;
  for (std::size_t i = 0; i < 3; ++i) {
    const souxmar_value_t* c = souxmar_value_list_at(v, i);
    if (!c || souxmar_value_kind(c) != SOUXMAR_VK_NUMBER) return false;
    (*out)[i] = souxmar_value_as_number(c);
  }
  return true;
}

NodeRouting routing_from_type_string(const char* type) {
  if (!type) return NodeRouting::Bulk;
  const std::string t(type);
  if (t == "wall"   || t == "no-slip") return NodeRouting::Wall;
  if (t == "inlet"  || t == "velocity-inlet") return NodeRouting::Inlet;
  if (t == "outlet" || t == "pressure-outlet") return NodeRouting::Outlet;
  return NodeRouting::Bulk;
}

// Read the patches list and fold into a single tag → PatchBc
// index. Returns an empty map (which the caller treats as "no per-
// patch routing — apply bulk to all nodes") when the list is
// missing.
std::map<int32_t, PatchBc>
read_patch_bcs(const souxmar_value_t* inputs) {
  std::map<int32_t, PatchBc> out;
  if (!inputs || souxmar_value_kind(inputs) != SOUXMAR_VK_MAP) return out;

  const souxmar_value_t* patches = souxmar_value_map_get(inputs, "patches");
  if (!patches || souxmar_value_kind(patches) != SOUXMAR_VK_LIST) return out;

  const std::size_t n_patches = souxmar_value_list_size(patches);
  for (std::size_t i = 0; i < n_patches; ++i) {
    const souxmar_value_t* entry = souxmar_value_list_at(patches, i);
    if (!entry || souxmar_value_kind(entry) != SOUXMAR_VK_MAP) continue;

    const souxmar_value_t* tag_v = souxmar_value_map_get(entry, "tag");
    if (!tag_v || souxmar_value_kind(tag_v) != SOUXMAR_VK_NUMBER) continue;
    const int32_t tag = static_cast<int32_t>(souxmar_value_as_number(tag_v));

    const souxmar_value_t* bc = souxmar_value_map_get(entry, "bc");
    if (!bc || souxmar_value_kind(bc) != SOUXMAR_VK_MAP) continue;

    const souxmar_value_t* type_v = souxmar_value_map_get(bc, "type");
    const char* type = (type_v && souxmar_value_kind(type_v) == SOUXMAR_VK_STRING)
                         ? souxmar_value_as_string(type_v)
                         : nullptr;

    PatchBc spec{};
    spec.routing = routing_from_type_string(type);

    const souxmar_value_t* vel = souxmar_value_map_get(bc, "velocity");
    if (vel) {
      std::array<double, 3> v{};
      if (read_vec3(vel, &v)) {
        spec.velocity     = v;
        spec.velocity_set = true;
      }
    }

    if (spec.routing != NodeRouting::Bulk) {
      out[tag] = spec;
    }
  }
  return out;
}

// Local face-vertex tables (Gmsh / VTK / OpenFOAM side-set ordering,
// per ADR-0012 § "Convention matches Gmsh/VTK/OpenFOAM"). Only the
// element types the example plugins emit today are covered; adding
// more is a Tier-0 change (file-local table). Returns the number of
// faces; out_face_vertex_count[*][.] and out_face_vertices[*][...]
// are written in-place.
//
// face_vertices[face][k] is the k-th local node index of the face,
// in the range [0, cell_node_count).
//
// Returns 0 (and leaves outputs untouched) for an unknown element
// type — the routing code falls back to a cell-tag-only path for
// such cells.
struct LocalFaces {
  std::size_t                  face_count = 0;
  std::array<std::size_t, 6>   face_vertex_count{};
  std::array<std::array<std::size_t, 4>, 6> face_vertices{};
};

LocalFaces faces_for(uint16_t element_type) {
  LocalFaces f;
  switch (element_type) {
    case SOUXMAR_ET_TET4: {
      // Faces: (1,2,3), (0,3,2), (0,1,3), (0,2,1) — Gmsh tet4
      // convention. The exact node order matters for orientation;
      // the stub treats the face as an unordered set of nodes for
      // BC application, so any consistent ordering is fine.
      f.face_count = 4;
      const std::size_t verts[4][3] = {
          {1, 2, 3}, {0, 3, 2}, {0, 1, 3}, {0, 2, 1},
      };
      for (std::size_t i = 0; i < 4; ++i) {
        f.face_vertex_count[i] = 3;
        for (std::size_t k = 0; k < 3; ++k) f.face_vertices[i][k] = verts[i][k];
      }
      return f;
    }
    case SOUXMAR_ET_HEX8: {
      f.face_count = 6;
      const std::size_t verts[6][4] = {
          {0, 3, 2, 1},  // -z
          {4, 5, 6, 7},  // +z
          {0, 1, 5, 4},  // -y
          {2, 3, 7, 6},  // +y
          {0, 4, 7, 3},  // -x
          {1, 2, 6, 5},  // +x
      };
      for (std::size_t i = 0; i < 6; ++i) {
        f.face_vertex_count[i] = 4;
        for (std::size_t k = 0; k < 4; ++k) f.face_vertices[i][k] = verts[i][k];
      }
      return f;
    }
    default:
      return f;  // face_count == 0 → unknown
  }
}

souxmar_status_t cfd_solve(const souxmar_mesh_t*           mesh,
                           const souxmar_value_t*          inputs,
                           const souxmar_solver_options_t* /*options*/,
                           souxmar_field_t**               out_field,
                           void*                           /*user_data*/) {
  if (!mesh) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "mesh is NULL");
  }
  if (!out_field) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "out_field is NULL");
  }
  const std::size_t num_nodes = souxmar_mesh_num_nodes(mesh);
  if (num_nodes == 0) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "mesh has no nodes");
  }

  const double magnitude = read_number(inputs, "velocity_magnitude", 1.0);
  const auto   direction = read_direction(inputs);
  const auto   patch_bcs = read_patch_bcs(inputs);

  // Sprint 13 push 4 — per-node routing decision. Initialised to
  // Bulk for every node; promoted in priority order as we walk
  // faces. The wall-dominance rule is enforced by
  // routing_supersedes() (Wall > Inlet > Outlet > Bulk).
  std::vector<NodeRouting>          node_routing(num_nodes, NodeRouting::Bulk);
  std::vector<std::array<double, 3>> node_velocity(num_nodes);
  for (std::size_t i = 0; i < num_nodes; ++i) {
    node_velocity[i] = {magnitude * direction[0],
                        magnitude * direction[1],
                        magnitude * direction[2]};
  }

  if (!patch_bcs.empty()) {
    const std::size_t num_cells = souxmar_mesh_num_cells(mesh);
    for (std::size_t c = 0; c < num_cells; ++c) {
      const uint16_t etype = souxmar_mesh_cell_type(mesh, c);
      const LocalFaces lf   = faces_for(etype);
      if (lf.face_count == 0) continue;  // unknown element — skip

      const std::size_t cell_node_count = souxmar_mesh_cell_node_count(mesh, c);
      if (cell_node_count == 0) continue;

      std::array<uint64_t, 27> cell_nodes{};  // hex27 is the max we cover
      const auto st = souxmar_mesh_cell_nodes(
          mesh, c, cell_nodes.data(), cell_nodes.size());
      if (st.code != SOUXMAR_OK) continue;

      for (std::size_t fi = 0; fi < lf.face_count; ++fi) {
        const int32_t tag =
            souxmar_mesh_face_tag(mesh, c, static_cast<uint8_t>(fi));
        if (tag == SOUXMAR_FACE_UNTAGGED) continue;
        const auto it = patch_bcs.find(tag);
        if (it == patch_bcs.end()) continue;
        const PatchBc& bc = it->second;

        std::array<double, 3> v_for_face = bc.velocity_set
          ? bc.velocity
          : std::array<double, 3>{magnitude * direction[0],
                                  magnitude * direction[1],
                                  magnitude * direction[2]};
        if (bc.routing == NodeRouting::Wall) {
          v_for_face = {0.0, 0.0, 0.0};
        }

        for (std::size_t k = 0; k < lf.face_vertex_count[fi]; ++k) {
          const std::size_t local = lf.face_vertices[fi][k];
          if (local >= cell_node_count) continue;
          const uint64_t global = cell_nodes[local];
          if (global >= num_nodes) continue;
          if (routing_supersedes(bc.routing, node_routing[global])) {
            node_routing[global]  = bc.routing;
            node_velocity[global] = v_for_face;
          }
        }
      }
    }
  }

  souxmar_field_t* field =
      souxmar_field_new("velocity", SOUXMAR_FL_NODAL, SOUXMAR_FK_VECTOR,
                        num_nodes, /*num_time_steps=*/1);
  if (!field) {
    return souxmar_status_error(SOUXMAR_E_OUT_OF_MEMORY, "souxmar_field_new failed");
  }
  double*           data      = souxmar_field_data(field);
  const std::size_t data_size = souxmar_field_data_size(field);
  if (!data || data_size != num_nodes * 3) {
    souxmar_field_free(field);
    return souxmar_status_error(SOUXMAR_E_INTERNAL,
                                "souxmar_field_data buffer size mismatch");
  }

  for (std::size_t i = 0; i < num_nodes; ++i) {
    data[i * 3 + 0] = node_velocity[i][0];
    data[i * 3 + 1] = node_velocity[i][1];
    data[i * 3 + 2] = node_velocity[i][2];
  }
  *out_field = field;
  return souxmar_status_ok();
}

constexpr souxmar_solver_vtable_t kVtable = {
    SOUXMAR_ABI_VERSION_MAJOR,
    &cfd_solve,
    nullptr,
};

}  // namespace

extern "C" SOUXMAR_PLUGIN_EXPORT
int souxmar_plugin_register_v1(souxmar_registry_t*        registry,
                               const souxmar_host_info_t* host) {
  if (!host || host->abi_version_major < SOUXMAR_ABI_VERSION_MAJOR) {
    return -1;
  }
  const souxmar_status_t s = souxmar_registry_add_solver(
      registry, "solver.cfd.simple", &kVtable, /*user_data=*/nullptr);
  return s.code == SOUXMAR_OK ? 0 : 1;
}
