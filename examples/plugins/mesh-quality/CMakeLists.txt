# The plugin links in include/souxmar/core/mesh_quality.h via the
# matching src/core/mesh_quality.cpp — pulled in as a source so the
# plugin remains self-contained on the C ABI surface (no link against
# libsouxmar-core, per docs/PLUGIN_SDK.md). The .cpp also compiles into
# libsouxmar-core for the in-process consumers (tests +
# query_mesh_quality agent tool); the two copies don't collide because
# the plugin's symbols are hidden-visibility per souxmar_add_plugin's
# default.
#
# mesh.cpp and field.cpp come along for the same reason. mesh_quality.cpp
# uses core::Mesh and core::Field, and "self-contained" was only ever
# true of the C ABI: on ELF and Mach-O those C++ references quietly
# resolved against the host at load time, so nothing noticed. Windows
# cannot do that — a DLL binds every symbol at link time — and the plugin
# failed with LNK2019 on Mesh::num_cells and the Field constructor.
#
# Compiling them in is what makes the comment above true on every
# platform rather than on two of them. The closure is exactly these two
# TUs, verified by resolving the plugin's undefined C++ symbols against
# libsouxmar-core's objects; adding either one alone leaves the other's
# symbols dangling. The plugin's core::Field is scratch — results reach
# the host through souxmar_field_new, not by handing a C++ object across
# the boundary — so the second copy computes and dies inside the plugin.

souxmar_add_plugin(mesh_quality
  SOURCES   mesh_quality.cpp
            ${CMAKE_SOURCE_DIR}/src/core/mesh_quality.cpp
            ${CMAKE_SOURCE_DIR}/src/core/mesh.cpp
            ${CMAKE_SOURCE_DIR}/src/core/field.cpp
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
