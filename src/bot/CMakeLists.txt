add_executable(pawbound_bot main.cpp)
target_link_libraries(pawbound_bot PRIVATE pb_core pb_world pb_net pb_ticket)
pb_set_warnings(pawbound_bot)
