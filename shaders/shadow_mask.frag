#version 450
#include "common.glsl"
layout(set = 0, binding = 2) uniform sampler2D textures[MAX_TEXTURES];
layout(location = 0) in vec2 v_uv;

void main() {
  float a = texture(textures[draw.tex.x], v_uv).a * draw.base_color.a;
  if (a < draw.extra.x) discard;
}
