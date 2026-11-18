#version 450
// Tam giác phủ màn hình ở độ sâu 1.0 (xa nhất): vẽ đầu tiên, mọi vật thể vẽ đè lên.
layout(location = 0) out vec2 v_ndc;
void main() {
  vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
  v_ndc = p * 2.0 - 1.0;
  gl_Position = vec4(v_ndc, 1.0, 1.0);
}
