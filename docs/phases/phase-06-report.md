# Phase 6 — Báo cáo: Dog Character, Third-Person (2.5D) Camera & Movement

## A. Kết luận: **PASS**, kèm hai lưu ý phải biết
1. **Không dùng Jolt** cho nhân vật mà dùng va chạm 2.5D tự viết (phương án dự phòng ghi sẵn trong ADR-006, nay đã cập nhật lý do). Đây là sai khác so với MASTER PROMPT mục 10.
2. Animation idle/walk/run chạy trên **model prototype (Fox — cáo)** có rig thật. Chưa có model chó production.

## B. Đã triển khai
Xem `docs/gameplay/dog-character.md`.

## D. Lệnh và kết quả thật
| Lệnh | Kết quả |
|---|---|
| Build Debug | OK, 0 warning |
| `pb_tests` | **55 test case pass** (thêm 10 test sim: tăng tốc, giới hạn gia tốc, gait, xuyên tường khi nước rút, trượt tường, hồ + biên, nhảy/độ cao đỉnh ≈ 0,625 m, nhảy qua ghế nhưng không qua tường 3 m, dốc đứng, breed config, anim graph, input + gamepad deadzone) |
| `ctest` | **11/11 pass**; `world_client_streaming_smoke` nay điều khiển chó tự chạy, nước rút, nhảy 400 frame qua world, validation sạch |
| Client `--world out/world_demo` | 0 lỗi validation; Fox skinned 24 joint; collision 179 box + 1967 hình tròn |

Screenshot: `screenshots/phase6_dog_close.png` (zoom 4, idle), `phase6_dog_idle.png` (debug), `phase6_dog_run.png` (đang chạy).

Lỗi gặp và sửa trong phase:
- Test nhảy qua ghế ban đầu fail vì nhảy từ trạng thái đứng yên. Hành vi đúng thiết kế (chỉ còn 35% điều khiển khi trên không), nên sửa test thành chạy lấy đà.
- Smoke test thiếu `--asset-root` nên không tìm thấy `breeds.json`.

## F. Known issues
- Hướng mặt trước của Fox (`yaw_offset_deg: 0`) mới kiểm tra bằng mắt trên screenshot; **cần người chơi thử xác nhận** khi điều khiển thật.
- Chưa có clip nhảy riêng (dùng run phát chậm khi trên không).
- Hộp va chạm của ghế/thùng lớn bỏ qua góc xoay (sai số nhỏ).
- Gamepad đã có code (SDL3) nhưng **chưa thử với tay cầm thật** (máy không có tay cầm); logic mapping đã có unit test.

## G. Tiêu chí
| Tiêu chí | Trạng thái |
|---|---|
| Điều khiển nhân vật trong world procedural | PASS |
| Camera 2.5D bám, không rung; vật che bị cutaway | PASS (thay cho "camera tránh tường" theo ADR-002) |
| idle/walk/run animation với asset fixture | PASS (Fox prototype + placeholder) |
| Không rơi xuyên địa hình; va chạm cơ bản | PASS (test) |
| Breed/appearance cấu hình qua data | PASS |
| Build + test pass | PASS |
