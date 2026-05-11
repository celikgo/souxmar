# libsouxmar-core — the in-process data model and pipeline orchestrator.
# At Sprint 0 this contains only version introspection. The data model
# (Geometry / Topology / Mesh / Field / LinearAlgebra) lands in Sprint 1.

add_library(souxmar_core
  version.cpp
)
add_library(souxmar::core ALIAS souxmar_core)

set_target_properties(souxmar_core PROPERTIES
  OUTPUT_NAME    souxmar-core
  VERSION        ${PROJECT_VERSION}
  SOVERSION      ${PROJECT_VERSION_MAJOR}
  EXPORT_NAME    core
)

target_link_libraries(souxmar_core
  PUBLIC
    souxmar::public_headers
  PRIVATE
    souxmar::warnings
    souxmar::sanitizers
)

# Generated build_config.h is consumed only inside this TU.
target_include_directories(souxmar_core
  PRIVATE
    ${CMAKE_BINARY_DIR}/generated/include
)

install(TARGETS souxmar_core
  EXPORT souxmar-targets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
