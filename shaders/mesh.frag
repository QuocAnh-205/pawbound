#version 450
#include "common.glsl"

layout(set = 0, binding = 1) uniform sampler2DShadow shadow_map;
layout(set = 0, binding = 2) uniform sampler2D textures[MAX_TEXTURES];

layout(location = 0) in vec3 v_world;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec2 v_uv;
layout(location = 3) in vec4 v_tangent;
layout(location = 4) in vec4 v_color;

layout(location = 0) out vec4 out_color;

const float PI = 3.14159265359;

// GGX / Smith-Schlick / Fresnel-Schlick — PBR metallic-roughness theo glTF.
float d_ggx(float NdotH, float a) {
  float a2 = a * a;
  float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
  return a2 / (PI * d * d);
}
float g_smith(float NdotV, float NdotL, float rough) {
  float k = (rough + 1.0) * (rough + 1.0) / 8.0;
  float gv = NdotV / (NdotV * (1.0 - k) + k);
  float gl = NdotL / (NdotL * (1.0 - k) + k);
  return gv * gl;
}
vec3 f_schlick(float VdotH, vec3 f0) { return f0 + (1.0 - f0) * pow(1.0 - VdotH, 5.0); }

vec3 brdf(vec3 N, vec3 V, vec3 L, vec3 albedo, float metallic, float rough, vec3 radiance) {
  vec3 H = normalize(V + L);
  float NdotL = max(dot(N, L), 0.0);
  if (NdotL <= 0.0) return vec3(0.0);
  float NdotV = max(dot(N, V), 1e-4);
  float NdotH = max(dot(N, H), 0.0);
  float VdotH = max(dot(V, H), 0.0);
  vec3 f0 = mix(vec3(0.04), albedo, metallic);
  vec3 F = f_schlick(VdotH, f0);
  float a = max(rough * rough, 0.002);
  vec3 spec = d_ggx(NdotH, a) * g_smith(NdotV, NdotL, rough) * F / max(4.0 * NdotV * NdotL, 1e-4);
  vec3 kd = (1.0 - F) * (1.0 - metallic);
  return (kd * albedo / PI + spec) * radiance * NdotL;
}

float shadow_factor(vec3 world, vec3 N) {
  vec3 L = frame.sun_dir.xyz;
  // normal offset giảm shadow acne
  vec4 lp = frame.light_view_proj * vec4(world + N * 0.04, 1.0);
  vec3 p = lp.xyz / lp.w;
  vec2 uv = p.xy * 0.5 + 0.5;
  if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || p.z > 1.0) return 1.0;
  float texel = frame.params.y;
  float sum = 0.0;
  for (int y = -1; y <= 1; ++y)
    for (int x = -1; x <= 1; ++x) sum += texture(shadow_map, vec3(uv + vec2(x, y) * texel, p.z - 0.0015));
  return sum / 9.0;
}

float bayer4(ivec2 p) {
  const float m[16] = float[](0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5);
  return (m[(p.y & 3) * 4 + (p.x & 3)] + 0.5) / 16.0;
}

