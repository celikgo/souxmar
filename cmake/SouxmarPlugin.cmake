# souxmar_add_plugin — declare a souxmar plugin target.
#
# Usable both in-tree (e.g. examples/plugins/hello-mesher) and out-of-tree
# (in a third-party plugin author's repo, after find_package(souxmar)).
# Bakes in the conventions from docs/PLUGIN_SDK.md so plugin authors get
# the contract (single exported symbol, hidden visibility, manifest beside
# the binary) right by default.
#
# Usage:
#   souxmar_add_plugin(my_mesher
#     SOURCES        src/my_mesher.cpp
#     MANIFEST       souxmar-plugin.toml          # optional; defaults to
#                                                 # ${CMAKE_CURRENT_SOURCE_DIR}/souxmar-plugin.toml
#     CAPABILITIES   "mesher.tetra.example"      # documentation hint;
#                                                 # source of truth is the manifest
#   )

include_guard(GLOBAL)

function(souxmar_add_plugin TARGET_NAME)
  cmake_parse_arguments(
    SXP
    ""
    "MANIFEST;DESTINATION"
    "SOURCES;CAPABILITIES"
    ${ARGN}
  )

  if(NOT SXP_SOURCES)
    message(FATAL_ERROR "souxmar_add_plugin(${TARGET_NAME}): SOURCES is required")
  endif()
  if(NOT SXP_MANIFEST)
    set(SXP_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/souxmar-plugin.toml")
  endif()
  if(NOT EXISTS "${SXP_MANIFEST}")
    message(FATAL_ERROR
      "souxmar_add_plugin(${TARGET_NAME}): manifest '${SXP_MANIFEST}' not found")
  endif()

  add_library(${TARGET_NAME} SHARED ${SXP_SOURCES})

  # Link the public headers (souxmar-c/*) without pulling in any souxmar
  # implementation libraries — the plugin must be self-contained on the host's
  # ABI surface only.
  if(TARGET souxmar::public_headers)
    target_link_libraries(${TARGET_NAME} PRIVATE souxmar::public_headers)
  endif()

  target_compile_definitions(${TARGET_NAME} PRIVATE SOUXMAR_BUILD_PLUGIN)

  set_target_properties(${TARGET_NAME} PROPERTIES
    CXX_VISIBILITY_PRESET    hidden
    C_VISIBILITY_PRESET      hidden
    VISIBILITY_INLINES_HIDDEN ON
    POSITION_INDEPENDENT_CODE ON
    FOLDER                   "plugins"
  )

  # Host-ABI symbols (souxmar_mesh_*, souxmar_field_*, souxmar_registry_*,
  # ...) are *not* linked into the plugin — they live in libsouxmar-core,
  # which is linked into whichever executable dlopens the plugin (the
  # `souxmar` CLI, the test runner, the Tauri bridge). At plugin-build
  # time these symbols are deliberately undefined; the loader resolves
  # them against the host process's symbol table at dlopen().
  #
  # Linux's default `ld` already permits undefined symbols in SHARED
  # objects, so no flag is needed there. macOS's `ld` is strict by
  # default and requires opt-in via `-undefined dynamic_lookup`.
  # Windows is unsupported by this path (plugins on Windows need an
  # import library against the host executable, which the project does
  # not yet ship; out-of-tree Windows plugins should link against
  # souxmar-core.dll's .lib explicitly).
  if(APPLE)
    target_link_options(${TARGET_NAME} PRIVATE
      "LINKER:-undefined,dynamic_lookup")
  endif()

  # Keep the manifest beside the built binary so discovery finds them
  # together when SOUXMAR_PLUGIN_PATH points at the build directory.
  #
  # Note on BYPRODUCTS: an earlier revision declared the copied manifest
  # as a BYPRODUCTS entry containing $<TARGET_FILE_DIR:${TARGET_NAME}>.
  # On CMake >= 3.30 that path evaluates the generator expression in a
  # scope where the just-created target isn't yet resolvable, producing
  # "No target X" at generation. The COMMAND argument has no such
  # constraint, so we inline the gen-expression there and skip
  # BYPRODUCTS — the only consequence is that `ninja clean` won't
  # delete the copied manifest, which is harmless (the target rebuild
  # always re-copies it via copy_if_different).
  add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SXP_MANIFEST}"
            "$<TARGET_FILE_DIR:${TARGET_NAME}>/souxmar-plugin.toml"
    COMMENT "souxmar: copying manifest for plugin '${TARGET_NAME}'"
    VERBATIM
  )

  # Install: place binary + manifest in a per-plugin subdirectory under the
  # platform's plugins prefix. Skipped for in-tree examples (no DESTINATION).
  if(SXP_DESTINATION)
    install(TARGETS ${TARGET_NAME}
      LIBRARY DESTINATION "${SXP_DESTINATION}/${TARGET_NAME}"
      RUNTIME DESTINATION "${SXP_DESTINATION}/${TARGET_NAME}"
    )
    install(FILES "${SXP_MANIFEST}"
      DESTINATION "${SXP_DESTINATION}/${TARGET_NAME}"
    )
  endif()

  # Stash the announced capabilities as a target property; useful for
  # diagnostics, conformance tooling, and the future plugin-marketplace
  # publish step.
  if(SXP_CAPABILITIES)
    set_property(TARGET ${TARGET_NAME}
      PROPERTY SOUXMAR_PLUGIN_CAPABILITIES "${SXP_CAPABILITIES}")
  endif()
endfunction()
