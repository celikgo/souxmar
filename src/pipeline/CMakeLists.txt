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

add_library(souxmar_pipeline
  value.cpp
  parser.cpp
  dag.cpp
  cache.cpp
  runner.cpp
  parallel_runner.cpp
  c_abi_value.cpp
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
)

install(TARGETS souxmar_pipeline
  EXPORT souxmar-targets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
