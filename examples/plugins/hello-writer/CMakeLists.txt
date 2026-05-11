souxmar_add_plugin(hello_writer
  SOURCES   hello_writer.cpp
  MANIFEST  ${CMAKE_CURRENT_SOURCE_DIR}/souxmar-plugin.toml
  CAPABILITIES "writer.text-summary"
)

if(APPLE)
  set_target_properties(hello_writer PROPERTIES
    PREFIX "lib" SUFFIX ".dylib")
elseif(UNIX)
  set_target_properties(hello_writer PROPERTIES
    PREFIX "lib" SUFFIX ".so")
elseif(WIN32)
  set_target_properties(hello_writer PROPERTIES
    PREFIX "" SUFFIX ".dll" OUTPUT_NAME "hello_writer")
endif()

set(SOUXMAR_HELLO_WRITER_DIR "$<TARGET_FILE_DIR:hello_writer>"
    CACHE INTERNAL "Built hello-writer plugin directory")
