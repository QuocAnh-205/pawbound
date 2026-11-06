add_library(pb_geom STATIC
  mesh_data.cpp
  primitives.cpp
  image.cpp
)
target_link_libraries(pb_geom PUBLIC pb_core PRIVATE pb_stb)
pb_set_warnings(pb_geom)
