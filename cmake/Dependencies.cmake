# Mọi dependency được pin theo tag + SHA256. Archive cache trong .deps-cache/archives (ổ D, không commit).
# Bảng license: docs/operations/dependencies.md
include(FetchContent)
set(PB_ARCHIVE_DIR "${CMAKE_SOURCE_DIR}/.deps-cache/archives")

function(pb_declare name file url sha256)
  FetchContent_Declare(${name}
    URL "${url}"
    URL_HASH SHA256=${sha256}
    DOWNLOAD_DIR "${PB_ARCHIVE_DIR}"
    DOWNLOAD_NAME "${file}"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM)
endfunction()

pb_declare(glm "glm-1.0.3.tar.gz"
  "https://github.com/g-truc/glm/archive/refs/tags/1.0.3.tar.gz"
  6775e47231a446fd086d660ecc18bcd076531cfedd912fbd66e576b118607001)
pb_declare(nlohmann_json "json-3.12.0.tar.gz"
  "https://github.com/nlohmann/json/archive/refs/tags/v3.12.0.tar.gz"
  4b92eb0c06d10683f7447ce9406cb97cd4b453be18d7279320f7b2f025c10187)
pb_declare(doctest "doctest-2.5.3.tar.gz"
  "https://github.com/doctest/doctest/archive/refs/tags/v2.5.3.tar.gz"
  174ebc4e769928959614789c5b4e9c3d0a0f81a62bb608756b127bfebfb21331)

set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
set(DOCTEST_WITH_TESTS OFF CACHE BOOL "" FORCE)
set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glm nlohmann_json doctest)

# Thư viện header-only/single-file: chỉ tải source, PAWBOUND tự tạo target (không chạy CMake của họ).
function(pb_declare_sources name file url sha256)
  FetchContent_Declare(${name}
    URL "${url}"
    URL_HASH SHA256=${sha256}
    DOWNLOAD_DIR "${PB_ARCHIVE_DIR}"
    DOWNLOAD_NAME "${file}"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR "__pawbound_no_cmake__")
endfunction()

pb_declare_sources(stb "stb-d14d46f6b444.tar.gz"
  "https://github.com/nothings/stb/archive/d14d46f6b44456eb88beac58cba833a4135d4c89.tar.gz"
  1b59a668a789a28a8fd4e01ff1413eec7eaeb28688bafdc66ec7068f4e7095e1)
FetchContent_MakeAvailable(stb)
add_library(pb_stb INTERFACE)
target_include_directories(pb_stb SYSTEM INTERFACE "${stb_SOURCE_DIR}")

pb_declare_sources(cgltf "cgltf-1.15.tar.gz"
  "https://github.com/jkuhlmann/cgltf/archive/refs/tags/v1.15.tar.gz"
  84e352092e5cd6aab7f66de62ddb66beb5e6f18d412ca9d12950d7a55bfef25a)
FetchContent_MakeAvailable(cgltf)
add_library(pb_cgltf INTERFACE)
target_include_directories(pb_cgltf SYSTEM INTERFACE "${cgltf_SOURCE_DIR}")

# ENet 1.3.18 (MIT): build tĩnh từ source.
pb_declare_sources(enet "enet-1.3.18.tar.gz"
  "https://github.com/lsalzman/enet/archive/refs/tags/v1.3.18.tar.gz"
  28603c895f9ed24a846478180ee72c7376b39b4bb1287b73877e5eae7d96b0dd)
FetchContent_MakeAvailable(enet)
add_library(pb_enet STATIC
  ${enet_SOURCE_DIR}/callbacks.c ${enet_SOURCE_DIR}/compress.c ${enet_SOURCE_DIR}/host.c
  ${enet_SOURCE_DIR}/list.c ${enet_SOURCE_DIR}/packet.c ${enet_SOURCE_DIR}/peer.c
  ${enet_SOURCE_DIR}/protocol.c ${enet_SOURCE_DIR}/unix.c ${enet_SOURCE_DIR}/win32.c)
target_include_directories(pb_enet SYSTEM PUBLIC "${enet_SOURCE_DIR}/include")
if(WIN32)
  target_link_libraries(pb_enet PUBLIC ws2_32 winmm)
else()
  target_compile_definitions(pb_enet PRIVATE HAS_SOCKLEN_T=1)
endif()

# libsodium 1.0.22 (ISC) — chỉ dùng để XÁC MINH chữ ký Ed25519 của session ticket (không tự viết crypto).
# Windows/MinGW: gói dựng sẵn chính thức; chữ ký minisign đã xác minh bằng tools/verify_minisign.js.
# Linux: dùng libsodium-dev của hệ thống.
if(MINGW)
  pb_declare_sources(sodium "libsodium-1.0.22-mingw.tar.gz"
    "https://github.com/jedisct1/libsodium/releases/download/1.0.22-RELEASE/libsodium-1.0.22-mingw.tar.gz"
    1d99e0afaf27bce664249232e9dc628ae6bb7b49f0ab53ad6db372b028a35d9d)
  FetchContent_MakeAvailable(sodium)
  add_library(pb_sodium STATIC IMPORTED GLOBAL)
  set_target_properties(pb_sodium PROPERTIES
    IMPORTED_LOCATION "${sodium_SOURCE_DIR}/libsodium-win64/lib/libsodium.a"
    INTERFACE_INCLUDE_DIRECTORIES "${sodium_SOURCE_DIR}/libsodium-win64/include"
    INTERFACE_COMPILE_DEFINITIONS "SODIUM_STATIC=1")
else()
  find_library(PB_SODIUM_LIB sodium REQUIRED)
  find_path(PB_SODIUM_INC sodium.h REQUIRED)
  add_library(pb_sodium INTERFACE)
  target_link_libraries(pb_sodium INTERFACE ${PB_SODIUM_LIB})
  target_include_directories(pb_sodium INTERFACE ${PB_SODIUM_INC})
endif()

if(PB_BUILD_CLIENT)
  pb_declare_sources(vma "vma-v3.5.0.tar.gz"
    "https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/archive/refs/tags/v3.5.0.tar.gz"
    c5f3db848fca9cb9726b41f256cc3007773321fa92a86d1e0902e66474e8a9e3)
  FetchContent_MakeAvailable(vma)
  add_library(pb_vma INTERFACE)
  target_include_directories(pb_vma SYSTEM INTERFACE "${vma_SOURCE_DIR}/include")

  pb_declare(SDL3 "SDL3-3.4.20.tar.gz"
    "https://github.com/libsdl-org/SDL/archive/refs/tags/release-3.4.20.tar.gz"
    5cb56d4223a41283f3c2baf6f1263fba171692409ae405a50417a7c8e7e28deb)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(SDL3)
  find_package(Vulkan REQUIRED)
endif()
