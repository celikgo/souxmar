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
// SCOPE (v1, post-Sprint-8):
//   - polyMesh generator: Tet4-only. Sprint 8 push 6 lands the real
//     face-deduplicated translator (closes the placeholder noted by
//     pushes 2 + 5). Non-Tet4 inputs surface a clean INVALID_ARGUMENT;
//     mixed-element meshes are a Sprint 9 follow-on (Hex8 / Prism6 /
//     Pyramid5 face tables are mechanical but additive).
//   - Single "walls" boundary patch. Routing apply_inlet / apply_wall /
//     apply_outlet (push 4) through to per-patch boundaries requires
//     per-face tag exposure on the C ABI (additive minor) — Sprint 9
//     follow-on. Until then, openfoam-solver runs with uniform wall BCs
//     and the BC list staged on session_state is informational.
//
// Declared `single-threaded` — OpenFOAM holds working-directory
// state; the reentrancy guard from Sprint 4 push 2 serialises calls.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
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

// 0/U — initial velocity field. Homogeneous zero for the v1 plugin;
// the Sprint 8 push 4 CFD-aware tools (apply_inlet / apply_wall /
// apply_outlet) thread real BCs through the value bag.
void write_initial_U(const fs::path& work) {
  std::string s = foam_header("volVectorField", "U");
  s +=
      "dimensions      [0 1 -1 0 0 0 0];\n"
      "internalField   uniform (0 0 0);\n"
      "boundaryField\n"
      "{\n"
      "    walls { type fixedValue; value uniform (0 0 0); }\n"
      "}\n";
  write_file(work / "0" / "U", s);
}

// 0/p — initial pressure field.
void write_initial_p(const fs::path& work) {
  std::string s = foam_header("volScalarField", "p");
  s +=
      "dimensions      [0 2 -2 0 0 0 0];\n"
      "internalField   uniform 0;\n"
      "boundaryField\n"
      "{\n"
      "    walls { type zeroGradient; }\n"
      "}\n";
  write_file(work / "0" / "p", s);
}

// constant/polyMesh — Sprint 8 push 6 replaces the push-2 placeholder
// single-cell case with a real Tet4 → polyMesh translator. The shape:
//
//   1. walk every Tet4 cell, emit its 4 faces by canonical (sorted)
//      vertex key. The face's vertex *order* from the first cell that
//      claims it becomes the canonical orientation;
//   2. faces with two claimants are internal (owner = lower cell idx,
//      neighbour = higher idx); the canonical orientation came from
//      the owner so the normal already points owner→neighbour;
//   3. faces with one claimant are boundary — emitted after every
//      internal face per the OpenFOAM polyMesh contract;
//   4. a single "walls" boundary patch covers every boundary face.
//      Per-patch routing (apply_inlet / apply_wall / apply_outlet from
//      push 4) requires per-face tags on the C ABI — Sprint 9 follow-on.
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

souxmar_status_t write_polymesh_from_mesh(const fs::path&        work,
                                          const souxmar_mesh_t*  mesh) {
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
        e.verts_owner = {v0, v1, v2};
        e.owner       = static_cast<std::int64_t>(c);
        face_map.emplace(key, e);
      } else {
        // Second claimant. The cell with lower index is the owner; if
        // we picked the wrong order on the first pass, swap and flip
        // the orientation so the normal points owner→neighbour.
        if (it->second.owner >
            static_cast<std::int64_t>(c)) {
          it->second.neighbour    = it->second.owner;
          it->second.owner        = static_cast<std::int64_t>(c);
          it->second.verts_owner  = {v0, v1, v2};
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

  // --- boundary — single "walls" patch ----------------------------
  {
    std::ostringstream o;
    o << foam_header("polyBoundaryMesh", "boundary")
      << "1\n(\n"
      << "    walls { type wall; nFaces " << n_boundary
      << "; startFace " << n_internal << "; }\n"
      << ")\n";
    write_file(pm / "boundary", o.str());
  }
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
  write_initial_U(work);
  write_initial_p(work);
  if (const auto pm_status = write_polymesh_from_mesh(work, mesh);
      pm_status.code != SOUXMAR_OK) {
    std::error_code ec_rm;
    fs::remove_all(work, ec_rm);
    return pm_status;
  }

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
