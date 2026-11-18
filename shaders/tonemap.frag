#version 450
layout(set = 0, binding = 0) uniform sampler2D hdr;
layout(push_constant) uniform TonemapPush { vec4 params; } pc;  // x: exposure, y: vignette
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

// ACES fit (Narkowicz 2015). Swapchain sRGB lo phần gamma.
vec3 aces(vec3 x) {
  const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
  return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
  vec3 c = texture(hdr, v_uv).rgb * pc.params.x;
  c = aces(c);
  // Chỉnh màu kiểu stylized: tăng bão hòa + tương phản nhẹ (ACES làm nhạt màu vùng sáng)
  float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
  c = clamp(mix(vec3(l), c, 1.22), 0.0, 1.0);
  c = clamp((c - 0.5) * 1.06 + 0.5, 0.0, 1.0);
  vec2 q = v_uv - 0.5;
  c *= 1.0 - pc.params.y * dot(q, q) * 1.6;
  out_color = vec4(c, 1.0);
}
