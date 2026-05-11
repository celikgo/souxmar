# Unit tests — fast, in-process, no I/O, no plugin loading.

add_executable(souxmar_unit_tests
  test_version.cpp
)

target_link_libraries(souxmar_unit_tests
  PRIVATE
    souxmar::core
    souxmar::warnings
    souxmar::sanitizers
    GTest::gtest
    GTest::gtest_main
)

set_target_properties(souxmar_unit_tests PROPERTIES
  FOLDER "tests/unit"
)

gtest_discover_tests(souxmar_unit_tests
  PROPERTIES
    LABELS "unit"
    TIMEOUT 30
  DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
)
