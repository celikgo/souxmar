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
// LIMITATIONS (v1):
//   - The polyMesh generator currently writes a placeholder
//     single-cell case. Generating a complete OpenFOAM polyMesh from
//     an arbitrary souxmar Tet4 mesh (face deduplication +
//     owner/neighbour bookkeeping + boundary patch extraction) is
//     substantial; it lands as an additive-minor ratchet event
//     mid-sprint-8 alongside the pipe-bend example (push 6). The v1
//     plugin proves the subprocess + case-dir + read-back contract
//     end-to-end against the placeholder.
//   - Homogeneous wall BCs only. Structured `apply_inlet` /
//     `apply_wall` / `apply_outlet` from push 4 of this sprint will
//     thread real BCs through the value bag.
//
// Declared `single-threaded` — OpenFOAM holds working-directory
// state; the reentrancy guard from Sprint 4 push 2 serialises calls.

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <system_error>

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

// constant/polyMesh — Sprint 8 push 2 ships a placeholder
// single-tetrahedron polyMesh so the subprocess + case-dir + run-back
// contract is end-to-end testable. Real polyMesh generation from
// arbitrary souxmar Tet4 meshes (face dedup + owner/neighbour
// bookkeeping + boundary patch extraction) lands mid-sprint-8 as a
// follow-on additive-minor ratchet event alongside the pipe-bend
// example (push 6 of this sprint).
void write_placeholder_polymesh(const fs::path& work) {
  const fs::path pm = work / "constant" / "polyMesh";
  fs::create_directories(pm);

  // 8 points of a unit cube.
  {
    std::ostringstream o;
    o << foam_header("vectorField", "points")
      << "8\n(\n"
      << "(0 0 0)\n(1 0 0)\n(1 1 0)\n(0 1 0)\n"
      << "(0 0 1)\n(1 0 1)\n(1 1 1)\n(0 1 1)\n"
      << ")\n";
    write_file(pm / "points", o.str());
  }
  // 6 faces (cube faces — quads).
  {
    std::ostringstream o;
    o << foam_header("faceList", "faces")
      << "6\n(\n"
      << "4(0 3 2 1)\n"   // bottom (-z)
      << "4(4 5 6 7)\n"   // top    (+z)
      << "4(0 1 5 4)\n"   // front  (-y)
      << "4(2 3 7 6)\n"   // back   (+y)
      << "4(0 4 7 3)\n"   // left   (-x)
      << "4(1 2 6 5)\n"   // right  (+x)
      << ")\n";
    write_file(pm / "faces", o.str());
  }
  // 1 owner per face — all faces own the single cell index 0.
  {
    std::ostringstream o;
    o << foam_header("labelList", "owner")
      << "6\n(\n0\n0\n0\n0\n0\n0\n)\n";
    write_file(pm / "owner", o.str());
  }
  // No internal faces — neighbour list is empty.
  {
    std::ostringstream o;
    o << foam_header("labelList", "neighbour") << "0\n(\n)\n";
    write_file(pm / "neighbour", o.str());
  }
  // boundary — one "walls" patch covering all 6 faces of the cube.
  {
    std::ostringstream o;
    o << foam_header("polyBoundaryMesh", "boundary")
      << "1\n(\n"
      << "    walls { type wall; nFaces 6; startFace 0; }\n"
      << ")\n";
    write_file(pm / "boundary", o.str());
  }
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
  write_placeholder_polymesh(work);

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
