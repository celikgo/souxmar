# souxmar-conformance — the v1 plugin conformance suite as a runnable
# binary. Pair-built with the main `souxmar` CLI (same SOUXMAR_BUILD_CLI
# gate) since both live in the same operator workflow.

find_package(fmt CONFIG REQUIRED)
# Pulled in transitively by souxmar::pipeline (see target_link_libraries below).
find_package(yaml-cpp CONFIG REQUIRED)

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
    # This binary dlopen()s real plugins, so it must export the whole host C
    # ABI for them to resolve against, not just the parts it calls itself.
    # souxmar::core carries the souxmar_mesh_/geometry_/field_/buffer_
    # bridges; souxmar_value_* lives in libsouxmar-pipeline (with the
    # pipeline::Value implementation it needs), which is the only reason the
    # conformance harness links the pipeline library at all. Drop this and
    # every plugin that reads its input bag fails at dlopen with
    # "symbol not found ... souxmar_value_as_number". See the comment block in
    # src/pipeline/CMakeLists.txt.
    souxmar::pipeline
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
