# Unit tests — fast, in-process, no I/O, no plugin loading.

find_package(unofficial-sodium CONFIG REQUIRED)

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
  test_parallel_runner.cpp
  test_c_abi_handles.cpp
  test_c_abi_buffer.cpp
  test_ai_tools.cpp
  # Sprint 6 push 1.
  test_mesh_quality.cpp
  # Sprint 6 push 6.
  test_budget_config.cpp
  # Sprint 8 push 1.
  test_subprocess.cpp
  # Sprint 9 push 9.
  test_heap_accountant.cpp
  # Sprint 10 push 2.
  test_plugin_index.cpp
  # Sprint 10 push 4.
  test_update_manifest.cpp
  # Sprint 10 push 5.
  test_update_verifier.cpp
  # Sprint 10 push 6.
  test_update_state_machine.cpp
  test_update_state.cpp
  # Sprint 10 push 7.
  test_update_install_layout.cpp
  test_update_rollback_log.cpp
  test_update_apply.cpp
  # Sprint 10 push 8.
  test_update_embedded_trust.cpp
  # Sprint 10 push 9.
  test_ai_provider.cpp
)

target_link_libraries(souxmar_unit_tests
  PRIVATE
    souxmar::core
    souxmar::plugin
    souxmar::pipeline
    souxmar::ai
    souxmar::update
    souxmar::warnings
    souxmar::sanitizers
    # Sprint 10 push 5 — the verifier tests generate keypairs +
    # signatures at test time via libsodium directly; the production
    # surface (souxmar::update) keeps libsodium PRIVATE so this is the
    # only test target that pulls it in.
    unofficial-sodium::sodium
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
