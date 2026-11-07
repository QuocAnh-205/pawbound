set(PB_CLIENT_SOURCES
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
  touch_controls.cpp
  android_bootstrap.cpp
)
if(ANDROID)
  # SDLActivity nạp "libmain.so" và gọi SDL_main
  add_library(pawbound_client SHARED ${PB_CLIENT_SOURCES})
  set_target_properties(pawbound_client PROPERTIES OUTPUT_NAME main)
  target_link_libraries(pawbound_client PRIVATE android log)
else()
  add_executable(pawbound_client ${PB_CLIENT_SOURCES})
endif()
target_link_libraries(pawbound_client PRIVATE pb_core pb_geom pb_platform pb_render pb_asset pb_world pb_sim pb_net)
pb_set_warnings(pawbound_client)
