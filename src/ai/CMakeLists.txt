# libsouxmar-ai — agent tool framework + the v1 tool implementations.
# Sprint 4 push 3 ships the framework + the 5 tools called out in
# docs/AI_INTEGRATION.md. Future tools join the catalogue via RFCs.

find_package(fmt CONFIG REQUIRED)
find_package(tomlplusplus CONFIG REQUIRED)   # Sprint 6 push 6 — budget_config.cpp
find_package(yaml-cpp CONFIG REQUIRED)       # Sprint 10 push 9 — provider response parsing

add_library(souxmar_ai
  tool_dispatcher.cpp
  audit_log.cpp
  # Sprint 6 push 6 — per-project budget config loader.
  budget_config.cpp
  # Sprint 10 push 9 — Provider abstraction + Ollama.
  provider.cpp
  tools/default_registry.cpp
  tools/read_geometry_summary.cpp
  tools/mesh.cpp
  tools/set_bc.cpp
  tools/solve.cpp
  tools/screenshot_viewport.cpp
  tools/query_field.cpp
  tools/compute_field.cpp
  tools/propose_pipeline.cpp
  # Sprint 6 push 1.
  tools/query_mesh_quality.cpp
  # Sprint 6 push 3.
  tools/set_material.cpp
  tools/list_plugins.cpp
  tools/apply_pipeline_diff.cpp
  tools/export_results.cpp
  # Sprint 8 push 4 — CFD-aware BC vocabulary.
  tools/apply_inlet.cpp
  tools/apply_wall.cpp
  tools/apply_outlet.cpp
  # Sprint 8 push 5 — CFD planner + BC validator.
  tools/propose_cfd_setup.cpp
  tools/validate_bcs.cpp
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
    tomlplusplus::tomlplusplus
    yaml-cpp::yaml-cpp
)

install(TARGETS souxmar_ai
  EXPORT souxmar-targets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
