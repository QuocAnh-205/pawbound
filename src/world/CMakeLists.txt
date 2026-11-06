add_library(pb_world STATIC
  world_config.cpp
  generator.cpp
  world_io.cpp
  validator.cpp
)
target_link_libraries(pb_world PUBLIC pb_core PRIVATE pb_asset)
pb_set_warnings(pb_world)
