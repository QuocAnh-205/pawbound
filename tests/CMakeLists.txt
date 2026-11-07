file(GLOB_RECURSE PB_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
add_executable(pb_tests ${PB_TEST_SOURCES})
target_link_libraries(pb_tests PRIVATE pb_core pb_geom pb_asset pb_world pb_sim pb_net pb_server doctest::doctest)
target_compile_definitions(pb_tests PRIVATE PB_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
pb_set_warnings(pb_tests)
add_test(NAME unit COMMAND pb_tests)

add_test(NAME cli_version COMMAND pawbound --version)
set_tests_properties(cli_version PROPERTIES PASS_REGULAR_EXPRESSION "pawbound 0\\.1\\.0")
add_test(NAME cli_bad_command COMMAND pawbound khong-ton-tai)
set_tests_properties(cli_bad_command PROPERTIES WILL_FAIL TRUE)

# Server headless: cần world + khóa ticket (tạo trong test)
add_test(NAME server_keygen COMMAND pawbound net keygen --out "${CMAKE_BINARY_DIR}/dev_keys" --force yes)
add_test(NAME server_headless_ticks COMMAND pawbound_server --world "${CMAKE_BINARY_DIR}/out/world_demo"
         --data "${CMAKE_SOURCE_DIR}" --ticket-pubkey "${CMAKE_BINARY_DIR}/dev_keys/ticket_pk.b64" --ticks 30 --no-console --port 27395)
set_tests_properties(server_headless_ticks PROPERTIES DEPENDS "world_generate_demo;server_keygen")

if(PB_BUILD_CLIENT)
  # Smoke test: mở cửa sổ SDL ẩn, chạy 30 frame, thoát sạch (exit code 0).
  add_test(NAME client_window_smoke
    COMMAND pawbound_client --frames 30 --hidden --no-render --config "${CMAKE_SOURCE_DIR}/config/client.json")
  set_tests_properties(client_window_smoke PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}" TIMEOUT 30)

  # Renderer smoke test: Vulkan + validation layer, đổi kích thước/thu nhỏ cửa sổ; exit 4 nếu có lỗi validation.
  add_test(NAME renderer_validation_smoke
    COMMAND pawbound_client --frames 140 --validation --resize-test
            --screenshot "${CMAKE_BINARY_DIR}/screenshots/smoke.png" --asset-root "${CMAKE_SOURCE_DIR}"
            --config "${CMAKE_SOURCE_DIR}/config/client.json")
  set_tests_properties(renderer_validation_smoke PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}" TIMEOUT 90
    LABELS "gpu" ENVIRONMENT "VK_LAYER_PATH=$ENV{VULKAN_SDK}/Bin")
endif()

# Asset pipeline: validator chạy trên manifest thật của repo (exit 0 = sạch).
add_test(NAME asset_manifest_validate COMMAND pawbound asset validate --manifest "${CMAKE_SOURCE_DIR}/assets/manifest.json")
add_test(NAME asset_inspect_fox COMMAND pawbound asset inspect "${CMAKE_SOURCE_DIR}/assets/prototype/fox/Fox.glb")
set_tests_properties(asset_inspect_fox PROPERTIES PASS_REGULAR_EXPRESSION "skin: 24 joint")

# World generation: sinh từ seed demo bằng CLI rồi validate (exit 0 = hợp lệ).
add_test(NAME world_generate_demo COMMAND pawbound world generate --seed 20261106
         --config "${CMAKE_SOURCE_DIR}/data/world/demo.json" --output "${CMAKE_BINARY_DIR}/out/world_demo")
add_test(NAME world_validate_demo COMMAND pawbound world validate "${CMAKE_BINARY_DIR}/out/world_demo")
set_tests_properties(world_validate_demo PROPERTIES DEPENDS world_generate_demo)

if(PB_BUILD_CLIENT)
  # Streaming: client tự di chuyển 400 frame trong world demo (nạp/gỡ chunk), validation phải sạch.
  add_test(NAME world_client_streaming_smoke
    COMMAND pawbound_client --world "${CMAKE_BINARY_DIR}/out/world_demo" --frames 400 --validation --resize-test
            --asset-root "${CMAKE_SOURCE_DIR}"
            --config "${CMAKE_SOURCE_DIR}/config/client.json")
  set_tests_properties(world_client_streaming_smoke PROPERTIES DEPENDS world_generate_demo WORKING_DIRECTORY
    "${CMAKE_BINARY_DIR}" TIMEOUT 120 LABELS "gpu" ENVIRONMENT "VK_LAYER_PATH=$ENV{VULKAN_SDK}/Bin")
endif()

if(PB_BUILD_CLIENT)
  # Phase 7: kịch bản gameplay tự động trong client thật (uống, ăn, lấy đồ, dùng đồ, cắn NPC, nghỉ). Exit 5 nếu FAIL.
  add_test(NAME gameplay_survival_scenario
    COMMAND pawbound_client --world "${CMAKE_BINARY_DIR}/out/world_demo" --scenario survival --validation
            --asset-root "${CMAKE_SOURCE_DIR}" --config "${CMAKE_SOURCE_DIR}/config/client.json")
  set_tests_properties(gameplay_survival_scenario PROPERTIES DEPENDS world_generate_demo WORKING_DIRECTORY
    "${CMAKE_BINARY_DIR}" TIMEOUT 120 LABELS "gpu" ENVIRONMENT "VK_LAYER_PATH=$ENV{VULKAN_SDK}/Bin")
endif()

if(PB_BUILD_CLIENT)
  # Phase 9: bão (mưa + chớp) và sự kiện ép buộc, chạy 300 frame có validation.
  add_test(NAME worldsim_client_storm_smoke
    COMMAND pawbound_client --world "${CMAKE_BINARY_DIR}/out/world_demo" --frames 300 --validation --resize-test
            --weather storm --event lost_puppy --time 20 --asset-root "${CMAKE_SOURCE_DIR}"
            --config "${CMAKE_SOURCE_DIR}/config/client.json")
  set_tests_properties(worldsim_client_storm_smoke PROPERTIES DEPENDS world_generate_demo WORKING_DIRECTORY
    "${CMAKE_BINARY_DIR}" TIMEOUT 120 LABELS "gpu" ENVIRONMENT "VK_LAYER_PATH=$ENV{VULKAN_SDK}/Bin")
endif()

if(PB_BUILD_CLIENT)
  # Phase 10: server + bot + client thật kết nối online (UDP localhost), validation sạch.
  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_FOUND)
    add_test(NAME net_online_smoke COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/scripts/net_smoke.py"
             "${CMAKE_BINARY_DIR}/bin" "${CMAKE_BINARY_DIR}/out/world_demo" "${CMAKE_BINARY_DIR}/screenshots/online.png")
    set_tests_properties(net_online_smoke PROPERTIES DEPENDS world_generate_demo TIMEOUT 150 LABELS "gpu;net"
      ENVIRONMENT "VK_LAYER_PATH=$ENV{VULKAN_SDK}/Bin")
  endif()
endif()

# Phase 12: chat gần qua game server (bán kính, rate limit, lọc từ, không log nội dung) — không cần GPU.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME net_local_chat COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/scripts/chat_smoke.py"
           "${CMAKE_BINARY_DIR}/bin" "${CMAKE_BINARY_DIR}/out/world_demo")
  set_tests_properties(net_local_chat PROPERTIES DEPENDS world_generate_demo TIMEOUT 120 LABELS "net")
endif()
if(Python3_FOUND)
  # Phase 14: kẻ trộm cảnh báo -> bắt -> giải cứu -> thưởng, qua mạng thật
  add_test(NAME net_capture_rescue COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/scripts/capture_smoke.py"
           "${CMAKE_BINARY_DIR}/bin" "${CMAKE_BINARY_DIR}/out/world_demo")
  set_tests_properties(net_capture_rescue PROPERTIES DEPENDS world_generate_demo TIMEOUT 180 LABELS "net")
endif()
