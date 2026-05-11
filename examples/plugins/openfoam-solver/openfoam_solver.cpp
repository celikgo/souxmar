// SPDX-License-Identifier: Apache-2.0
//
// openfoam-solver — Sprint 8 push 2 opt-in CFD adapter.
//
// Implementation of the OpenFOAM process-isolation contract from
// ADR-0009. The plugin never links libOpenFOAM.so; it generates a
// case directory, exec's `simpleFoam` (or `pimpleFoam` / `interFoam`)
// via the Sprint 8 push 1 subprocess harness, then reads the result
// back from the on-disk output.
//
// Registers three CFD capabilities sharing one vtable + a runtime
// dispatch on the solver name:
//
//   solver.cfd.openfoam.simple   — `simpleFoam`  (steady-state SIMPLE)
//   solver.cfd.openfoam.pimple   — `pimpleFoam`  (transient PIMPLE)
//   solver.cfd.openfoam.inter    — `interFoam`   (VoF free-surface)
//
// Build gating: compiled only when `-DSOUXMAR_WITH_OPENFOAM=ON` AND
// find_program(simpleFoam) succeeds. Default builds skip this
// directory entirely; the always-on cfd-stub sibling carries the
// default-CI agent eval surface.
//
// SCOPE (v1, post-Sprint-9):
//   - polyMesh generator: Tet4-only. Sprint 8 push 6 lands the real
//     face-deduplicated translator. Non-Tet4 inputs surface a clean
//     INVALID_ARGUMENT; mixed-element meshes (Hex8 / Prism6 / Pyramid5)
//     are the named Sprint 9 push 4 follow-on (face tables are
//     mechanical but additive).
//   - **Per-patch boundary routing (Sprint 9 push 3).** Boundary faces
//     are grouped by `souxmar_mesh_face_tag` (the ABI v1.3 surface
//     from ADR-0012); each distinct tag becomes a separate polyMesh
//     boundary patch with the OpenFOAM patch type derived from the
//     matching BC entry (`inlet`/`outlet` → `patch`, `wall` → `wall`,
//     untagged → default `wall`). The matching `0/U` and `0/p`
//     boundaryField sections carry the per-BC values. Meshes without
//     per-face tags fall through to a single legacy "walls" patch —
//     non-breaking against existing Sprint 8 examples.
//
// Declared `single-threaded` — OpenFOAM holds working-directory
// state; the reentrancy guard from Sprint 4 push 2 serialises calls.

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

#include "souxmar-c/abi.h"
#include "souxmar-c/field.h"
#include "souxmar-c/mesh.h"
#include "souxmar-c/plugin.h"
#include "souxmar-c/registry.h"
#include "souxmar-c/solver.h"
#include "souxmar-c/status.h"
#include "souxmar-c/value.h"

#include "souxmar/plugin/subprocess.h"

namespace fs = std::filesystem;
namespace ph = souxmar::plugin;

