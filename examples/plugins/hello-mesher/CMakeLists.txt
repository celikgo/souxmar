# hello-mesher — the canonical souxmar reference plugin.
# Built with souxmar_add_plugin(); the same macro third-party plugin
# authors use out-of-tree.

souxmar_add_plugin(hello_mesher
  SOURCES   hello_mesher.cpp
  MANIFEST  ${CMAKE_CURRENT_SOURCE_DIR}/souxmar-plugin.toml
  CAPABILITIES "mesher.tetra.hello"
)

# Make the manifest's binary `file` field portable across OSes by setting
# the OS-typical naming convention. The discovery layer is forgiving but
# the canonical extension keeps the manifest reviewable.
if(APPLE)
  set_target_properties(hello_mesher PROPERTIES
    PREFIX "lib" SUFFIX ".dylib")
elseif(UNIX)
  set_target_properties(hello_mesher PROPERTIES
    PREFIX "lib" SUFFIX ".so")
elseif(WIN32)
  set_target_properties(hello_mesher PROPERTIES
    PREFIX "" SUFFIX ".dll" OUTPUT_NAME "hello_mesher")
endif()

# Expose the built binary's directory to integration tests so they can
# point SOUXMAR_PLUGIN_PATH at it for discovery.
set(SOUXMAR_HELLO_MESHER_DIR "$<TARGET_FILE_DIR:hello_mesher>"
    CACHE INTERNAL "Built hello-mesher plugin directory")
