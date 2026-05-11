souxmar_add_plugin(elasticity_stub
  SOURCES   elasticity_stub.cpp
  MANIFEST  ${CMAKE_CURRENT_SOURCE_DIR}/souxmar-plugin.toml
  CAPABILITIES "solver.elasticity.linear"
)

if(APPLE)
  set_target_properties(elasticity_stub PROPERTIES
    PREFIX "lib" SUFFIX ".dylib")
elseif(UNIX)
  set_target_properties(elasticity_stub PROPERTIES
    PREFIX "lib" SUFFIX ".so")
elseif(WIN32)
  set_target_properties(elasticity_stub PROPERTIES
    PREFIX "" SUFFIX ".dll" OUTPUT_NAME "elasticity_stub")
endif()

set(SOUXMAR_ELASTICITY_STUB_DIR "$<TARGET_FILE_DIR:elasticity_stub>"
    CACHE INTERNAL "Built elasticity-stub plugin directory")
