# libsouxmar-pipeline — DAG orchestrator + parser + cache + runner.
# Sprint 3 push 2 adds RegistryDispatcher (the bridge from the runner's
# IDispatcher to real plugin vtables in the Registry) and the C ABI value
# accessors used by solver / writer plugin inputs. Sprint 4 push 2 adds
# the parallel runner (parallel_runner.cpp) plus the per-plugin
# reentrancy guard.

find_package(fmt        CONFIG REQUIRED)
find_package(yaml-cpp   CONFIG REQUIRED)

# The parallel runner spawns std::thread workers, which on POSIX requires
# explicit linking against pthread on some toolchains.
find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# souxmar_pipeline_c_abi — souxmar_value_* as an OBJECT library.
#
# DO NOT fold c_abi_value.cpp back into souxmar_pipeline's source list.
#
# WHY: this was the live bug. souxmar_value_* is the accessor family every
# solver / writer / postproc / reader plugin uses to read its YAML input bag,
# and *nothing in the host calls it* — the host builds Values, plugins read
# them. So when c_abi_value.cpp was an ordinary member of the static archive
# libsouxmar-pipeline.a, the linker dropped it from every host executable, and
# every plugin that reads its input bag failed at dlopen with
#   symbol not found in flat namespace '_souxmar_value_as_number'
# The archive had all 9 symbols; `souxmar` had none of them. The failure was
# invisible at build time and only reproduced by actually loading a plugin.
#
# HOW / portability: see the long block at the top of src/core/CMakeLists.txt.
# Same mechanism, same reasoning, same warning to future readers.
#
# Note that souxmar::pipeline_c_abi is NOT self-contained the way
# souxmar::core_c_abi is: these objects call pipeline::Value's members, which
# live in libsouxmar-pipeline. So an executable that dlopens plugins must link
# souxmar::pipeline (which carries the objects as a usage requirement) — the
# object library alone will not link. That is the whole reason
# tools/conformance links the pipeline library despite never running a
# pipeline; see the comment there.
# ---------------------------------------------------------------------------
add_library(souxmar_pipeline_c_abi OBJECT
  c_abi_value.cpp
)
add_library(souxmar::pipeline_c_abi ALIAS souxmar_pipeline_c_abi)

target_link_libraries(souxmar_pipeline_c_abi
  PUBLIC
    souxmar::public_headers
  PRIVATE
    souxmar::warnings
    souxmar::sanitizers
)

add_library(souxmar_pipeline
  value.cpp
  parser.cpp
  dag.cpp
  cache.cpp
  runner.cpp
  parallel_runner.cpp
  registry_dispatcher.cpp
)
add_library(souxmar::pipeline ALIAS souxmar_pipeline)

set_target_properties(souxmar_pipeline PROPERTIES
  OUTPUT_NAME    souxmar-pipeline
  VERSION        ${PROJECT_VERSION}
  SOVERSION      ${PROJECT_VERSION_MAJOR}
  EXPORT_NAME    pipeline
)

target_link_libraries(souxmar_pipeline
  PUBLIC
    souxmar::public_headers
    souxmar::core
    souxmar::plugin
    Threads::Threads
  PRIVATE
    souxmar::warnings
    souxmar::sanitizers
    fmt::fmt
    yaml-cpp::yaml-cpp
    # Keeps libsouxmar-pipeline.a's member list unchanged.
    souxmar_pipeline_c_abi
)

# ...and puts the objects directly on the link line of every consumer that
# produces a loadable image, so they cannot be dropped. Both genex guards are
# explained in src/core/CMakeLists.txt; SOUXMAR_C_ABI_LOADABLE_CONSUMER is
# defined in cmake/SouxmarOptions.cmake.
target_sources(souxmar_pipeline
  INTERFACE
    $<BUILD_INTERFACE:$<${SOUXMAR_C_ABI_LOADABLE_CONSUMER}:$<TARGET_OBJECTS:souxmar_pipeline_c_abi>>>
)

install(TARGETS souxmar_pipeline
  EXPORT souxmar-targets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
