# The plugin links in include/souxmar/core/mesh_quality.h via the
# matching src/core/mesh_quality.cpp — pulled in as a second source
# so the plugin remains self-contained on the C ABI surface (no link
# against libsouxmar-core, per docs/PLUGIN_SDK.md). The .cpp also
# compiles into libsouxmar-core for the in-process consumers (tests +
# query_mesh_quality agent tool); the two copies don't collide
# because the plugin's symbols are hidden-visibility per
# souxmar_add_plugin's default.

souxmar_add_plugin(mesh_quality
  SOURCES   mesh_quality.cpp
            ${CMAKE_SOURCE_DIR}/src/core/mesh_quality.cpp
  MANIFEST  ${CMAKE_CURRENT_SOURCE_DIR}/souxmar-plugin.toml
  CAPABILITIES "postproc.mesh_quality"
)

if(APPLE)
  set_target_properties(mesh_quality PROPERTIES
    PREFIX "lib" SUFFIX ".dylib")
elseif(UNIX)
  set_target_properties(mesh_quality PROPERTIES
    PREFIX "lib" SUFFIX ".so")
elseif(WIN32)
  set_target_properties(mesh_quality PROPERTIES
    PREFIX "" SUFFIX ".dll" OUTPUT_NAME "mesh_quality")
endif()

set(SOUXMAR_MESH_QUALITY_DIR "$<TARGET_FILE_DIR:mesh_quality>"
    CACHE INTERNAL "Built mesh-quality plugin directory")
