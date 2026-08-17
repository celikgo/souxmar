souxmar_add_plugin(am_layered_mesher
  SOURCES   am_layered_mesher.cpp
  MANIFEST  ${CMAKE_CURRENT_SOURCE_DIR}/souxmar-plugin.toml
  CAPABILITIES "mesher.am.layered"
)

if(APPLE)
  set_target_properties(am_layered_mesher PROPERTIES
    PREFIX "lib" SUFFIX ".dylib")
elseif(UNIX)
  set_target_properties(am_layered_mesher PROPERTIES
    PREFIX "lib" SUFFIX ".so")
elseif(WIN32)
  set_target_properties(am_layered_mesher PROPERTIES
    PREFIX "" SUFFIX ".dll" OUTPUT_NAME "am_layered_mesher")
endif()

set(SOUXMAR_AM_LAYERED_MESHER_DIR "$<TARGET_FILE_DIR:am_layered_mesher>"
    CACHE INTERNAL "Built am-layered-mesher plugin directory")
