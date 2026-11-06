# Phase 5 — Báo cáo: Procedural World Generation & Streaming

## A. Kết luận: **PASS** (kèm known issue: hitch khi nạp chunk; chưa kiểm tra hash chéo nền tảng)

## B. Đã triển khai
Xem `docs/architecture/world-generation.md`.

## D. Lệnh và kết quả thật
| Lệnh | Kết quả |
|---|---|
| `pawbound world generate --seed 20261106 ...` | 28–95 ms (Debug); 81 nút đường, 130 cạnh, 157 nhà, 1984 props, 44 POI, 449 nút nav, 5 hồ, 64 chunk |
| `pawbound world validate out/world_demo` | HỢP LỆ, 0 lỗi, exit 0 |
| Quét seed 1..60 + seed demo | **61/61 hợp lệ** |
| `pb_tests` (test world) | 7 test case pass: rng, noise, determinism + golden hash, demo hợp lệ, 7 fixture lỗi cố ý, file I/O + file hỏng, streaming |
| `ctest` | **11/11 pass** (thêm `world_generate_demo`, `world_validate_demo`, `world_client_streaming_smoke`) |
| Client `--world` Release, GTX 1650, 1280×720, tắt vsync | khoảng 710–725 FPS; 9–11 draw call; 35–40k tam giác; GPU 0,21 ms; 0 lỗi validation |

Lỗi thật do validator bắt được trong quá trình làm và đã sửa:
1. Nhà chắn lối đi công viên: block bị chia bằng đường nhánh của ô khác.
2. Điểm nước ở mép hồ cách nav > 30 m.
3. Spawn rơi ra ngoài vùng safe ở ranh giới Voronoi (seed 2 và 42).

Screenshot: `screenshots/phase5_world.png`, `phase5_world_wide.png` (công viên), `phase5_downtown.png`, `phase5_downtown_night.png`.

## F. Known issues
- **Hitch nạp chunk:** max frame 108–127 ms khi nạp chunk mới (tạo mesh và upload đồng bộ, `vkQueueWaitIdle` mỗi lần upload). Cần nạp bất đồng bộ (thread pool) và staging gộp; đưa vào Phase 15.
- Determinism mới chỉ kiểm tra trên Windows/MinGW. Kiểm tra chéo Linux ở Phase 10.
- Đây là thành phố placeholder: hình khối, không photorealistic.

## G. Tiêu chí
| Tiêu chí | Trạng thái |
|---|---|
| Sinh khu thành phố nhỏ bằng CLI | PASS |
| Runtime nạp và render địa hình, đường, nhà, cây/props | PASS (screenshot) |
| Không cần editor hoặc chỉnh tay tọa độ | PASS |
| Cùng seed/config cho cùng logical hash | PASS (test + golden hash) |
| Validator phát hiện lỗi với fixture lỗi cố ý | PASS (7 kịch bản) |
| Thống kê chunk, mesh, nhà, props, thời gian | PASS |
| Streaming có test load/unload, không trùng | PASS (unit + smoke client) |