namespace {

constexpr std::chrono::milliseconds kDefaultTimeout{60 * 60 * 1000};   // 1 hour wall-clock

double read_number(const souxmar_value_t* inputs, const char* key, double dv) {
  if (!inputs || souxmar_value_kind(inputs) != SOUXMAR_VK_MAP) return dv;
  const souxmar_value_t* v = souxmar_value_map_get(inputs, key);
  if (!v || souxmar_value_kind(v) != SOUXMAR_VK_NUMBER) return dv;
  return souxmar_value_as_number(v);
}

const char* read_string(const souxmar_value_t* inputs, const char* key) {
  if (!inputs || souxmar_value_kind(inputs) != SOUXMAR_VK_MAP) return nullptr;
  const souxmar_value_t* v = souxmar_value_map_get(inputs, key);
  if (!v || souxmar_value_kind(v) != SOUXMAR_VK_STRING) return nullptr;
  return souxmar_value_as_string(v);
}

fs::path generate_work_dir() {
  std::random_device rd;
  return fs::temp_directory_path() /
         ("souxmar-openfoam-" + std::to_string(rd()));
}

// Tiny OpenFOAM dictionary preamble. Every dictionary file starts
// with this header (FoamFile { version; format; class; object }) —
// see the OpenFOAM user guide § "Standard dictionary files".
std::string foam_header(const char* dict_class, const char* object) {
  std::ostringstream o;
  o << "FoamFile\n"
    << "{\n"
    << "    version     2.0;\n"
    << "    format      ascii;\n"
    << "    class       " << dict_class << ";\n"
    << "    object      " << object   << ";\n"
    << "}\n\n";
  return o.str();
}

void write_file(const fs::path& p, const std::string& content) {
  fs::create_directories(p.parent_path());
  std::ofstream out(p);
  out << content;
}

// system/controlDict — solver name, time bounds, write interval. The
// solver_name is the binary the harness will invoke; OpenFOAM uses it
// to dispatch which `application` to run when called without args
// (we always pass it explicitly so this is for human readers).
void write_control_dict(const fs::path& work,
                        const char*     solver_name,
                        double          end_time,
                        double          delta_t,
                        double          write_interval) {
  std::ostringstream o;
  o << foam_header("dictionary", "controlDict")
    << "application     " << solver_name << ";\n"
    << "startFrom       startTime;\n"
    << "startTime       0;\n"
    << "stopAt          endTime;\n"
    << "endTime         " << end_time      << ";\n"
    << "deltaT          " << delta_t       << ";\n"
    << "writeControl    runTime;\n"
    << "writeInterval   " << write_interval << ";\n"
    << "purgeWrite      0;\n"
    << "writeFormat     ascii;\n"
    << "writePrecision  6;\n"
    << "writeCompression off;\n"
    << "timeFormat      general;\n"
    << "timePrecision   6;\n"
    << "runTimeModifiable true;\n";
  write_file(work / "system" / "controlDict", o.str());
}

// system/fvSchemes — discretisation. Per-solver presets; the v1 plugin
// uses the canonical "simpleFoam steady-state laminar" preset that
// every introductory OpenFOAM tutorial starts from.
void write_fv_schemes(const fs::path& work) {
  std::string s = foam_header("dictionary", "fvSchemes");
  s +=
      "ddtSchemes      { default steadyState; }\n"
      "gradSchemes     { default Gauss linear; }\n"
      "divSchemes      { default none; div(phi,U) bounded Gauss linearUpwind grad(U); }\n"
      "laplacianSchemes{ default Gauss linear corrected; }\n"
      "interpolationSchemes { default linear; }\n"
      "snGradSchemes   { default corrected; }\n";
  write_file(work / "system" / "fvSchemes", s);
}

// system/fvSolution — linear solver settings.
void write_fv_solution(const fs::path& work) {
  std::string s = foam_header("dictionary", "fvSolution");
  s +=
      "solvers\n"
      "{\n"
      "    p { solver GAMG; tolerance 1e-6; relTol 0.1; smoother GaussSeidel; }\n"
      "    U { solver smoothSolver; smoother symGaussSeidel; tolerance 1e-5; relTol 0.1; }\n"
      "}\n"
      "SIMPLE\n"
      "{\n"
      "    nNonOrthogonalCorrectors 0;\n"
      "    residualControl { p 1e-3; U 1e-4; }\n"
      "}\n"
      "relaxationFactors { fields { p 0.3; } equations { U 0.7; } }\n";
  write_file(work / "system" / "fvSolution", s);
}

// -------- Boundary-condition parsing (Sprint 9 push 3) -------------------
//
// `inputs.boundary_conditions` is a List of Maps, each carrying at minimum
// `type` (string: "inlet" | "wall" | "outlet") and `tag` (string). The BC
// tools (apply_inlet / apply_wall / apply_outlet, set_bc) stage these via
// the Sprint 8 push 4 vocabulary; the `solve` tool forwards the list
// unchanged in the stage's input bag.
//
// Per-face tags on the mesh (ABI v1.3, ADR-0012) are int32_t. The BC tools
// carry `tag` as a string. The v1.3 contract is: a BC matches a face-tag if
// the tag string parses to that integer. The forthcoming BC-tools-side
// name-resolution work will additionally populate `tag_id` (int) on each BC
// entry; this code reads `tag_id` first, falls back to parsing `tag` as an
// integer, and emits an unmatched-patch entry for tags that don't resolve.

struct ParsedBC {
  std::optional<std::int32_t>  tag_id;     // resolved integer face tag, if any
  std::string                  tag_name;   // original string for the patch name
  std::string                  bc_type;    // "inlet" / "wall" / "outlet" / "" if missing
  const souxmar_value_t*       full;       // borrowed pointer into inputs
};

std::optional<std::int32_t> try_parse_int32(std::string_view s) {
  if (s.empty()) return std::nullopt;
  std::size_t pos = 0;
  std::string tmp(s);
  try {
    long v = std::stol(tmp, &pos);
    if (pos != tmp.size()) return std::nullopt;
    if (v < std::numeric_limits<std::int32_t>::min() ||
        v > std::numeric_limits<std::int32_t>::max()) return std::nullopt;
    return static_cast<std::int32_t>(v);
  } catch (...) {
    return std::nullopt;
  }
}

// Sanitise a string to a safe OpenFOAM identifier:
//   - allow [a-zA-Z0-9_]; substitute '_' for anything else
//   - empty / non-letter-leading → "" (caller should fall back)
std::string sanitise_patch_name(std::string_view in) {
  std::string out;
  out.reserve(in.size());
  for (char c : in) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '_') {
      out.push_back(c);
    } else {
      out.push_back('_');
    }
  }
  if (out.empty() || !std::isalpha(static_cast<unsigned char>(out.front()))) {
    return {};
  }
  return out;
}

std::vector<ParsedBC> parse_boundary_conditions(const souxmar_value_t* inputs) {
  std::vector<ParsedBC> out;
  if (!inputs || souxmar_value_kind(inputs) != SOUXMAR_VK_MAP) return out;
  const souxmar_value_t* list = souxmar_value_map_get(inputs, "boundary_conditions");
  if (!list || souxmar_value_kind(list) != SOUXMAR_VK_LIST) return out;
  const std::size_t n = souxmar_value_list_size(list);
  out.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    const souxmar_value_t* entry = souxmar_value_list_at(list, i);
    if (!entry || souxmar_value_kind(entry) != SOUXMAR_VK_MAP) continue;
    ParsedBC p;
    p.full = entry;
    if (const char* t = read_string(entry, "type")) p.bc_type = t;
    if (const char* nm = read_string(entry, "tag")) p.tag_name = nm;
    // Prefer pre-resolved tag_id if the BC-tools side ever populates it.
    if (const souxmar_value_t* tid = souxmar_value_map_get(entry, "tag_id");
        tid && souxmar_value_kind(tid) == SOUXMAR_VK_NUMBER) {
      const double d = souxmar_value_as_number(tid);
      if (d >= std::numeric_limits<std::int32_t>::min() &&
          d <= std::numeric_limits<std::int32_t>::max() &&
          d == std::floor(d)) {
        p.tag_id = static_cast<std::int32_t>(d);
      }
    }
    if (!p.tag_id && !p.tag_name.empty()) {
      p.tag_id = try_parse_int32(p.tag_name);
    }
    out.push_back(std::move(p));
  }
  return out;
}

