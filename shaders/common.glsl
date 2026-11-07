// Layout dùng chung giữa C++ (render/gpu_types.hpp) và shader. Đổi ở đây phải đổi bên C++.
#define MAX_POINT_LIGHTS 8
#define MAX_TEXTURES 48

#define FLAG_CUTAWAY 1u
#define FLAG_UNLIT 2u

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
} frame;

layout(push_constant) uniform DrawPush {
  mat4 model;
  vec4 base_color;
  vec4 mat_params;  // x: metallic, y: roughness, z: emissive, w: offset ma trận joint (skinned)
  uvec4 tex;        // x: base color, y: normal, z: ORM, w: flags
} draw;
