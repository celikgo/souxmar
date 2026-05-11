# OpenFOAM CFD adapter. Built only when SOUXMAR_WITH_OPENFOAM=ON AND
# the simpleFoam binary is reachable on $PATH at configure time. Per
# ADR-0009 we use find_program (resolving binaries on $PATH) rather
# than find_package (which would resolve OpenFOAM headers and pull
# them into souxmar's include path — that's the GPL-tainting path we
# explicitly forbid).
#
# Default builds skip this directory entirely (the option defaults
# OFF in cmake/SouxmarOptions.cmake). Nightly CFD CI flips the option
# on against a Docker image that pre-stages OpenFOAM v12.
#
# The plugin links libsouxmar-plugin for the subprocess harness (the
# Sprint 8 push 1 surface), which is the only allowed linking — the
# harness is host-side C++, not anything from OpenFOAM.

find_program(SOUXMAR_SIMPLE_FOAM_BIN simpleFoam)

if(NOT SOUXMAR_SIMPLE_FOAM_BIN)
  message(STATUS
    "souxmar: openfoam-solver skipped — `simpleFoam` not on PATH. "
    "Source OpenFOAM's bashrc (`source <openfoam-install>/etc/bashrc`) "
    "and reconfigure, or build with -DSOUXMAR_WITH_OPENFOAM=OFF (the default).")
  return()
endif()
message(STATUS "souxmar: openfoam-solver enabled — simpleFoam at ${SOUXMAR_SIMPLE_FOAM_BIN}")

souxmar_add_plugin(openfoam_solver
  SOURCES   openfoam_solver.cpp
  MANIFEST  ${CMAKE_CURRENT_SOURCE_DIR}/souxmar-plugin.toml
  CAPABILITIES "solver.cfd.openfoam.simple;solver.cfd.openfoam.pimple;solver.cfd.openfoam.inter"
)

# The plugin uses the subprocess harness from libsouxmar-plugin.
# That's a souxmar-internal C++ library, NOT a GPL'd component;
# ADR-0009's no-GPL-linking rule is preserved. We override
# souxmar_add_plugin's default "link only public_headers" by adding
# the plugin library explicitly here.
target_link_libraries(openfoam_solver PRIVATE souxmar::plugin)

if(APPLE)
  set_target_properties(openfoam_solver PROPERTIES
    PREFIX "lib" SUFFIX ".dylib")
elseif(UNIX)
  set_target_properties(openfoam_solver PROPERTIES
    PREFIX "lib" SUFFIX ".so")
elseif(WIN32)
  set_target_properties(openfoam_solver PROPERTIES
    PREFIX "" SUFFIX ".dll" OUTPUT_NAME "openfoam_solver")
endif()

set(SOUXMAR_OPENFOAM_SOLVER_DIR "$<TARGET_FILE_DIR:openfoam_solver>"
    CACHE INTERNAL "Built openfoam-solver plugin directory")
