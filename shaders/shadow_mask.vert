#version 450
#include "common.glsl"
// Bóng đổ cho vật liệu cắt alpha (lá, cỏ): cần UV để lấy alpha; lay theo gió giống pass màu.
layout(location = 0) in vec3 in_pos;
layout(location = 2) in vec2 in_uv;
layout(location = 4) in vec4 in_color;
layout(location = 0) out vec2 v_uv;

void main() {
  vec4 world = draw.model * vec4(in_pos, 1.0);
  if ((draw.tex.w & FLAG_WIND) != 0u) world.xyz += wind_offset(world.xyz, in_color.a);
  v_uv = in_uv;
  gl_Position = frame.light_view_proj * world;
}
