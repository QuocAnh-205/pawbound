add_library(pb_sim STATIC
  collision.cpp
  dog_archetype.cpp
  dog_motor.cpp
  anim_graph.cpp
  input_mapping.cpp
  gameplay.cpp
  nav.cpp
  npc.cpp
  world_sim.cpp
  advanced.cpp
)
target_link_libraries(pb_sim PUBLIC pb_core pb_world)
pb_set_warnings(pb_sim)
