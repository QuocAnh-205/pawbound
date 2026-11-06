#version 450
#include "common.glsl"

layout(location = 0) in vec3 in_pos;

void main() { gl_Position = frame.light_view_proj * draw.model * vec4(in_pos, 1.0); }
