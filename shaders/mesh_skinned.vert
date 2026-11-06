#version 450
#include "common.glsl"
#include "skin.glsl"

layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
layout(location = 3) in vec4 in_tangent;
layout(location = 4) in vec4 in_color;

layout(location = 0) out vec3 v_world;
layout(location = 1) out vec3 v_normal;
layout(location = 2) out vec2 v_uv;
layout(location = 3) out vec4 v_tangent;
layout(location = 4) out vec4 v_color;

void main() {
  mat4 m = draw.model * skin_matrix();
  vec4 world = m * vec4(in_pos, 1.0);
  mat3 nm = transpose(inverse(mat3(m)));
  v_world = world.xyz;
  v_normal = normalize(nm * in_normal);
  v_tangent = vec4(normalize(mat3(m) * in_tangent.xyz), in_tangent.w);
  v_uv = in_uv;
  v_color = in_color;
  gl_Position = frame.view_proj * world;
}
