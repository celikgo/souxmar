souxmar_add_plugin(scalar_magnitude
  SOURCES   scalar_magnitude.cpp
  MANIFEST  ${CMAKE_CURRENT_SOURCE_DIR}/souxmar-plugin.toml
  CAPABILITIES "postproc.scalar_magnitude"
)

if(APPLE)
  set_target_properties(scalar_magnitude PROPERTIES
    PREFIX "lib" SUFFIX ".dylib")
elseif(UNIX)
  set_target_properties(scalar_magnitude PROPERTIES
    PREFIX "lib" SUFFIX ".so")
elseif(WIN32)
  set_target_properties(scalar_magnitude PROPERTIES
    PREFIX "" SUFFIX ".dll" OUTPUT_NAME "scalar_magnitude")
endif()

set(SOUXMAR_SCALAR_MAGNITUDE_DIR "$<TARGET_FILE_DIR:scalar_magnitude>"
    CACHE INTERNAL "Built scalar-magnitude plugin directory")
