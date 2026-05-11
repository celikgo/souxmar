# Integration tests — exercise multi-module flows that unit tests cannot
# (plugin discovery + load + register against a real built plugin binary).
#
# Each test gets a generated config header (test_config.h) carrying CMake-
# resolved paths to in-tree plugin builds, so the test does not have to
# guess where the binary lives.

# Two-pass generation: configure_file substitutes @CACHE_VAR@ tokens but does
# not evaluate $<TARGET_FILE:...> generator expressions; file(GENERATE) does
# the opposite. The cache vars (SOUXMAR_*_DIR, SOUXMAR_CLI_BINARY) themselves
# hold generator expressions, so we need both passes to land a fully-resolved
# header at build time.
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/test_config.h.in
  ${CMAKE_CURRENT_BINARY_DIR}/test_config.h.tmp
  @ONLY
)
file(GENERATE
  OUTPUT  ${CMAKE_CURRENT_BINARY_DIR}/test_config.h
  INPUT   ${CMAKE_CURRENT_BINARY_DIR}/test_config.h.tmp
)

add_executable(souxmar_integration_tests
  test_load_hello_mesher.cpp
  test_pipeline_end_to_end.cpp
  test_cli_smoke.cpp
  test_conformance.cpp
  test_postproc_end_to_end.cpp
  # Sprint 6 push 1.
  test_mesh_quality_plugin.cpp
  # Sprint 6 push 4.
  test_reader_end_to_end.cpp
)

target_include_directories(souxmar_integration_tests
  PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(souxmar_integration_tests
  PRIVATE
    souxmar::core
    souxmar::plugin
    souxmar::pipeline
    souxmar::warnings
    souxmar::sanitizers
    GTest::gtest
    GTest::gtest_main
)

# The integration suite depends on the example plugins + CLI being built first.
add_dependencies(souxmar_integration_tests
  hello_mesher hello_writer vtu_writer
  heat_solver scalar_magnitude
  mesh_quality stl_reader)
if(SOUXMAR_BUILD_CLI AND TARGET souxmar_cli)
  add_dependencies(souxmar_integration_tests souxmar_cli)
endif()

set_target_properties(souxmar_integration_tests PROPERTIES
  FOLDER "tests/integration"
)

gtest_discover_tests(souxmar_integration_tests
  PROPERTIES
    LABELS "integration"
    TIMEOUT 60
  DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
)
