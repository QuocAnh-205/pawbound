# Phase 9 — Báo cáo: World Simulation, Day/Night, Weather & Events

## A. Kết luận: **PASS**

## B. Đã triển khai
Xem `docs/world-simulation.md`.

## D. Lệnh và kết quả thật
| Lệnh | Kết quả |
|---|---|
| Build Debug | OK, 0 warning |
| `pb_tests` | **85/85 test case pass** (9 test `worldsim:` mới) |
| `ctest` | **13/13 pass** (thêm `worldsim_client_storm_smoke`: bão + sự kiện + đổi kích thước cửa sổ, validation sạch) |
| Client `--time 21 --weather rain --event food_hotspot` | mưa, đèn đường, HUD liệt kê sự kiện, chỉ 19 NPC ban đêm (42 lúc 15h); 0 lỗi validation |

Screenshot: `screenshots/phase9_night_rain.png`, `screenshots/phase9_fog.png`.

### Lỗi thật phát hiện trong phase
1. **Trùng mặt nạ id**: node tạm của sự kiện dùng `0xE0000000`, trùng bit với id POI thật (`0x3…`). Hậu quả: snapshot bỏ qua mọi nguồn tài nguyên thật, nên trạng thái tài nguyên không bao giờ được lưu. Test lưu/nạp và test đếm node bắt được; đã đổi sang tag `0xF` + `is_event_node()`.
2. Vòng hồi tài nguyên tạo vector tạm cho mỗi node mỗi tick. Đã viết lại.
3. Tham số `--at` có thể đặt người chơi vào trong vật cản. Đã đẩy ra bằng `CollisionWorld::resolve`.
4. Panel HUD hẹp hơn chữ, số bị mất trên nền sương. Đã nới rộng.

## F. Known issues
- `dog_conflict`, `rescue_signal` mới là điểm đánh dấu + hoàn thành đơn giản; nội dung đầy đủ ở Phase 14.
- Mưa là vệt hình học đơn giản, chưa có mặt đường ướt, vũng nước hay âm thanh (chưa tích hợp audio).
- Đồng bộ giờ, thời tiết và sự kiện qua mạng là Phase 10.

## G. Tiêu chí
| Tiêu chí | Trạng thái |
|---|---|
| Chu kỳ ngày/đêm quan sát được | PASS (screenshot 21h vs 8h; test ánh sáng) |
| NPC và tài nguyên theo giờ | PASS (test mật độ + test hồi giờ ăn) |
| Thời tiết chuyển không đột ngột | PASS (blend 90 s; test bước < 0,001) |
| Sự kiện kích hoạt và kết thúc đúng | PASS (test duration, cooldown, chống lặp, hoàn thành sớm, gỡ node tạm) |
| Chạy test ở giờ cố định | PASS (`--time`, `set_hour`) |
| Logic không phụ thuộc render frame | PASS (test 30 Hz vs 10 Hz) |
