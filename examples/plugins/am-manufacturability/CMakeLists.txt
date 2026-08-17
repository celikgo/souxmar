souxmar_add_plugin(am_manufacturability
  SOURCES   am_manufacturability.cpp boundary_faces.cpp
  MANIFEST  ${CMAKE_CURRENT_SOURCE_DIR}/souxmar-plugin.toml
  CAPABILITIES "solver.am.overhang" "solver.am.printability" "solver.am.buildtime"
)

if(APPLE)
  set_target_properties(am_manufacturability PROPERTIES
    PREFIX "lib" SUFFIX ".dylib")
elseif(UNIX)
  set_target_properties(am_manufacturability PROPERTIES
    PREFIX "lib" SUFFIX ".so")
elseif(WIN32)
  set_target_properties(am_manufacturability PROPERTIES
    PREFIX "" SUFFIX ".dll" OUTPUT_NAME "am_manufacturability")
endif()

set(SOUXMAR_AM_MANUFACTURABILITY_DIR "$<TARGET_FILE_DIR:am_manufacturability>"
    CACHE INTERNAL "Built am-manufacturability plugin directory")
