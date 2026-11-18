#version 450
#include "common.glsl"
layout(location = 0) in vec2 v_ndc;
layout(location = 0) out vec4 out_color;

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise(vec2 p) {
  vec2 i = floor(p), f = fract(p);
  vec2 u = f * f * (3.0 - 2.0 * f);
  return mix(mix(hash(i), hash(i + vec2(1, 0)), u.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), u.x), u.y);
}
float fbm(vec2 p) {
  float s = 0.0, a = 0.5;
  for (int i = 0; i < 5; ++i) { s += a * noise(p); p = p * 2.03 + vec2(17.1, 9.3); a *= 0.5; }
  return s;
}

void main() {
  // Tia nhìn: hiệu điểm xa và gần (đúng cho cả phối cảnh lẫn trực giao)
  vec4 n = frame.inv_view_proj * vec4(v_ndc, 0.0, 1.0);
  vec4 f = frame.inv_view_proj * vec4(v_ndc, 1.0, 1.0);
  vec3 dir = normalize(f.xyz / f.w - n.xyz / n.w);
  float up = dir.y;
  vec3 zen = frame.sky_zenith.rgb, hor = frame.sky_horizon.rgb;
  vec3 col = mix(hor, zen, pow(clamp(up, 0.0, 1.0), 0.55));
  col = mix(col, hor * 0.92, clamp(-up * 6.0, 0.0, 1.0));  // dưới chân trời: hòa vào sương mù
  vec3 L = normalize(frame.sun_dir.xyz);
  float sd = max(dot(dir, L), 0.0);
  // Mặt trời + quầng sáng
  col += frame.sun_color.rgb * (pow(sd, 900.0) * 18.0 + pow(sd, 12.0) * 0.35) * clamp(L.y * 4.0 + 0.2, 0.0, 1.0);
  // Mây: chiếu lên mặt phẳng trên cao, trôi theo thời gian
  if (up > 0.0) {
    vec2 uv = dir.xz / (up + 0.08) * 0.9 + vec2(frame.params.z * 0.006, frame.params.z * 0.003);
    float cov = frame.sky_zenith.w;
    float c = smoothstep(1.0 - cov * 0.85 - 0.25, 1.0 - cov * 0.85 + 0.2, fbm(uv * 1.3));
    float shade = 0.75 + 0.25 * fbm(uv * 2.6 + 3.0);
    vec3 cloud = mix(hor, vec3(1.0), 0.85) * shade * (0.55 + 0.6 * max(frame.sun_dir.w / 3.0, 0.1)) + frame.sun_color.rgb * pow(sd, 6.0) * 0.4;
    col = mix(col, cloud, c * clamp(up * 5.0, 0.0, 1.0) * 0.9);
  }
  out_color = vec4(col, 1.0);
}