void main() {
  uint flags = draw.tex.w;

  // Cutaway 2.5D: làm mờ (dither) phần hình học nằm giữa camera và nhân vật, cao hơn nhân vật.
  if ((flags & FLAG_CUTAWAY) != 0u && frame.focus.w > 0.0) {
    vec3 cam = frame.camera_back_dir.w > 0.5 ? normalize(frame.camera_back_dir.xyz - frame.focus.xyz) : frame.camera_back_dir.xyz;
    vec3 d = v_world - frame.focus.xyz;
    float along = dot(d, cam);
    float perp = length(d - along * cam);
    if (along > 0.5 && v_world.y > frame.focus.y + 0.6 && perp < frame.focus.w) {
      float fade = smoothstep(frame.focus.w, frame.focus.w * 0.55, perp);  // 1 ở tâm
      if (bayer4(ivec2(gl_FragCoord.xy)) < fade * 0.85) discard;
    }
  }

  vec4 tex = texture(textures[draw.tex.x], v_uv);
  // Vật liệu có gió: vertex color.a là trọng số lay, không phải độ trong suốt
  float va = (flags & FLAG_WIND) != 0u ? 1.0 : v_color.a;
  vec4 base = tex * draw.base_color * vec4(v_color.rgb, va);
  float cutoff = (flags & FLAG_ALPHA_MASK) != 0u ? draw.extra.x : 0.5;
  if (base.a < cutoff) discard;  // alpha mask (lá cây, cỏ, hàng rào)
  vec3 albedo = base.rgb;

  if ((flags & FLAG_UNLIT) != 0u) {
    out_color = vec4(albedo, 1.0);
    return;
  }

  vec3 orm = texture(textures[draw.tex.z], v_uv).rgb;
  float ao = orm.r;
  float rough = clamp(draw.mat_params.y * orm.g, 0.04, 1.0);
  float metallic = clamp(draw.mat_params.x * orm.b, 0.0, 1.0);

  vec3 Ng = normalize(v_normal);
  if ((flags & FLAG_DOUBLE_SIDED) != 0u && !gl_FrontFacing) Ng = -Ng;  // mặt sau của lá/cỏ
  vec3 T = normalize(v_tangent.xyz - Ng * dot(Ng, v_tangent.xyz));
  vec3 B = cross(Ng, T) * v_tangent.w;
  vec3 tn = texture(textures[draw.tex.y], v_uv).xyz * 2.0 - 1.0;
  vec3 N = normalize(mat3(T, B, Ng) * tn);
  vec3 V = frame.camera_back_dir.w > 0.5 ? normalize(frame.camera_back_dir.xyz - v_world) : normalize(frame.camera_back_dir.xyz);

  // Mặt nước: sóng nhiều lớp, Fresnel phản chiếu bầu trời, lấp lánh mặt trời
  if ((flags & FLAG_WATER) != 0u) {
    float t = frame.params.z;
    vec2 p = v_world.xz;
    vec2 g = vec2(cos(p.x * 0.35 + t * 1.1) * 0.35 + cos(p.x * 1.3 - p.y * 0.7 + t * 2.1) * 0.12,
                  cos(p.y * 0.29 + t * 0.9) * 0.35 + cos(p.y * 1.7 + p.x * 0.5 + t * 1.7) * 0.12);
    vec3 Nw = normalize(vec3(-g.x * 0.18, 1.0, -g.y * 0.18));
    float fres = 0.04 + 0.96 * pow(1.0 - max(dot(Nw, V), 0.0), 5.0);
    vec3 R = reflect(-V, Nw);
    vec3 sky = mix(frame.sky_horizon.rgb, frame.sky_zenith.rgb, clamp(R.y * 1.5, 0.0, 1.0));
    vec3 deep = albedo * (0.25 + 0.75 * frame.sky_color.w);
    vec3 H = normalize(V + normalize(frame.sun_dir.xyz));
    float glint = pow(max(dot(Nw, H), 0.0), 220.0) * frame.sun_dir.w * shadow_factor(v_world, Nw);
    vec3 wc = mix(deep, sky, fres) + frame.sun_color.rgb * glint * 1.5;
    float fog_w = 1.0 - exp(-frame.fog_color.w * length(v_world - frame.camera_back_dir.xyz));
    out_color = vec4(mix(wc, frame.fog_color.rgb, clamp(fog_w, 0.0, 1.0)), 1.0);
    return;
  }

  vec3 color = vec3(0.0);
  // Mặt trời / mặt trăng
  float sh = shadow_factor(v_world, Ng);
  color += brdf(N, V, normalize(frame.sun_dir.xyz), albedo, metallic, rough,
                frame.sun_color.rgb * frame.sun_dir.w) * sh;
  // Point lights (đèn đường)
  int n = int(frame.params.w);
  for (int i = 0; i < n; ++i) {
    vec3 Lv = frame.point_pos[i].xyz - v_world;
    float dist = length(Lv);
    float r = frame.point_pos[i].w;
    if (dist >= r) continue;
    float att = pow(clamp(1.0 - pow(dist / r, 4.0), 0.0, 1.0), 2.0) / (dist * dist + 1.0);
    color += brdf(N, V, Lv / dist, albedo, metallic, rough, frame.point_color[i].rgb * att);
  }
  // Ambient bán cầu (placeholder cho environment lighting/IBL)
  float up = N.y * 0.5 + 0.5;
  vec3 ambient = mix(frame.ground_color.rgb, frame.sky_color.rgb, up) * frame.sky_color.w;
  color += ambient * albedo * ao * (1.0 - metallic * 0.5);
  color += albedo * draw.mat_params.z;  // emissive
  // Lá/cỏ (vật liệu có gió): ánh sáng xuyên qua lá khi nhìn ngược nắng — tán cây sáng, không bị "nhựa"
  if ((flags & FLAG_WIND) != 0u) {
    vec3 Ls = normalize(frame.sun_dir.xyz);
    float back = pow(clamp(dot(-V, Ls), 0.0, 1.0), 3.0) * 0.6 + max(dot(-Ng, Ls), 0.0) * 0.35;
    color += albedo * frame.sun_color.rgb * frame.sun_dir.w * back * 0.25 * sh;
  }

  // Sương mù theo khoảng cách: 3D tính từ camera (che rìa thế giới), 2.5D từ tâm cảnh
  float fog_d = frame.camera_back_dir.w > 0.5 ? length(v_world - frame.camera_back_dir.xyz) : length(v_world.xz - frame.focus.xz);
  float fog = 1.0 - exp(-frame.fog_color.w * fog_d);
  color = mix(color, frame.fog_color.rgb, clamp(fog, 0.0, 1.0));

  out_color = vec4(color, 1.0);
}
