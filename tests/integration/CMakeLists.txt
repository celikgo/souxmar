# Integration tests — exercise multi-module flows that unit tests cannot
# (plugin discovery + load + register against a real built plugin binary).
#
# Each test gets a generated config header (test_config.h) carrying CMake-
# resolved paths to in-tree plugin builds, so the test does not have to
# guess where the binary lives.

configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/test_config.h.in
  ${CMAKE_CURRENT_BINARY_DIR}/test_config.h
  @ONLY
)

add_executable(souxmar_integration_tests
  test_load_hello_mesher.cpp
)

target_include_directories(souxmar_integration_tests
  PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(souxmar_integration_tests
  PRIVATE
    souxmar::core
    souxmar::plugin
    souxmar::warnings
    souxmar::sanitizers
    GTest::gtest
    GTest::gtest_main
)

# The integration suite depends on the hello-mesher plugin being built first.
add_dependencies(souxmar_integration_tests hello_mesher)

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
