souxmar_add_plugin(grid_mesher
  SOURCES   grid_mesher.cpp
  MANIFEST  ${CMAKE_CURRENT_SOURCE_DIR}/souxmar-plugin.toml
  CAPABILITIES "mesher.tetra.grid"
)

if(APPLE)
  set_target_properties(grid_mesher PROPERTIES
    PREFIX "lib" SUFFIX ".dylib")
elseif(UNIX)
  set_target_properties(grid_mesher PROPERTIES
    PREFIX "lib" SUFFIX ".so")
elseif(WIN32)
  set_target_properties(grid_mesher PROPERTIES
    PREFIX "" SUFFIX ".dll" OUTPUT_NAME "grid_mesher")
endif()

set(SOUXMAR_GRID_MESHER_DIR "$<TARGET_FILE_DIR:grid_mesher>"
    CACHE INTERNAL "Built grid-mesher plugin directory")
