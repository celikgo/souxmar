souxmar_add_plugin(stl_reader
  SOURCES   stl_reader.cpp
  MANIFEST  ${CMAKE_CURRENT_SOURCE_DIR}/souxmar-plugin.toml
  CAPABILITIES "reader.stl"
)

if(APPLE)
  set_target_properties(stl_reader PROPERTIES
    PREFIX "lib" SUFFIX ".dylib")
elseif(UNIX)
  set_target_properties(stl_reader PROPERTIES
    PREFIX "lib" SUFFIX ".so")
elseif(WIN32)
  set_target_properties(stl_reader PROPERTIES
    PREFIX "" SUFFIX ".dll" OUTPUT_NAME "stl_reader")
endif()

set(SOUXMAR_STL_READER_DIR "$<TARGET_FILE_DIR:stl_reader>"
    CACHE INTERNAL "Built stl-reader plugin directory")
