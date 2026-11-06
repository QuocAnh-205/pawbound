#pragma once
// Mesh sinh bằng code: dùng cho placeholder (nhà, cây, props, chó) và test scene.
#include "geom/mesh_data.hpp"

namespace pb::geom {

// Hộp tâm gốc tọa độ, kích thước size. UV theo mét nếu uv_per_meter > 0.
MeshData make_box(const glm::vec3& size, float uv_per_meter = 0.0f);
// Lưới phẳng trên mặt XZ, nx × nz ô, tâm gốc tọa độ.
MeshData make_grid(float size_x, float size_z, int nx, int nz, float uv_scale = 1.0f);
// Trụ đứng, đáy tại y=0.
MeshData make_cylinder(float radius, float height, int segments, bool caps = true);
// Nón đứng, đáy tại y=0.
MeshData make_cone(float radius, float height, int segments);
// Cầu UV tâm gốc tọa độ.
MeshData make_sphere(float radius, int rings, int segments);

}  // namespace pb::geom
