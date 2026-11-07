configure_file(version.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/generated/core/version.hpp" @ONLY)
find_package(Threads REQUIRED)

add_library(pb_core STATIC
  log.cpp
  config_reader.cpp
  client_config.cpp
  application.cpp
  job.cpp
  text.cpp
)
target_include_directories(pb_core PUBLIC
  "${CMAKE_SOURCE_DIR}/src"
  "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(pb_core PUBLIC glm::glm nlohmann_json::nlohmann_json Threads::Threads)
if(WIN32)
  target_link_libraries(pb_core PUBLIC shell32)
endif()
target_compile_definitions(pb_core PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE GLM_FORCE_RADIANS)
pb_set_warnings(pb_core)
