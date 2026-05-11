# souxmar-eval — Sprint 7 push 4 agent eval runner. Pair-built with
# the `souxmar` CLI (same SOUXMAR_BUILD_CLI gate) since both live in
# the same operator workflow.

find_package(fmt        CONFIG REQUIRED)
find_package(yaml-cpp   CONFIG REQUIRED)

add_executable(souxmar_eval
  main.cpp
)
add_executable(souxmar::eval ALIAS souxmar_eval)

set_target_properties(souxmar_eval PROPERTIES
  OUTPUT_NAME souxmar-eval
  FOLDER      "cli"
)

target_link_libraries(souxmar_eval
  PRIVATE
    souxmar::core
    souxmar::plugin
    souxmar::pipeline
    souxmar::ai
    souxmar::warnings
    souxmar::sanitizers
    fmt::fmt
    yaml-cpp::yaml-cpp
)

install(TARGETS souxmar_eval
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

set(SOUXMAR_EVAL_BINARY "$<TARGET_FILE:souxmar_eval>"
    CACHE INTERNAL "Built souxmar-eval binary path")
