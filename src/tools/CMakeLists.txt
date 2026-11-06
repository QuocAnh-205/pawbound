add_executable(pawbound main.cpp world_commands.cpp net_commands.cpp)
target_link_libraries(pawbound PRIVATE pb_core pb_geom pb_asset pb_world pb_net)
pb_set_warnings(pawbound)
