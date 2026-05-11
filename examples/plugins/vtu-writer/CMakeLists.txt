souxmar_add_plugin(vtu_writer
  SOURCES   vtu_writer.cpp
  MANIFEST  ${CMAKE_CURRENT_SOURCE_DIR}/souxmar-plugin.toml
  CAPABILITIES "writer.vtu"
)

if(APPLE)
  set_target_properties(vtu_writer PROPERTIES
    PREFIX "lib" SUFFIX ".dylib")
elseif(UNIX)
  set_target_properties(vtu_writer PROPERTIES
    PREFIX "lib" SUFFIX ".so")
elseif(WIN32)
  set_target_properties(vtu_writer PROPERTIES
    PREFIX "" SUFFIX ".dll" OUTPUT_NAME "vtu_writer")
endif()

set(SOUXMAR_VTU_WRITER_DIR "$<TARGET_FILE_DIR:vtu_writer>"
    CACHE INTERNAL "Built vtu-writer plugin directory")