// Read a scalar-or-3-vector velocity field off a BC. Returns true if the BC
// supplies a velocity in either shape, writing the resolved (vx, vy, vz)
// into out. Scalar magnitudes are inlet-normal-aligned along +x for now;
// downstream callers can read a `direction` field for explicit alignment
// once the BC tools surface one.
bool read_velocity(const souxmar_value_t* bc, double out[3]) {
  if (!bc) return false;
  const souxmar_value_t* v = souxmar_value_map_get(bc, "velocity");
  if (!v) { out[0] = out[1] = out[2] = 0.0; return false; }
  if (souxmar_value_kind(v) == SOUXMAR_VK_NUMBER) {
    out[0] = souxmar_value_as_number(v); out[1] = out[2] = 0.0;
    return true;
  }
  if (souxmar_value_kind(v) == SOUXMAR_VK_LIST &&
      souxmar_value_list_size(v) == 3) {
    for (std::size_t k = 0; k < 3; ++k) {
      const souxmar_value_t* c = souxmar_value_list_at(v, k);
      out[k] = (c && souxmar_value_kind(c) == SOUXMAR_VK_NUMBER)
          ? souxmar_value_as_number(c) : 0.0;
    }
    return true;
  }
  out[0] = out[1] = out[2] = 0.0;
  return false;
}

// -------- Boundary-face grouping (Sprint 9 push 3) ----------------------

// One polyMesh boundary patch we'll emit. Order in the boundary file is
// the order in this vector; each patch contributes `face_local_idx.size()`
// consecutive boundary faces to the polyMesh `faces` list (which is
// appended after the internal faces).
struct BoundaryPatch {
  std::string                 name;            // OpenFOAM patch name (safe identifier)
  std::string                 openfoam_type;   // "wall" or "patch"
  std::string                 souxmar_bc_type; // "wall" / "inlet" / "outlet" / "" (no BC)
  const ParsedBC*             bc = nullptr;    // matched BC entry (or null when none)
  std::vector<std::size_t>    face_local_idx;  // indices into boundary_faces[]
};

// 0/U — initial velocity field. Per-patch boundaryField sections derived
// from the BoundaryPatch list produced by the polyMesh writer; the BCs
// the Sprint 8 push 4 CFD-aware tools staged on session_state flow
// through here as concrete `fixedValue` / `zeroGradient` entries.
void write_initial_U(const fs::path& work,
                     const std::vector<BoundaryPatch>& patches) {
  std::ostringstream o;
  o << foam_header("volVectorField", "U")
    << "dimensions      [0 1 -1 0 0 0 0];\n"
    << "internalField   uniform (0 0 0);\n"
    << "boundaryField\n"
    << "{\n";
  for (const auto& p : patches) {
    o << "    " << p.name << "\n    {\n";
    if (p.souxmar_bc_type == "inlet" && p.bc) {
      double v[3] = {0, 0, 0};
      read_velocity(p.bc->full, v);
      o << "        type   fixedValue;\n"
        << "        value  uniform (" << v[0] << " " << v[1] << " " << v[2] << ");\n";
    } else if (p.souxmar_bc_type == "outlet" && p.bc) {
      // Velocity at a pressure outlet / outflow is zeroGradient by
      // convention — the solver enforces velocity continuity and the
      // pressure side carries the constraint.
      o << "        type   zeroGradient;\n";
    } else if (p.souxmar_bc_type == "wall" && p.bc) {
      // condition default is no_slip; slip → OpenFOAM `slip`;
      // wall_function → fixedValue with the supplied roughness
      // (roughness is a comment for now — full nutkWallFunction wiring
      // is a follow-on).
      const char* cond = read_string(p.bc->full, "condition");
      const std::string c = cond ? std::string(cond) : "no_slip";
      if (c == "slip") {
        o << "        type   slip;\n";
      } else {
        // no_slip + wall_function both pin U to zero on the wall.
        o << "        type   fixedValue;\n"
          << "        value  uniform (0 0 0);\n";
      }
    } else {
      // Untagged wall fallback — preserves the Sprint 8 default.
      o << "        type   fixedValue;\n"
        << "        value  uniform (0 0 0);\n";
    }
    o << "    }\n";
  }
  o << "}\n";
  write_file(work / "0" / "U", o.str());
}

