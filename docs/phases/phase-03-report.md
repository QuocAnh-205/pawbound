# Phase 3 — Báo cáo: Vulkan Graphics & Rendering Engine

## A. Kết luận: **PASS**

## B. Đã triển khai
Xem `docs/architecture/renderer.md`. Thêm module `pb_geom` (mesh, AABB, primitive procedural, camera 2.5D, frustum, đọc/ghi ảnh qua stb) và `pb_render` (Vulkan). Dependency mới: VMA 3.5.0 (MIT) và stb commit `d14d46f6b444` (MIT/Public domain).

## D. Lệnh và kết quả thật
| Lệnh | Kết quả |
|---|---|
| Build Debug và Release | OK, 0 warning |
| `ctest` (Debug) | 6/6 pass, gồm `renderer_validation_smoke` |
| `pb_tests` | 31 test case, 401 assertion, tất cả pass |
| `pawbound_client --validation --gpu discrete` | GTX 1650 Max-Q, Vulkan 1.3.194, **0 lỗi validation** |
| `pawbound_client --validation --gpu integrated` | Intel UHD, Vulkan 1.3.215, **0 lỗi validation** |
| `--resize-test` | swapchain tạo lại 1280×720 → 800×600 → 1600×900 → minimize → restore → 640×360, 0 lỗi |

Lỗi thật do validation layer phát hiện và đã sửa: (1) thiếu feature `shaderDemoteToHelperInvocation`, cần cho `discard` khi biên dịch target vulkan1.3; (2) vượt giới hạn 64 sampler mỗi stage trên Intel, đã giảm mảng texture xuống 48.

## E. Hiệu năng (Release, 1280×720, tắt vsync, test scene 59 draw và 7.284 tam giác)
| GPU | FPS | GPU time | Draw (scene/shadow) |
|---|---|---|---|
| GTX 1650 Max-Q | 234 | 0,68 ms | 59 / 64 |
| Intel UHD | 85–95 | 10,4–11,0 ms | 59 / 64 |

Screenshot do chính chương trình chụp: `screenshots/phase3_discrete.png` (ban ngày), `screenshots/phase3_night.png` (21h, đèn đường), `screenshots/phase3_integrated.png`.

## F. Known issues
- Ambient bán cầu chỉ là **placeholder**; chưa có IBL, SSAO hay bloom.
- Upload texture gọi `vkDeviceWaitIdle` để cập nhật descriptor. Chấp nhận được lúc load, nhưng không dùng được để streaming giữa frame (cần sửa ở Phase 5/15).
- Chưa có instancing. Mỗi đối tượng là một draw call (sẽ tối ưu ở Phase 5/15 khi số object lớn).
- GPU time trên Intel khoảng 10 ms; cần preset đồ họa thấp (shadow 1024, PCF ít tap) ở Phase 15.

## G. Tiêu chí
| Tiêu chí | Trạng thái |
|---|---|
| Mesh 3D có camera, depth đúng | PASS (screenshot) |
| Texture và material | PASS (checker procedural, PBR metallic/roughness) |
| Ánh sáng và shadow | PASS |
| Resize không crash | PASS (`--resize-test`) |
| Không có validation error nghiêm trọng | PASS (0 lỗi trên 2 GPU) |
| Thống kê draw calls, tam giác, CPU/GPU time | PASS (title bar + log) |
| Hủy tài nguyên đúng thứ tự | PASS (0 lỗi validation khi tắt) |
| Screenshot do chương trình tạo | PASS |
