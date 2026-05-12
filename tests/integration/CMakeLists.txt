# Integration tests — exercise multi-module flows that unit tests cannot
# (plugin discovery + load + register against a real built plugin binary).
#
# Each test gets a generated config header (test_config.h) carrying CMake-
# resolved paths to in-tree plugin builds, so the test does not have to
# guess where the binary lives.

# Sprint 10 push 6 — `souxmar update check` CLI integration test
# generates ed25519 signatures at test time via libsodium.
find_package(unofficial-sodium CONFIG REQUIRED)

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
  # Sprint 6 push 5.
  test_swap_mesher.cpp
  # Sprint 7 push 2.
  test_elasticity_stub.cpp
  # Sprint 8 push 2.
  test_cfd_stub.cpp
  # Sprint 8 push 3.
  test_obj_reader.cpp
  # Sprint 10 push 6.
  test_update_check_cli.cpp
  # Sprint 10 push 7.
  test_update_apply_rollback_cli.cpp
  # Sprint 13 push 2 — VTU consumer-conformance carry-over from
  # the Sprint 11 / 12 retro.
  test_vtu_consumer_conformance.cpp
  # Sprint 13 push 3 — libsouxmar-c-bridge C ABI smoke. The
  # regression net for the bridge surface declared in
  # include/souxmar-c-bridge/pipeline.h (ADR-0018).
  test_c_bridge_pipeline.cpp
  # Sprint 14 push 4 — provider_call surface (bridge ABI v2).
  test_c_bridge_provider.cpp
  # Sprint 15 push 4 — auto_updater_menu surface (bridge ABI v3).
  test_c_bridge_updater.cpp
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
    souxmar::update
    # Sprint 13 push 3 — libsouxmar-c-bridge smoke test calls the
    # C ABI directly; link the static archive into the integration
    # suite.
    souxmar::c_bridge
    souxmar::warnings
    souxmar::sanitizers
    # Sprint 10 push 6 — the `update check` integration test signs
    # its own manifests with libsodium so no fixture .sig file ships
    # in the tree.
    unofficial-sodium::sodium
    GTest::gtest
    GTest::gtest_main
)

# The integration suite depends on the example plugins + CLI being built first.
add_dependencies(souxmar_integration_tests
  hello_mesher hello_writer vtu_writer
  heat_solver scalar_magnitude
  mesh_quality stl_reader
  grid_mesher
  elasticity_stub
  cfd_stub
  obj_reader)
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
