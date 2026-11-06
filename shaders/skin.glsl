// Ma trận skinning của mọi draw trong frame; mỗi draw có offset riêng (draw.mat_params.w).
layout(set = 0, binding = 3, std430) readonly buffer JointMatrices { mat4 joints[]; };

layout(location = 5) in uvec4 in_joints;
layout(location = 6) in vec4 in_weights;

mat4 skin_matrix() {
  int base = int(draw.mat_params.w);
  return in_weights.x * joints[base + int(in_joints.x)] + in_weights.y * joints[base + int(in_joints.y)] +
         in_weights.z * joints[base + int(in_joints.z)] + in_weights.w * joints[base + int(in_joints.w)];
}
