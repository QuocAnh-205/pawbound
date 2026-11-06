add_library(pb_net STATIC
  protocol.cpp
  ticket.cpp
  netsim.cpp
  http_client.cpp
)
target_link_libraries(pb_net PUBLIC pb_core pb_enet $<$<PLATFORM_ID:Windows>:ws2_32> PRIVATE pb_sodium nlohmann_json::nlohmann_json)
pb_set_warnings(pb_net)
