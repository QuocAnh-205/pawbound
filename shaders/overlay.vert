#version 450
layout(push_constant) uniform OverlayPush { vec2 screen; } pc;
layout(location = 0) in vec2 in_pos;  // pixel, gốc trên-trái
layout(location = 1) in vec4 in_color;
layout(location = 0) out vec4 v_color;
void main() {
  v_color = in_color;
  gl_Position = vec4(in_pos / pc.screen * 2.0 - 1.0, 0.0, 1.0);
}
