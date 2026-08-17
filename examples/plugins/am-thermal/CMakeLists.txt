souxmar_add_plugin(am_thermal
  SOURCES   am_thermal.cpp
  MANIFEST  ${CMAKE_CURRENT_SOURCE_DIR}/souxmar-plugin.toml
  CAPABILITIES "solver.am.thermal.lpbf" "postproc.am.melt_pool"
)

if(APPLE)
  set_target_properties(am_thermal PROPERTIES
    PREFIX "lib" SUFFIX ".dylib")
elseif(UNIX)
  set_target_properties(am_thermal PROPERTIES
    PREFIX "lib" SUFFIX ".so")
elseif(WIN32)
  set_target_properties(am_thermal PROPERTIES
    PREFIX "" SUFFIX ".dll" OUTPUT_NAME "am_thermal")
endif()

set(SOUXMAR_AM_THERMAL_DIR "$<TARGET_FILE_DIR:am_thermal>"
    CACHE INTERNAL "Built am-thermal plugin directory")
