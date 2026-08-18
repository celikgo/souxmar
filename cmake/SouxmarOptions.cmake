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

# ---------------------------------------------------------------------------
# Every souxmar executable is a potential plugin host: it dlopen()s plugin
# shared objects that resolve souxmar_* C-ABI symbols against the loading
# process. On ELF platforms an executable's symbols are NOT in the dynamic
# symbol table unless it was linked with --export-dynamic, so dlopen()ed
# plugins get "undefined symbol: souxmar_value_as_number" at load time even
# though `nm` shows the symbol present in the binary.
#
# ENABLE_EXPORTS is the portable, per-target CMake spelling of that. It makes
# CMake append CMAKE_SHARED_LIBRARY_LINK_<LANG>_FLAGS to the executable link:
# `-rdynamic` (i.e. --export-dynamic) on ELF; empty on Mach-O, where the flat
# namespace lookup used by `-undefined dynamic_lookup` plugins already sees
# the host's symbols, so it is a verified no-op there rather than a risk. On
# Windows it additionally generates the host import library a Windows plugin
# would have to link against.
#
# Set here rather than per executable so a new tool or test binary cannot
# forget it. The companion half of this fix — making sure the C-ABI object
# files reach the executable at all — is in src/core/CMakeLists.txt and
# src/pipeline/CMakeLists.txt; read those comment blocks before changing
# either. Windows caveat: exporting is necessary but not sufficient there,
# see cmake/SouxmarPlugin.cmake.
# ---------------------------------------------------------------------------
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.27)
  # CMAKE_ENABLE_EXPORTS is deprecated in favour of this from 3.27 on.
  set(CMAKE_EXECUTABLE_ENABLE_EXPORTS ON)
else()
  set(CMAKE_ENABLE_EXPORTS ON)
endif()

# ENABLE_EXPORTS alone is not enough on Windows. -rdynamic puts *every*
# symbol in the ELF dynamic table, but MSVC exports only what is marked
# __declspec(dllexport) or named in a .def — so a host executable built with
# ENABLE_EXPORTS still exported nothing, GetProcAddress in the plugin shim
# returned NULL for every lookup, and plugins loaded and registered no
# capabilities at all.
#
# WINDOWS_EXPORT_ALL_SYMBOLS generates that .def from the object files, for
# a shared library "or executable with ENABLE_EXPORTS". It is the MSVC
# spelling of -rdynamic, and marking the C ABI dllexport by hand is not an
# option: those declarations live in include/souxmar-c/**, which is frozen.
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

# Generator-expression predicate: "the consuming target produces a loadable
# image" — i.e. an executable, a shared library or a module, as opposed to a
# static archive or another object library.
#
# $<TARGET_PROPERTY:TYPE> with no target name resolves against the *consuming*
# target when it appears in a usage requirement, which makes this the guard
# src/core and src/pipeline use to hand their C-ABI object files to binaries
# that dlopen plugins without duplicating those TUs into every intermediate
# static archive. Defined once here so the two call sites cannot drift.
set(SOUXMAR_C_ABI_LOADABLE_CONSUMER
  "$<NOT:$<OR:$<STREQUAL:$<TARGET_PROPERTY:TYPE>,STATIC_LIBRARY>,$<STREQUAL:$<TARGET_PROPERTY:TYPE>,OBJECT_LIBRARY>>>")

if(SOUXMAR_ENABLE_ASAN AND SOUXMAR_ENABLE_TSAN)
  message(FATAL_ERROR
    "ASAN and TSAN cannot be enabled at the same time. Pick one.")
endif()
