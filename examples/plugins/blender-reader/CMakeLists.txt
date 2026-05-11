# Blender (.blend) reader. Built only when SOUXMAR_WITH_BLENDER=ON AND
# the `blender` binary is on $PATH at configure time. Per ADR-0009 we
# use find_program (resolving binaries on $PATH); we never link Blender's
# C API or against libblender — Blender is GPL-2.0+, so the same
# subprocess-only rule the OpenFOAM adapter follows applies here.
#
# Default builds skip this directory entirely (the option defaults
# OFF in cmake/SouxmarOptions.cmake). The always-on sibling for default
# CI is obj-reader.
#
# Links libsouxmar-plugin for the subprocess harness (Sprint 8 push 1).

find_program(SOUXMAR_BLENDER_BIN blender)

if(NOT SOUXMAR_BLENDER_BIN)
  message(STATUS
    "souxmar: blender-reader skipped — `blender` not on PATH. "
    "Install Blender 4.x and ensure `blender --version` resolves, "
    "or build with -DSOUXMAR_WITH_BLENDER=OFF (the default).")
  return()
endif()
message(STATUS "souxmar: blender-reader enabled — blender at ${SOUXMAR_BLENDER_BIN}")

souxmar_add_plugin(blender_reader
  SOURCES   blender_reader.cpp
  MANIFEST  ${CMAKE_CURRENT_SOURCE_DIR}/souxmar-plugin.toml
  CAPABILITIES "reader.blend"
)

# Subprocess harness from libsouxmar-plugin. Apache-2.0 internal C++,
# not anything from Blender; ADR-0009's no-GPL-linking rule is preserved.
target_link_libraries(blender_reader PRIVATE souxmar::plugin)

if(APPLE)
  set_target_properties(blender_reader PROPERTIES
    PREFIX "lib" SUFFIX ".dylib")
elseif(UNIX)
  set_target_properties(blender_reader PROPERTIES
    PREFIX "lib" SUFFIX ".so")
elseif(WIN32)
  set_target_properties(blender_reader PROPERTIES
    PREFIX "" SUFFIX ".dll" OUTPUT_NAME "blender_reader")
endif()

set(SOUXMAR_BLENDER_READER_DIR "$<TARGET_FILE_DIR:blender_reader>"
    CACHE INTERNAL "Built blender-reader plugin directory")
