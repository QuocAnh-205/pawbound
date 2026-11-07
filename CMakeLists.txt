cmake_minimum_required(VERSION 3.25)
project(Pawbound VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

option(PB_BUILD_CLIENT "Build game client (needs SDL3 video + Vulkan)" ON)
option(PB_BUILD_TESTS "Build unit tests" ON)
option(PB_BUILD_SERVER "Build dedicated server, bot, CLI tools (cần libsodium)" ON)
if(ANDROID)  # Android: chỉ client (libmain.so cho SDLActivity); không server/tool/test
  set(PB_BUILD_SERVER OFF CACHE BOOL "" FORCE)
  set(PB_BUILD_TESTS OFF CACHE BOOL "" FORCE)
endif()

include(cmake/CompilerSettings.cmake)
include(cmake/Dependencies.cmake)

add_subdirectory(src)

if(PB_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
