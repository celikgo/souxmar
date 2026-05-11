# Unit tests — fast, in-process, no I/O, no plugin loading.

add_executable(souxmar_unit_tests
  test_version.cpp
  test_tag.cpp
  test_element_type.cpp
  test_geometry.cpp
  test_topology.cpp
  test_mesh.cpp
  test_field.cpp
  test_manifest.cpp
  test_discovery.cpp
  test_registry.cpp
  test_guard.cpp
  test_pipeline_value.cpp
  test_pipeline_parser.cpp
  test_pipeline_dag.cpp
  test_pipeline_cache.cpp
  test_pipeline_runner.cpp
)

target_link_libraries(souxmar_unit_tests
  PRIVATE
    souxmar::core
    souxmar::plugin
    souxmar::pipeline
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
