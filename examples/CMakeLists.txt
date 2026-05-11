# Examples build only when SOUXMAR_BUILD_EXAMPLES=ON.
# Plugin examples use the souxmar_add_plugin CMake macro.

include(SouxmarPlugin)

add_subdirectory(plugins/hello-mesher)
add_subdirectory(plugins/hello-writer)
add_subdirectory(plugins/vtu-writer)
add_subdirectory(plugins/heat-solver)
add_subdirectory(plugins/scalar-magnitude)
add_subdirectory(plugins/mesh-quality)
