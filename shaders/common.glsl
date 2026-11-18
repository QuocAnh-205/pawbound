// Layout dùng chung giữa C++ (render/gpu_types.hpp) và shader. Đổi ở đây phải đổi bên C++.
#define MAX_POINT_LIGHTS 8
#define MAX_TEXTURES 48

#define FLAG_CUTAWAY 1u
#define FLAG_UNLIT 2u
#define FLAG_ALPHA_MASK 4u   // cắt theo alpha (lá cây, cỏ)
#define FLAG_WIND 8u         // lay theo gió; vertex color.a = trọng số lay (0 gốc, 1 ngọn)
#define FLAG_WATER 16u       // mặt nước có sóng
#define FLAG_DOUBLE_SIDED 32u

layout(set = 0, binding = 0, std140) uniform FrameUniforms {
  mat4 view_proj;
  mat4 light_view_proj;
  vec4 camera_back_dir;  // w=0: xyz hướng từ cảnh tới camera (ortho); w=1: xyz vị trí camera (phối cảnh)
  vec4 sun_dir;          // xyz: hướng TỚI mặt trời; w: cường độ
  vec4 sun_color;        // rgb
  vec4 sky_color;        // rgb ambient từ trên; w: cường độ ambient
  vec4 ground_color;     // rgb ambient từ dưới
  vec4 fog_color;        // rgb; w: mật độ
  vec4 focus;            // xyz: vị trí nhân vật; w: bán kính cutaway (0 = tắt)
  vec4 params;           // x: exposure; y: 1/shadow_size; z: thời gian; w: số point light
  vec4 point_pos[MAX_POINT_LIGHTS];    // xyz, w: bán kính
  vec4 point_color[MAX_POINT_LIGHTS];  // rgb * cường độ
  mat4 inv_view_proj;    // dựng tia nhìn cho bầu trời
  vec4 sky_zenith;       // rgb màu đỉnh trời; w: độ phủ mây 0..1
  vec4 sky_horizon;      // rgb màu chân trời; w: sức gió
} frame;

layout(push_constant) uniform DrawPush {
  mat4 model;
  vec4 base_color;
  vec4 mat_params;  // x: metallic, y: roughness, z: emissive, w: offset ma trận joint (skinned)
  uvec4 tex;        // x: base color, y: normal, z: ORM, w: flags
  vec4 extra;       // x: ngưỡng alpha, y: hệ số gió của vật liệu
} draw;

// Gió: dịch theo thế giới, biên độ theo trọng số w (0 ở gốc) — dùng chung cho pass màu và pass bóng
vec3 wind_offset(vec3 wp, float w) {
  float t = frame.params.z;
  float s = frame.sky_horizon.w * draw.extra.y * w;
  return vec3(sin(t * 1.9 + wp.x * 0.42 + wp.z * 0.31) + 0.35 * sin(t * 4.3 + wp.z * 1.7), 0.0,
              cos(t * 1.6 + wp.x * 0.27 - wp.z * 0.39) + 0.35 * cos(t * 3.7 + wp.x * 1.3)) * s;
}
