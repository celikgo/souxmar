# libsouxmar-update — auto-updater (Sprint 10 push 4 onward).
#
# Push 4: signed update manifest data model + parser + structural
# validator (manifest.cpp). Push 5 (this push): ed25519 detached-
# signature verifier wrapping libsodium (verifier.cpp). Push 6: state
# machine + CLI subcommand. Push 7: rollback log. Each push adds
# source files to this list; the public API surface lives under
# include/souxmar/update/.
#
# libsodium is scoped *only* to this target — the ABI host, the
# AI dispatcher, the pipeline runner all stay sodium-free.
# ADR-0013 § "Why ed25519, detached signature" picks the algorithm;
# ADR-0013 § "Signing key compromise" lists the risk model that
# justifies pinning to one curve.

find_package(tomlplusplus     CONFIG REQUIRED)
find_package(unofficial-sodium CONFIG REQUIRED)

add_library(souxmar_update
  manifest.cpp
  verifier.cpp
  state_machine.cpp
  update_state.cpp
)

add_library(souxmar::update ALIAS souxmar_update)

set_target_properties(souxmar_update PROPERTIES
  OUTPUT_NAME    souxmar-update
  VERSION        ${PROJECT_VERSION}
  SOVERSION      ${PROJECT_VERSION_MAJOR}
  EXPORT_NAME    update
)

target_link_libraries(souxmar_update
  PUBLIC
    souxmar::public_headers
  PRIVATE
    souxmar::warnings
    souxmar::sanitizers
    tomlplusplus::tomlplusplus
    unofficial-sodium::sodium
)

install(TARGETS souxmar_update
  EXPORT souxmar-targets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
