souxmar_add_plugin(am_slicer
  SOURCES   am_slicer.cpp slicer.cpp
  MANIFEST  ${CMAKE_CURRENT_SOURCE_DIR}/souxmar-plugin.toml
  CAPABILITIES "writer.am.gcode" "writer.am.cli" "writer.am.report"
)

if(APPLE)
  set_target_properties(am_slicer PROPERTIES
    PREFIX "lib" SUFFIX ".dylib")
elseif(UNIX)
  set_target_properties(am_slicer PROPERTIES
    PREFIX "lib" SUFFIX ".so")
elseif(WIN32)
  set_target_properties(am_slicer PROPERTIES
    PREFIX "" SUFFIX ".dll" OUTPUT_NAME "am_slicer")
endif()

set(SOUXMAR_AM_SLICER_DIR "$<TARGET_FILE_DIR:am_slicer>"
    CACHE INTERNAL "Built am-slicer plugin directory")
