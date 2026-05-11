# libsouxmar-update — auto-updater (Sprint 10 push 4 onward).
#
# Push 4 (this push): signed update manifest data model + parser +
# structural validator. The verifier (libsodium ed25519) lands in
# push 5; the update state machine + CLI subcommand in push 6;
# rollback log in push 7. Each push adds source files to this list;
# the public API surface lives under include/souxmar/update/.

find_package(tomlplusplus CONFIG REQUIRED)

add_library(souxmar_update
  manifest.cpp
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
)

install(TARGETS souxmar_update
  EXPORT souxmar-targets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