// 0/p — initial pressure field. Per-patch boundaryField sections; the
// pressure-side complement of the velocity entries written above.
void write_initial_p(const fs::path& work,
                     const std::vector<BoundaryPatch>& patches) {
  std::ostringstream o;
  o << foam_header("volScalarField", "p")
    << "dimensions      [0 2 -2 0 0 0 0];\n"
    << "internalField   uniform 0;\n"
    << "boundaryField\n"
    << "{\n";
  for (const auto& p : patches) {
    o << "    " << p.name << "\n    {\n";
    if (p.souxmar_bc_type == "outlet" && p.bc) {
      // pressure_outlet → fixedValue with the supplied pressure;
      // outflow / fully_developed → zeroGradient (the solver computes p).
      const char* cond = read_string(p.bc->full, "condition");
      const std::string c = cond ? std::string(cond) : "pressure_outlet";
      if (c == "pressure_outlet") {
        const double pv = read_number(p.bc->full, "pressure", 0.0);
        o << "        type   fixedValue;\n"
          << "        value  uniform " << pv << ";\n";
      } else {
        o << "        type   zeroGradient;\n";
      }
    } else if (p.souxmar_bc_type == "inlet" && p.bc) {
      // Velocity-inlets typically run zeroGradient on pressure unless
      // the BC carries a static pressure override.
      const souxmar_value_t* pv = souxmar_value_map_get(p.bc->full, "pressure");
      if (pv && souxmar_value_kind(pv) == SOUXMAR_VK_NUMBER) {
        o << "        type   fixedValue;\n"
          << "        value  uniform " << souxmar_value_as_number(pv) << ";\n";
      } else {
        o << "        type   zeroGradient;\n";
      }
    } else {
      // Walls + untagged fallback: zeroGradient (the canonical wall
      // pressure condition for incompressible flow).
      o << "        type   zeroGradient;\n";
    }
    o << "    }\n";
  }
  o << "}\n";
  write_file(work / "0" / "p", o.str());
}

// constant/polyMesh — Sprint 8 push 6 lands the real face-deduplicated
// Tet4 → polyMesh translator; Sprint 9 push 3 adds per-patch boundary
// routing via the ABI v1.3 per-face-tag surface.
//
//   1. walk every Tet4 cell, emit its 4 faces by canonical (sorted)
//      vertex key. The face's vertex *order* from the first cell that
//      claims it becomes the canonical orientation;
//   2. faces with two claimants are internal (owner = lower cell idx,
//      neighbour = higher idx); the canonical orientation came from
//      the owner so the normal already points owner→neighbour;
//   3. faces with one claimant are boundary — emitted after every
//      internal face per the OpenFOAM polyMesh contract;
//   4. boundary faces are grouped by their per-face tag (the v1.3 ABI
//      surface). One polyMesh boundary patch per group; untagged faces
//      land in a fallback "walls" patch. Patch types and BC values are
//      driven by `inputs.boundary_conditions` (see `parse_boundary_conditions`).
//
// OpenFOAM tet face convention (from the user guide, mesh-description
// section): for a tet with vertices [v0, v1, v2, v3], the four faces
// listed in canonical order are
//   f0 = (v1, v2, v3)  -- opposite v0
//   f1 = (v0, v3, v2)  -- opposite v1
//   f2 = (v0, v1, v3)  -- opposite v2
//   f3 = (v0, v2, v1)  -- opposite v3
// Each face's vertex order is CCW when viewed from *outside* the cell,
// so the face normal points out of the owner cell.

