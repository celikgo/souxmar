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

# Sprint 10 push 8 — embedded trust store. The release pipeline
# injects the official release ed25519 public key via
# -DSOUXMAR_RELEASE_PUBKEY_HEX=... + -DSOUXMAR_RELEASE_PUBKEY_ID=...
# Non-release builds fall back to the souxmar-dev-key — a real but
# disposable keypair whose private half ships in
# scripts/release/dev-signing-key.txt for local-dev convenience.
# See docs/SECURITY.md § "Release signing key rotation".
set(SOUXMAR_RELEASE_PUBKEY_ID  "souxmar-dev-key" CACHE STRING
    "ed25519 key id of the release public key embedded in this build")
set(SOUXMAR_RELEASE_PUBKEY_HEX
    "5e1b9d2f6c4a08b3a8f0e7d9c5b2a16f4d8e0c1b3a5d7f9e1c3b5d7f9a1b3c5d"
    CACHE STRING
    "ed25519 release public key (64 lowercase hex chars)")

configure_file(
  ${CMAKE_SOURCE_DIR}/include/souxmar/update/embedded_trust.h.in
  ${CMAKE_BINARY_DIR}/generated/include/souxmar/update/embedded_trust.h
  @ONLY
)

add_library(souxmar_update
  manifest.cpp
  verifier.cpp
  state_machine.cpp
  update_state.cpp
  # Sprint 10 push 7 — install layout, rollback log, apply / rollback
  # orchestrators.
  install_layout.cpp
  rollback_log.cpp
  apply.cpp
  # Sprint 10 push 8 — embedded trust store.
  embedded_trust.cpp
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
    # Sprint 11 push 1 (ADR-0015) — crypto primitives moved to a
    # separate library. souxmar_update keeps its libsodium dep only
    # because the verifier.cpp forwarder still includes <sodium.h>'s
    # crypto_sign_VERIFYBYTES etc. via souxmar/crypto/primitives.h
    # transitive; if a future cleanup drops that transitivity, the
    # unofficial-sodium link can be dropped here.
    souxmar::crypto
    tomlplusplus::tomlplusplus
    unofficial-sodium::sodium
)

install(TARGETS souxmar_update
  EXPORT souxmar-targets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
