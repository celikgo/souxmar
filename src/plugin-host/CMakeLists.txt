# libsouxmar-plugin — manifest parser, discovery walker, ABI loader, registry.
# Sprint 1 implements manifest + discovery. Loader + registry land in Sprint 2.

find_package(fmt        CONFIG REQUIRED)
find_package(tomlplusplus CONFIG REQUIRED)

add_library(souxmar_plugin
  manifest.cpp
  discovery.cpp
  registry.cpp
  loader.cpp
  guard.cpp
  conformance.cpp
  # Sprint 8 push 1 — subprocess harness for OpenFOAM + Blender + future
  # GPL-isolated adapters.
  subprocess.cpp
  # Sprint 9 push 9 — per-plugin heap accounting (mallinfo2 on glibc).
  heap_accountant.cpp
  # Sprint 10 push 2 — plugin-index data model + search.
  index.cpp
)

# POSIX needs libdl for dlopen(); Windows uses kernel32 implicitly.
if(NOT WIN32)
  list(APPEND _SOUXMAR_PLUGIN_HOST_PRIVATE_LIBS ${CMAKE_DL_LIBS})
endif()
add_library(souxmar::plugin ALIAS souxmar_plugin)

set_target_properties(souxmar_plugin PROPERTIES
  OUTPUT_NAME    souxmar-plugin
  VERSION        ${PROJECT_VERSION}
  SOVERSION      ${PROJECT_VERSION_MAJOR}
  EXPORT_NAME    plugin
)

target_link_libraries(souxmar_plugin
  PUBLIC
    souxmar::public_headers
  PRIVATE
    souxmar::warnings
    souxmar::sanitizers
    fmt::fmt
    tomlplusplus::tomlplusplus
    ${_SOUXMAR_PLUGIN_HOST_PRIVATE_LIBS}
)

install(TARGETS souxmar_plugin
  EXPORT souxmar-targets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