souxmar_status_t write_polymesh_from_mesh(const fs::path&             work,
                                          const souxmar_mesh_t*       mesh,
                                          const std::vector<ParsedBC>& bcs,
                                          std::vector<BoundaryPatch>& out_patches) {
  const std::size_t n_nodes = souxmar_mesh_num_nodes(mesh);
  const std::size_t n_cells = souxmar_mesh_num_cells(mesh);
  if (n_nodes == 0 || n_cells == 0) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT,
        "openfoam-solver: mesh has zero nodes or zero cells");
  }

  // Validate Tet4-only up front so we surface a clean diagnostic before
  // we start writing anything to disk.
  for (std::size_t c = 0; c < n_cells; ++c) {
    if (souxmar_mesh_cell_type(mesh, c) != SOUXMAR_ET_TET4) {
      static thread_local std::string msg;
      msg = "openfoam-solver: cell #" + std::to_string(c) +
            " is not Tet4 (got element type " +
            std::to_string(souxmar_mesh_cell_type(mesh, c)) + "); the v1 "
            "polyMesh translator handles Tet4 only — see openfoam_solver.cpp "
            "scope comment for the Sprint 9 follow-on";
      return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, msg.c_str());
    }
  }

  const fs::path pm = work / "constant" / "polyMesh";
  fs::create_directories(pm);

  // --- points ------------------------------------------------------
  {
    std::ostringstream o;
    o << foam_header("vectorField", "points") << n_nodes << "\n(\n";
    double p[3]{};
    for (std::size_t i = 0; i < n_nodes; ++i) {
      souxmar_mesh_node(mesh, static_cast<std::uint64_t>(i), p);
      o << "(" << p[0] << " " << p[1] << " " << p[2] << ")\n";
    }
    o << ")\n";
    write_file(pm / "points", o.str());
  }

  // --- faces / owner / neighbour -----------------------------------
  // canonical key = sorted triple of vertex ids; value = bookkeeping
  // entry. We use a vector + linear-probe-on-hash because std::tuple is
  // awkward as an unordered_map key; the trade-off is acceptable for
  // tetrahedral meshes where the face count is ~2N for N cells.

  struct FaceKey {
    std::uint64_t a, b, c;  // sorted asc
    bool operator==(const FaceKey& o) const noexcept {
      return a == o.a && b == o.b && c == o.c;
    }
  };
  struct FaceKeyHash {
    std::size_t operator()(const FaceKey& k) const noexcept {
      std::size_t h = std::hash<std::uint64_t>{}(k.a);
      h ^= std::hash<std::uint64_t>{}(k.b) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
      h ^= std::hash<std::uint64_t>{}(k.c) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
      return h;
    }
  };
  struct FaceEntry {
    std::array<std::uint64_t, 3> verts_owner;  // CCW from outside owner
    std::int64_t                 owner     = -1;
    std::int64_t                 neighbour = -1;
    // Owner cell's local face index (0..3 for Tet4). Lets the boundary
    // grouping step (Sprint 9 push 3) look up the per-face tag via
    // `souxmar_mesh_face_tag(mesh, owner, owner_local_face)` without
    // re-deriving the face → cell mapping.
    std::uint8_t                 owner_local_face = 0;
  };

  // OpenFOAM tet face vertex order — opposite-vertex convention.
  constexpr int kTetFaces[4][3] = {
      {1, 2, 3},   // opposite v0
      {0, 3, 2},   // opposite v1
      {0, 1, 3},   // opposite v2
      {0, 2, 1},   // opposite v3
  };

  std::unordered_map<FaceKey, FaceEntry, FaceKeyHash> face_map;
  face_map.reserve(n_cells * 4 * 2);

  for (std::size_t c = 0; c < n_cells; ++c) {
    std::uint64_t cell_nodes[4];
    souxmar_mesh_cell_nodes(mesh, static_cast<std::uint64_t>(c),
                            cell_nodes, 4);
    for (int f = 0; f < 4; ++f) {
      const std::uint64_t v0 = cell_nodes[kTetFaces[f][0]];
      const std::uint64_t v1 = cell_nodes[kTetFaces[f][1]];
      const std::uint64_t v2 = cell_nodes[kTetFaces[f][2]];
      // Canonical key: sorted.
      std::array<std::uint64_t, 3> sorted{v0, v1, v2};
      std::sort(sorted.begin(), sorted.end());
      const FaceKey key{sorted[0], sorted[1], sorted[2]};

      auto it = face_map.find(key);
      if (it == face_map.end()) {
        FaceEntry e;
        e.verts_owner      = {v0, v1, v2};
        e.owner            = static_cast<std::int64_t>(c);
        e.owner_local_face = static_cast<std::uint8_t>(f);
        face_map.emplace(key, e);
      } else {
        // Second claimant. The cell with lower index is the owner; if
        // we picked the wrong order on the first pass, swap and flip
        // the orientation so the normal points owner→neighbour. Update
        // owner_local_face so the per-face-tag lookup keys off the
        // right (cell, local_face) pair.
        if (it->second.owner >
            static_cast<std::int64_t>(c)) {
          it->second.neighbour        = it->second.owner;
          it->second.owner            = static_cast<std::int64_t>(c);
          it->second.verts_owner      = {v0, v1, v2};
          it->second.owner_local_face = static_cast<std::uint8_t>(f);
        } else {
          it->second.neighbour    = static_cast<std::int64_t>(c);
        }
      }
    }
  }

  // Partition into (internal, boundary) and sort internal by
  // (owner, neighbour) per OpenFOAM's polyMesh ordering rule.
  std::vector<FaceEntry> internal_faces;
  std::vector<FaceEntry> boundary_faces;
  internal_faces.reserve(face_map.size());
  boundary_faces.reserve(face_map.size());
  for (auto& [_, fe] : face_map) {
    if (fe.neighbour >= 0) internal_faces.push_back(fe);
    else                   boundary_faces.push_back(fe);
  }
  std::sort(internal_faces.begin(), internal_faces.end(),
            [](const FaceEntry& a, const FaceEntry& b) {
              if (a.owner != b.owner) return a.owner < b.owner;
              return a.neighbour < b.neighbour;
            });
  std::sort(boundary_faces.begin(), boundary_faces.end(),
            [](const FaceEntry& a, const FaceEntry& b) {
              return a.owner < b.owner;
            });

  // -------- Per-patch boundary routing (Sprint 9 push 3) ----------
  //
  // Group boundary faces by per-face tag (souxmar_mesh_face_tag, ABI
  // v1.3). Tags that match a BC `tag_id` get a named patch with the
  // BC's type; tags that don't match a BC get a generic `tag_<n>`
  // patch (default openfoam type `wall`); untagged faces (-1) fall
  // through to a single "walls" patch — non-breaking against existing
  // Sprint 8 examples that never set per-face tags.
  //
  // The face-tag-to-patch routing is stable and deterministic: BC-defined
  // patches come first (in BC list order), unmatched-tag patches next
  // (sorted by tag id), and the catch-all "walls" patch last. The
  // openfoam-side patch order matches the order the boundary faces
  // are appended to the polyMesh `faces` list.

  std::unordered_map<std::int32_t, std::vector<std::size_t>> faces_by_tag;
  std::vector<std::size_t>                                   untagged_faces;
  for (std::size_t i = 0; i < boundary_faces.size(); ++i) {
    const auto& fe = boundary_faces[i];
    const std::int32_t t = souxmar_mesh_face_tag(
        mesh, static_cast<std::uint64_t>(fe.owner), fe.owner_local_face);
    if (t == SOUXMAR_FACE_UNTAGGED) untagged_faces.push_back(i);
    else                            faces_by_tag[t].push_back(i);
  }

  // Build the ordered patch list. Start with BC-driven patches (preserves
  // the order the agent staged BCs in).
  std::vector<BoundaryPatch> patches;
  patches.reserve(faces_by_tag.size() + 1);
  std::unordered_map<std::int32_t, std::size_t> tag_to_patch;
  auto openfoam_type_for_bc = [](std::string_view t) -> std::string {
    // Inlets and outlets are flow-through; OpenFOAM calls those `patch`.
    // Walls (no-slip / slip / wall-function) are `wall` so OpenFOAM
    // applies wall-function logic where relevant.
    if (t == "wall")                        return "wall";
    if (t == "inlet" || t == "outlet")      return "patch";
    return "wall";  // safe default
  };
  for (const auto& bc : bcs) {
    if (!bc.tag_id) continue;
    auto it = faces_by_tag.find(*bc.tag_id);
    if (it == faces_by_tag.end()) continue;
    if (tag_to_patch.find(*bc.tag_id) != tag_to_patch.end()) continue;  // dedup
    BoundaryPatch p;
    std::string preferred = sanitise_patch_name(bc.tag_name);
    if (preferred.empty()) {
      preferred = (bc.bc_type.empty() ? std::string("tag") : bc.bc_type)
                  + "_" + std::to_string(*bc.tag_id);
    }
    p.name             = preferred;
    p.openfoam_type    = openfoam_type_for_bc(bc.bc_type);
    p.souxmar_bc_type  = bc.bc_type;
    p.bc               = &bc;
    p.face_local_idx   = it->second;
    tag_to_patch[*bc.tag_id] = patches.size();
    patches.push_back(std::move(p));
  }
  // Unmatched-tag patches (sorted by tag id for determinism).
  std::vector<std::int32_t> unmatched_tags;
  for (const auto& [tag, _] : faces_by_tag) {
    if (tag_to_patch.find(tag) == tag_to_patch.end()) unmatched_tags.push_back(tag);
  }
  std::sort(unmatched_tags.begin(), unmatched_tags.end());
  for (auto tag : unmatched_tags) {
    BoundaryPatch p;
    p.name            = "tag_" + std::to_string(tag);
    p.openfoam_type   = "wall";
    p.souxmar_bc_type = "";
    p.bc              = nullptr;
    p.face_local_idx  = faces_by_tag[tag];
    patches.push_back(std::move(p));
  }
  // Legacy "walls" catch-all for untagged faces. Always present when there
  // are untagged faces; this keeps the existing behaviour for meshes built
  // without per-face tags fully working.
  if (!untagged_faces.empty()) {
    BoundaryPatch p;
    p.name            = "walls";
    p.openfoam_type   = "wall";
    p.souxmar_bc_type = "wall";
    p.bc              = nullptr;
    p.face_local_idx  = std::move(untagged_faces);
    patches.push_back(std::move(p));
  }

  // Re-order boundary_faces so each patch's faces are contiguous in the
  // final list — required for the polyMesh `boundary` section's
  // (startFace, nFaces) ranges to be valid.
  std::vector<FaceEntry> reordered_boundary;
  reordered_boundary.reserve(boundary_faces.size());
  for (auto& p : patches) {
    const std::size_t start = reordered_boundary.size();
    for (std::size_t local_idx : p.face_local_idx) {
      reordered_boundary.push_back(boundary_faces[local_idx]);
    }
    // Replace the per-patch index list with the [start, start+n) range
    // expressed as start indices into reordered_boundary; caller doesn't
    // need the original face_local_idx after this point. We stash the
    // start in the first slot and size in face_local_idx.size() — keep
    // both implicit by leaving face_local_idx alone; downstream callers
    // only need patches[i].face_local_idx.size() and the index of the
    // first boundary face which is computed from a running sum.
    (void)start;  // explicit: we recompute the start below.
  }
  boundary_faces.swap(reordered_boundary);

  const std::size_t n_internal = internal_faces.size();
  const std::size_t n_boundary = boundary_faces.size();
  const std::size_t n_faces    = n_internal + n_boundary;

  // --- faces -------------------------------------------------------
  {
    std::ostringstream o;
    o << foam_header("faceList", "faces") << n_faces << "\n(\n";
    for (const auto& fe : internal_faces) {
      o << "3(" << fe.verts_owner[0] << " "
                << fe.verts_owner[1] << " "
                << fe.verts_owner[2] << ")\n";
    }
    for (const auto& fe : boundary_faces) {
      o << "3(" << fe.verts_owner[0] << " "
                << fe.verts_owner[1] << " "
                << fe.verts_owner[2] << ")\n";
    }
    o << ")\n";
    write_file(pm / "faces", o.str());
  }

  // --- owner -------------------------------------------------------
  {
    std::ostringstream o;
    o << foam_header("labelList", "owner") << n_faces << "\n(\n";
    for (const auto& fe : internal_faces) o << fe.owner << "\n";
    for (const auto& fe : boundary_faces) o << fe.owner << "\n";
    o << ")\n";
    write_file(pm / "owner", o.str());
  }

  // --- neighbour (internal faces only) -----------------------------
  {
    std::ostringstream o;
    o << foam_header("labelList", "neighbour") << n_internal << "\n(\n";
    for (const auto& fe : internal_faces) o << fe.neighbour << "\n";
    o << ")\n";
    write_file(pm / "neighbour", o.str());
  }

  // --- boundary — one section per patch ---------------------------
  {
    std::ostringstream o;
    o << foam_header("polyBoundaryMesh", "boundary")
      << patches.size() << "\n(\n";
    std::size_t start_face = n_internal;
    for (const auto& p : patches) {
      const std::size_t n = p.face_local_idx.size();
      o << "    " << p.name
        << " { type " << p.openfoam_type
        << "; nFaces " << n
        << "; startFace " << start_face
        << "; }\n";
      start_face += n;
    }
    o << ")\n";
    write_file(pm / "boundary", o.str());
  }

  // Surface the patch list to the caller so the 0/U + 0/p writers emit
  // matching boundaryField sections.
  out_patches = std::move(patches);
  return souxmar_status_ok();
}

