souxmar_add_plugin(am_distortion
  SOURCES   am_distortion.cpp
  MANIFEST  ${CMAKE_CURRENT_SOURCE_DIR}/souxmar-plugin.toml
  CAPABILITIES "solver.am.distortion.inherent_strain" "postproc.am.residual_stress"
)

if(APPLE)
  set_target_properties(am_distortion PROPERTIES
    PREFIX "lib" SUFFIX ".dylib")
elseif(UNIX)
  set_target_properties(am_distortion PROPERTIES
    PREFIX "lib" SUFFIX ".so")
elseif(WIN32)
  set_target_properties(am_distortion PROPERTIES
    PREFIX "" SUFFIX ".dll" OUTPUT_NAME "am_distortion")
endif()

set(SOUXMAR_AM_DISTORTION_DIR "$<TARGET_FILE_DIR:am_distortion>"
    CACHE INTERNAL "Built am-distortion plugin directory")
