# libsouxmar-c-bridge — first real FFI surface for the desktop
# Rust ⇄ C++ boundary. Sprint 13 push 3.
#
# Built as a *static* library (libsouxmar-c-bridge.a) so the Rust
# souxmar-bridge crate can link it statically into the Tauri shell
# without dragging shared-object dependency-resolution rules into
# the desktop installer's distribution story. The library re-
# exports the libsouxmar-pipeline symbols it needs transitively
# via the C ABI declared in include/souxmar-c-bridge/pipeline.h.
#
# Stability of the C ABI is governed by ADR-0016 (BridgeFeatureSet
# contract); the function-level surface adds the version-byte
# check via souxmar_bridge_abi_version().

add_library(souxmar_c_bridge STATIC
  pipeline.cpp
  # Sprint 14 push 4 — second real FFI surface: provider_call.
  provider.cpp
)

add_library(souxmar::c_bridge ALIAS souxmar_c_bridge)

set_target_properties(souxmar_c_bridge PROPERTIES
  OUTPUT_NAME    souxmar-c-bridge
  POSITION_INDEPENDENT_CODE ON       # the Rust side links into a
                                     # PIE binary; static archives
                                     # need PIC on Linux x86_64.
  EXPORT_NAME    c_bridge
)

target_link_libraries(souxmar_c_bridge
  PUBLIC
    souxmar::public_headers
  PRIVATE
    souxmar::pipeline
    # Sprint 14 push 4 — provider.cpp routes through the
    # engine's Provider abstraction (libsouxmar-ai).
    souxmar::ai
    souxmar::warnings
    souxmar::sanitizers
)

# Exported header set so consumers (today only the Rust bridge
# build.rs; tomorrow the cbindgen-generated FFI mirror) pick up
# `souxmar-c-bridge/pipeline.h` from a single source of truth.
target_include_directories(souxmar_c_bridge
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

install(TARGETS souxmar_c_bridge
  EXPORT souxmar-targets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/souxmar-c-bridge
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
