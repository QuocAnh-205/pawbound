#version 450
layout(location = 0) in vec4 v_color;
layout(location = 0) out vec4 out_color;
// Màu HUD khai báo theo sRGB; swapchain sRGB sẽ mã hóa lại -> đổi về tuyến tính trước.
void main() { out_color = vec4(pow(v_color.rgb, vec3(2.2)), v_color.a); }
