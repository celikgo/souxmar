# FEniCSx-backed Poisson solver. Built only when SOUXMAR_WITH_FENICSX=ON
# AND a usable DOLFINx install is present. find_package is QUIET so a
# developer flipping the option without DOLFINx installed gets a clean
# STATUS skip rather than a noisy cmake-time failure. CI environments
# that actually rely on DOLFINx should run with -DSOUXMAR_WITH_FENICSX=ON
# AND require the package — flip QUIET to REQUIRED on those runners.
#
# Dependency chain when this plugin builds:
#   dolfinx  →  basix, ufcx, petsc, mpi
# All five are typically packaged together (fenics-dolfinx on Debian /
# Ubuntu, dolfinx on conda-forge). Vcpkg has the heavy adapters as a
# manifest feature; turning on -DSOUXMAR_WITH_FENICSX=ON should pull them.

find_package(DOLFINX QUIET)

if(NOT DOLFINX_FOUND)
  message(STATUS
    "souxmar: fenicsx-solver skipped — find_package(DOLFINX) didn't find a usable install. "
    "Provide DOLFINX_DIR / CMAKE_PREFIX_PATH and reconfigure, or build with "
    "-DSOUXMAR_WITH_FENICSX=OFF (the default).")
  return()
endif()

# The FFCx-compiled poisson form. The build step produces poisson.c
# from poisson.py via the `ffcx` tool; we add the generated file as a
# plugin source so the form symbols are linked together with the
# adapter's C++ shim. The form file is generated at configure time;
# the .py is the canonical UFL source.
#
# The Sprint 7 push 2 ships a pre-generated form alongside the .py to
# keep the build hermetic (no FFCx required at souxmar build time).
# Plugin authors that want to edit the UFL pass `-DSOUXMAR_REGENERATE_FORMS=ON`
# and we re-run ffcx.

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/poisson.c")
  message(STATUS
    "souxmar: fenicsx-solver skipped — poisson.c (FFCx-generated form) is missing. "
    "Run `ffcx --output-directory examples/plugins/fenicsx-solver examples/plugins/fenicsx-solver/poisson.py` "
    "and commit the output, then reconfigure. See examples/plugins/fenicsx-solver/README.md.")
  return()
endif()

souxmar_add_plugin(fenicsx_solver
  SOURCES   fenicsx_solver.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/poisson.c   # FFCx-prebuilt; see poisson.py
  MANIFEST  ${CMAKE_CURRENT_SOURCE_DIR}/souxmar-plugin.toml
  CAPABILITIES "solver.heat.fenicsx"
)

target_link_libraries(fenicsx_solver PRIVATE dolfinx)

if(APPLE)
  set_target_properties(fenicsx_solver PROPERTIES
    PREFIX "lib" SUFFIX ".dylib")
elseif(UNIX)
  set_target_properties(fenicsx_solver PROPERTIES
    PREFIX "lib" SUFFIX ".so")
elseif(WIN32)
  set_target_properties(fenicsx_solver PROPERTIES
    PREFIX "" SUFFIX ".dll" OUTPUT_NAME "fenicsx_solver")
endif()

set(SOUXMAR_FENICSX_SOLVER_DIR "$<TARGET_FILE_DIR:fenicsx_solver>"
    CACHE INTERNAL "Built fenicsx-solver plugin directory")
