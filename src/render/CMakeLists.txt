# Shader GLSL -> SPIR-V lúc build (glslc từ Vulkan SDK), nhúng vào binary dạng mảng uint32.
if(NOT Vulkan_GLSLC_EXECUTABLE)
  message(FATAL_ERROR "Không tìm thấy glslc (Vulkan SDK)")
endif()
set(PB_SHADER_SRC "${CMAKE_SOURCE_DIR}/shaders")
set(PB_SHADER_OUT "${CMAKE_CURRENT_BINARY_DIR}/shaders")
set(PB_SHADER_INCS "")
foreach(s mesh.vert mesh.frag shadow.vert tonemap.vert tonemap.frag mesh_skinned.vert shadow_skinned.vert overlay.vert overlay.frag)
  set(out "${PB_SHADER_OUT}/${s}.inc")
  add_custom_command(
    OUTPUT "${out}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${PB_SHADER_OUT}"
    COMMAND "${Vulkan_GLSLC_EXECUTABLE}" --target-env=vulkan1.3 -O -Werror -I "${PB_SHADER_SRC}"
            -mfmt=c -o "${out}" "${PB_SHADER_SRC}/${s}"
    DEPENDS "${PB_SHADER_SRC}/${s}" "${PB_SHADER_SRC}/common.glsl" "${PB_SHADER_SRC}/skin.glsl"
    COMMENT "glslc ${s}"
    VERBATIM)
  list(APPEND PB_SHADER_INCS "${out}")
endforeach()
add_custom_target(pb_shaders DEPENDS ${PB_SHADER_INCS})

add_library(pb_render STATIC
  renderer.cpp
  overlay.cpp
  vma_impl.cpp
)
add_dependencies(pb_render pb_shaders)
target_include_directories(pb_render PRIVATE "${PB_SHADER_OUT}")
target_link_libraries(pb_render PUBLIC pb_core pb_geom Vulkan::Vulkan PRIVATE SDL3::SDL3-static pb_vma pb_stb)
pb_set_warnings(pb_render)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  # Kiểu khởi tạo VkXxx{sType} cố ý zero-init phần còn lại.
  target_compile_options(pb_render PRIVATE -Wno-missing-field-initializers)
endif()
