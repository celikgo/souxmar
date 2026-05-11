# souxmar-conformance — the v1 plugin conformance suite as a runnable
# binary. Pair-built with the main `souxmar` CLI (same SOUXMAR_BUILD_CLI
# gate) since both live in the same operator workflow.

find_package(fmt CONFIG REQUIRED)

add_executable(souxmar_conformance
  main.cpp
)
add_executable(souxmar::conformance ALIAS souxmar_conformance)

set_target_properties(souxmar_conformance PROPERTIES
  OUTPUT_NAME souxmar-conformance
  FOLDER      "cli"
)

target_link_libraries(souxmar_conformance
  PRIVATE
    souxmar::core
    souxmar::plugin
    souxmar::warnings
    souxmar::sanitizers
    fmt::fmt
)

install(TARGETS souxmar_conformance
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Expose the built binary path so integration tests can shell out to it.
set(SOUXMAR_CONFORMANCE_BINARY "$<TARGET_FILE:souxmar_conformance>"
    CACHE INTERNAL "Built souxmar-conformance binary path")