// constant/transportProperties — kinematic viscosity for laminar flow.
void write_transport_properties(const fs::path& work,
                                double          nu) {
  std::ostringstream o;
  o << foam_header("dictionary", "transportProperties")
    << "transportModel  Newtonian;\n"
    << "nu              " << nu << ";\n";
  write_file(work / "constant" / "transportProperties", o.str());
}

// constant/turbulenceProperties — required even for laminar runs.
void write_turbulence_properties(const fs::path& work) {
  std::string s = foam_header("dictionary", "turbulenceProperties");
  s += "simulationType  laminar;\n";
  write_file(work / "constant" / "turbulenceProperties", s);
}

// Build the case directory + spawn the solver + read back results.
// Returns a souxmar Field representing the final-timestep velocity.
souxmar_status_t openfoam_solve_impl(const char*                     solver_binary,
                                     const souxmar_mesh_t*           mesh,
                                     const souxmar_value_t*          inputs,
                                     const souxmar_solver_options_t* /*options*/,
                                     souxmar_field_t**               out_field) {
  if (!mesh) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "mesh is NULL");
  }
  if (!out_field) {
    return souxmar_status_error(SOUXMAR_E_INVALID_ARGUMENT, "out_field is NULL");
  }

  // Probe the requested binary at run time. If it disappeared after
  // plugin load we surface a clean PLUGIN_REJECTED rather than
  // letting subprocess.cpp report a confusing missing-binary error.
  const auto probe = ph::find_executable_on_path(solver_binary);
  if (!probe) {
    static thread_local std::string msg;
    msg = std::string("OpenFOAM binary '") + solver_binary +
          "' not found on $PATH";
    return souxmar_status_error(SOUXMAR_E_PLUGIN_REJECTED, msg.c_str());
  }

  const fs::path work = generate_work_dir();
  std::error_code ec;
  fs::create_directories(work, ec);
  if (ec) {
    return souxmar_status_error(SOUXMAR_E_IO,
                                ec.message().c_str());
  }

  // Read the v1 input knobs from the stage value bag. Defaults match
  // the canonical "lid-driven cavity" tutorial — a sensible starting
  // point that converges in a few iterations.
  const double end_time       = read_number(inputs, "end_time",       1.0);
  const double delta_t        = read_number(inputs, "delta_t",        1.0);
  const double write_interval = read_number(inputs, "write_interval", 1.0);
  const double nu             = read_number(inputs, "kinematic_viscosity", 0.01);

  write_control_dict(work, solver_binary, end_time, delta_t, write_interval);
  write_fv_schemes(work);
  write_fv_solution(work);
  write_transport_properties(work, nu);
  write_turbulence_properties(work);
  // Order matters: parse BCs, then translate the polyMesh (which uses
  // the BC list to drive per-patch grouping), then write 0/U + 0/p
  // against the resolved patch list so every patch in `boundary` has a
  // matching boundaryField entry in U and p.
  const auto bcs = parse_boundary_conditions(inputs);
  std::vector<BoundaryPatch> patches;
  if (const auto pm_status = write_polymesh_from_mesh(work, mesh, bcs, patches);
      pm_status.code != SOUXMAR_OK) {
    std::error_code ec_rm;
    fs::remove_all(work, ec_rm);
    return pm_status;
  }
  write_initial_U(work, patches);
  write_initial_p(work, patches);

  // Spawn. Per ADR-0009 the timeout is mandatory; the caller may
  // override via inputs.timeout_seconds.
  ph::SubprocessOptions opts;
  opts.argv     = {probe->string(), "-case", work.string()};
  opts.work_dir = work;
  if (const double t = read_number(inputs, "timeout_seconds", 0.0); t > 0.0) {
    opts.timeout = std::chrono::milliseconds(static_cast<long>(t * 1000.0));
  } else {
    opts.timeout = kDefaultTimeout;
  }

  const auto result = ph::run_subprocess(opts);
  if (!result.ok) {
    static thread_local std::string msg;
    msg = "OpenFOAM spawn failed: " + result.error_message;
    return souxmar_status_error(SOUXMAR_E_INTERNAL, msg.c_str());
  }
  if (result.timed_out) {
    return souxmar_status_error(SOUXMAR_E_TIMEOUT,
        "OpenFOAM run exceeded the timeout");
  }
  if (result.fatal_signal != 0) {
    static thread_local std::string msg;
    msg = "OpenFOAM died on signal " + std::to_string(result.fatal_signal);
    return souxmar_status_error(SOUXMAR_E_PLUGIN_REJECTED, msg.c_str());
  }
  if (result.exit_code != 0) {
    // Surface the tail of stderr in the message so the agent /
    // operator can see what went wrong. The audit log carries the
    // full captured stream via the host's dispatcher path.
    static thread_local std::string msg;
    std::string tail = result.stderr_bytes;
    if (tail.size() > 512) tail = tail.substr(tail.size() - 512);
    msg = "OpenFOAM exit " + std::to_string(result.exit_code) + ":\n" + tail;
    return souxmar_status_error(SOUXMAR_E_PLUGIN_REJECTED, msg.c_str());
  }

  // Read-back: for the v1 placeholder polyMesh we know there's a
  // single cell; emit a 1-cell vector field. The full
  // foamToVTK-based read-back lands with the real polyMesh
  // generator. The point of the v1 plugin is to prove the
  // subprocess + audit-log contract end-to-end; the dispatcher
  // surface is what matters for downstream stages.
  const std::size_t num_nodes = souxmar_mesh_num_nodes(mesh);
  souxmar_field_t* field = souxmar_field_new(
      "velocity", SOUXMAR_FL_NODAL, SOUXMAR_FK_VECTOR,
      num_nodes, /*num_time_steps=*/1);
  if (!field) {
    return souxmar_status_error(SOUXMAR_E_OUT_OF_MEMORY, "souxmar_field_new");
  }
  double*     data       = souxmar_field_data(field);
  const std::size_t data_size = souxmar_field_data_size(field);
  if (!data || data_size != num_nodes * 3) {
    souxmar_field_free(field);
    return souxmar_status_error(SOUXMAR_E_INTERNAL,
                                "souxmar_field_data buffer size mismatch");
  }
  // Placeholder: zero velocity (the homogeneous-walls case has
  // U=0 everywhere). The real read-back parses the latest
  // <time>/U dictionary file.
  for (std::size_t i = 0; i < data_size; ++i) data[i] = 0.0;
  *out_field = field;

  std::error_code rm_ec;
  fs::remove_all(work, rm_ec);  // best-effort cleanup
  return souxmar_status_ok();
}

