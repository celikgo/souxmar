# libsouxmar-ai — agent tool framework + the v1 tool implementations.
# Sprint 4 push 3 ships the framework + the 5 tools called out in
# docs/AI_INTEGRATION.md. Future tools join the catalogue via RFCs.

find_package(fmt CONFIG REQUIRED)

add_library(souxmar_ai
  tool_dispatcher.cpp
  tools/default_registry.cpp
  tools/read_geometry_summary.cpp
  tools/mesh.cpp
  tools/set_bc.cpp
  tools/solve.cpp
  tools/screenshot_viewport.cpp
)
add_library(souxmar::ai ALIAS souxmar_ai)

set_target_properties(souxmar_ai PROPERTIES
  OUTPUT_NAME    souxmar-ai
  VERSION        ${PROJECT_VERSION}
  SOVERSION      ${PROJECT_VERSION_MAJOR}
  EXPORT_NAME    ai
)

target_link_libraries(souxmar_ai
  PUBLIC
    souxmar::public_headers
    souxmar::core
    souxmar::plugin
    souxmar::pipeline
  PRIVATE
    souxmar::warnings
    souxmar::sanitizers
    fmt::fmt
)

install(TARGETS souxmar_ai
  EXPORT souxmar-targets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
