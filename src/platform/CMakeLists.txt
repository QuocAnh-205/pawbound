add_library(pb_platform STATIC
  platform.cpp
)
target_link_libraries(pb_platform PUBLIC pb_core SDL3::SDL3-static)
pb_set_warnings(pb_platform)
