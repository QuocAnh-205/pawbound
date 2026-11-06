add_library(pb_asset STATIC
  sha256.cpp
  animation.cpp
  gltf_loader.cpp
  glb_writer.cpp
  manifest.cpp
  placeholders.cpp
)
target_link_libraries(pb_asset PUBLIC pb_core pb_geom PRIVATE pb_cgltf)
pb_set_warnings(pb_asset)
