# souxmar CLI — the host-facing executable.
#
# `souxmar run <pipeline.yaml>` and `souxmar plugin list` are the v1 subcommands.
# Built only when SOUXMAR_BUILD_CLI=ON (default). The CLI links the same core,
# plugin, and pipeline libraries the tests link, plus fmt for formatted output.

find_package(fmt CONFIG REQUIRED)

add_executable(souxmar_cli
  main.cpp
)
add_executable(souxmar::cli ALIAS souxmar_cli)

set_target_properties(souxmar_cli PROPERTIES
  OUTPUT_NAME souxmar
  FOLDER      "cli"
)

target_link_libraries(souxmar_cli
  PRIVATE
    souxmar::core
    souxmar::plugin
    souxmar::pipeline
    souxmar::ai
    # Sprint 10 push 6 — `souxmar update check` / `apply --dry-run`.
    souxmar::update
    souxmar::warnings
    souxmar::sanitizers
    fmt::fmt
)

install(TARGETS souxmar_cli
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Expose the built CLI binary path so integration tests can exec it.
set(SOUXMAR_CLI_BINARY "$<TARGET_FILE:souxmar_cli>"
    CACHE INTERNAL "Built souxmar CLI binary path")
