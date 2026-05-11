# Public configuration options for the souxmar build.
# Defaults err on the side of "fast PR CI build" — heavy adapters off by default.
# CI nightly turns on the adapters; release builds turn on everything.

include_guard(GLOBAL)

option(SOUXMAR_BUILD_TESTS       "Build unit + integration tests"        ON)
option(SOUXMAR_BUILD_EXAMPLES    "Build example pipelines"               OFF)
option(SOUXMAR_BUILD_BENCHMARKS  "Build benchmark suite"                 OFF)
option(SOUXMAR_BUILD_CLI         "Build the souxmar CLI executable"      ON)
option(SOUXMAR_BUILD_PYTHON      "Build pysouxmar Python bindings"       OFF)
option(SOUXMAR_BUILD_DOCS        "Build documentation (Doxygen + Sphinx)" OFF)

# Adapters — each pulls in a heavy dependency. Off by default; flip on per build.
option(SOUXMAR_WITH_OPENCASCADE  "Enable OpenCASCADE adapter (CAD geometry kernel)"  OFF)
option(SOUXMAR_WITH_GMSH         "Enable Gmsh adapter (alternative mesher)"          OFF)
option(SOUXMAR_WITH_FENICSX      "Enable FEniCSx adapter (FEM solver)"               OFF)
option(SOUXMAR_WITH_OPENFOAM     "Enable OpenFOAM adapter (CFD solver, subprocess)"  OFF)
option(SOUXMAR_WITH_BLENDER      "Enable Blender .blend importer"                    OFF)
option(SOUXMAR_WITH_VTK          "Enable VTK writer (ParaView output)"               OFF)

# Sanitisers and coverage — at most one of ASAN/TSAN per build.
option(SOUXMAR_ENABLE_ASAN       "AddressSanitizer (debug builds)"        OFF)
option(SOUXMAR_ENABLE_TSAN       "ThreadSanitizer (debug builds)"         OFF)
option(SOUXMAR_ENABLE_UBSAN      "UndefinedBehaviorSanitizer"             OFF)
option(SOUXMAR_ENABLE_COVERAGE   "Code coverage instrumentation (gcov)"   OFF)

option(SOUXMAR_WERROR            "Treat compiler warnings as errors"      ON)

if(SOUXMAR_ENABLE_ASAN AND SOUXMAR_ENABLE_TSAN)
  message(FATAL_ERROR
    "ASAN and TSAN cannot be enabled at the same time. Pick one.")
endif()
