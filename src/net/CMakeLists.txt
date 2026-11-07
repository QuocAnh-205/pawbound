add_library(pb_net STATIC
  protocol.cpp
  netsim.cpp
  http_client.cpp
)
target_link_libraries(pb_net PUBLIC pb_core pb_enet $<$<PLATFORM_ID:Windows>:ws2_32> PRIVATE nlohmann_json::nlohmann_json)
pb_set_warnings(pb_net)

# Ticket Ed25519 (libsodium): chỉ server/tool/test. Client chỉ chuyển ticket dạng chuỗi, không cần crypto.
if(PB_BUILD_SERVER OR PB_BUILD_TESTS)
  add_library(pb_ticket STATIC ticket.cpp)
  target_link_libraries(pb_ticket PUBLIC pb_net PRIVATE pb_sodium nlohmann_json::nlohmann_json)
  pb_set_warnings(pb_ticket)
endif()
