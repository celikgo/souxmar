# libsouxmar-crypto — shared cryptographic primitives (Sprint 11 push 1).
#
# Extracted from libsouxmar-update; ADR-0015 ratifies the split. The
# auto-updater (souxmar_update) is the first consumer; plugin
# marketplace v1 (Sprint 16) + cloud sync (Sprint 15) will follow.

include(SouxmarFindSodium)

add_library(souxmar_crypto
  primitives.cpp
)

add_library(souxmar::crypto ALIAS souxmar_crypto)

set_target_properties(souxmar_crypto PROPERTIES
  OUTPUT_NAME    souxmar-crypto
  VERSION        ${PROJECT_VERSION}
  SOVERSION      ${PROJECT_VERSION_MAJOR}
  EXPORT_NAME    crypto
)

target_link_libraries(souxmar_crypto
  PUBLIC
    souxmar::public_headers
  PRIVATE
    souxmar::warnings
    souxmar::sanitizers
    souxmar::sodium
)

install(TARGETS souxmar_crypto
  EXPORT souxmar-targets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
