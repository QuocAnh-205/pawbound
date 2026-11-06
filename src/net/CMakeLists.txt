add_library(pb_net STATIC
  protocol.cpp
  ticket.cpp
  netsim.cpp
)
target_link_libraries(pb_net PUBLIC pb_core pb_enet PRIVATE pb_sodium nlohmann_json::nlohmann_json)
pb_set_warnings(pb_net)
