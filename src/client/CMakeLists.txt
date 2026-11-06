add_executable(pawbound_client
  main.cpp
  test_scene.cpp
  model_upload.cpp
  world_view.cpp
  dog_actor.cpp
  game_session.cpp
  qa_scenario.cpp
  model_library.cpp
  npc_view.cpp
  net_game.cpp
)
target_link_libraries(pawbound_client PRIVATE pb_core pb_geom pb_platform pb_render pb_asset pb_world pb_sim pb_net)
pb_set_warnings(pawbound_client)