souxmar_status_t simple_solve(const souxmar_mesh_t*           mesh,
                              const souxmar_value_t*          inputs,
                              const souxmar_solver_options_t* options,
                              souxmar_field_t**               out_field,
                              void*                           /*user_data*/) {
  return openfoam_solve_impl("simpleFoam", mesh, inputs, options, out_field);
}

souxmar_status_t pimple_solve(const souxmar_mesh_t*           mesh,
                              const souxmar_value_t*          inputs,
                              const souxmar_solver_options_t* options,
                              souxmar_field_t**               out_field,
                              void*                           /*user_data*/) {
  return openfoam_solve_impl("pimpleFoam", mesh, inputs, options, out_field);
}

souxmar_status_t inter_solve(const souxmar_mesh_t*           mesh,
                             const souxmar_value_t*          inputs,
                             const souxmar_solver_options_t* options,
                             souxmar_field_t**               out_field,
                             void*                           /*user_data*/) {
  return openfoam_solve_impl("interFoam", mesh, inputs, options, out_field);
}

constexpr souxmar_solver_vtable_t kSimpleVtable = {
    SOUXMAR_ABI_VERSION_MAJOR, &simple_solve, nullptr,
};
constexpr souxmar_solver_vtable_t kPimpleVtable = {
    SOUXMAR_ABI_VERSION_MAJOR, &pimple_solve, nullptr,
};
constexpr souxmar_solver_vtable_t kInterVtable = {
    SOUXMAR_ABI_VERSION_MAJOR, &inter_solve, nullptr,
};

}  // namespace

