# Renderer (Vulkan 1.3, 2.5D)

Mã nguồn: `src/render/renderer.{hpp,cpp}`, shader trong `shaders/` (biên dịch bằng `glslc` lúc build, SPIR-V nhúng vào binary).

## Pipeline mỗi frame
1. **Shadow pass**: depth-only vào shadow map 2048² (D32). Ma trận ortho của ánh sáng phủ bán kính `shadow_radius` quanh nhân vật và bám lưới texel để bóng không rung.
2. **Scene pass**: vẽ vào target HDR (RGBA16F) + depth (D32).
   - PBR metallic-roughness theo glTF: GGX, Smith-Schlick, Fresnel-Schlick; texture base color, normal (tangent space) và ORM.
   - Một directional light (mặt trời hoặc mặt trăng) với shadow PCF 3×3.
   - Tối đa 8 point light (đèn đường), chọn những đèn gần nhân vật nhất.
   - Ambient bán cầu (sky/ground). **Placeholder** cho environment lighting; chưa có IBL.
   - Sương mù theo khoảng cách tới nhân vật.
   - **Cutaway 2.5D:** fragment nằm giữa camera và nhân vật, cao hơn nhân vật 0,6 m và trong bán kính `cutaway_radius` sẽ bị dither-discard (pattern Bayer 4×4).
3. **Tonemap pass**: tam giác phủ màn hình, ACES (Narkowicz), exposure, vignette. Ghi vào swapchain sRGB.

## Đồng bộ và tài nguyên
- Dynamic rendering + synchronization2 (core 1.3). Không dùng VkRenderPass/VkFramebuffer.
- 2 frame in flight. Mỗi frame có command buffer, fence, semaphore `acquired` và UBO riêng. Mỗi swapchain image có semaphore `render_done` riêng.
- Bộ nhớ quản lý bằng VMA 3.5.0. Upload qua staging buffer. Texture tự sinh mipmap bằng blit.
- Mảng 48 texture (`textures[48]` trong descriptor set 0). Slot trống trỏ tới texture trắng. Giới hạn 48 vì Intel UHD chỉ cho 64 sampler mỗi stage (48 + shadow map).
- Swapchain được tạo lại khi resize, khi gặp OUT_OF_DATE/SUBOPTIMAL, và khi restore sau minimize. Khi minimize thì bỏ qua frame.
- Hủy tài nguyên theo thứ tự ngược trong `~Impl` sau `vkDeviceWaitIdle`.

## Camera và culling (`src/geom/camera.hpp`)
- `Camera25D`: orthographic, pitch 50°, yaw mặc định 45°, xoay theo bước 90° (Q/E) có nội suy, zoom 6–30 m.
- Frustum culling theo AABB (cho cả camera và ánh sáng).

## Chọn GPU
`RendererSettings::gpu_preference` hoặc biến môi trường `PB_GPU` (`discrete`, `integrated`, hoặc chuỗi con trong tên GPU). Yêu cầu Vulkan 1.3 với dynamicRendering, synchronization2, shaderDemoteToHelperInvocation, và đủ ≥ 49 sampler mỗi stage.

## QA
- `pawbound_client --validation --frames N --screenshot out.png [--resize-test] [--gpu integrated] [--time 21]`
- Exit code 4 nếu có lỗi validation.
- CTest `renderer_validation_smoke` (label `gpu`): resize → minimize → restore, yêu cầu 0 lỗi validation.

## Không phải photorealistic
Mọi mesh trong test scene là placeholder sinh bằng code. Chất lượng hình ảnh cuối phụ thuộc asset (Phase 4) và ánh sáng nâng cao (IBL, SSAO, post-processing) dự kiến ở Phase 15.
