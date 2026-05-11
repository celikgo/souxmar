souxmar_add_plugin(obj_reader
  SOURCES   obj_reader.cpp
  MANIFEST  ${CMAKE_CURRENT_SOURCE_DIR}/souxmar-plugin.toml
  CAPABILITIES "reader.obj"
)

if(APPLE)
  set_target_properties(obj_reader PROPERTIES
    PREFIX "lib" SUFFIX ".dylib")
elseif(UNIX)
  set_target_properties(obj_reader PROPERTIES
    PREFIX "lib" SUFFIX ".so")
elseif(WIN32)
  set_target_properties(obj_reader PROPERTIES
    PREFIX "" SUFFIX ".dll" OUTPUT_NAME "obj_reader")
endif()

set(SOUXMAR_OBJ_READER_DIR "$<TARGET_FILE_DIR:obj_reader>"
    CACHE INTERNAL "Built obj-reader plugin directory")