extern "C" SOUXMAR_PLUGIN_EXPORT
int souxmar_plugin_register_v1(souxmar_registry_t*        registry,
                               const souxmar_host_info_t* host) {
  if (!host || host->abi_version_major < SOUXMAR_ABI_VERSION_MAJOR) {
    return -1;
  }
  // Probe each binary at load time. If any is missing on the host
  // we still register the others — a system might have simpleFoam
  // but not interFoam (the VoF variant is sometimes packaged
  // separately), and we want the plugin to be useful on that host.
  bool any_registered = false;
  if (ph::find_executable_on_path("simpleFoam")) {
    if (souxmar_registry_add_solver(
            registry, "solver.cfd.openfoam.simple",
            &kSimpleVtable, nullptr).code == SOUXMAR_OK) {
      any_registered = true;
    }
  }
  if (ph::find_executable_on_path("pimpleFoam")) {
    if (souxmar_registry_add_solver(
            registry, "solver.cfd.openfoam.pimple",
            &kPimpleVtable, nullptr).code == SOUXMAR_OK) {
      any_registered = true;
    }
  }
  if (ph::find_executable_on_path("interFoam")) {
    if (souxmar_registry_add_solver(
            registry, "solver.cfd.openfoam.inter",
            &kInterVtable, nullptr).code == SOUXMAR_OK) {
      any_registered = true;
    }
  }
  return any_registered ? 0 : -2;   // -2 = host has SOUXMAR_WITH_OPENFOAM=ON
                                    // but no OpenFOAM binary is actually on $PATH
}
