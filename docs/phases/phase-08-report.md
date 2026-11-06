# Phase 8 — Báo cáo: NPC, Navigation & AI

## A. Kết luận: **PASS**

## B. Đã triển khai
Xem `docs/ai/npc-ai.md`. Thêm `ModelLibrary` (mỗi model nạp một lần, dùng chung): trước đó mỗi `DogActor` tự nạp glTF và upload texture, nhiều NPC sẽ vượt giới hạn 48 texture.

## D. Lệnh và kết quả thật
| Lệnh | Kết quả |
|---|---|
| Build Debug/Release | OK, 0 warning |
| `pb_tests` | **76/76 test case pass** (11 test AI) |
| `ctest` | **12/12 pass** |
| Kịch bản AI 60 s, seed demo, khu trung tâm, bán kính 2 chunk | 78 NPC; 75 NPC di chuyển > 3 m; **0 NPC kẹt trong nhà**; 228 lần tìm đường (108 bị hoãn do ngân sách, **0 lỗi**) |
| Chi phí AI (78 NPC) | Release: trung bình **0,0075 ms/tick**, tối đa 0,17 ms. Debug: 0,048 / 0,72 ms. Ngân sách đặt ra ≤ 3 ms cho 100 NPC: **đạt** |
| Client `--world ... --debug` | 0 lỗi validation; HUD hiện "NPC 42 · AI 0.03 ms" |

Kết quả Debug và Release **giống hệt nhau** (cùng số NPC, đường đi, sự kiện), là bằng chứng phụ cho tính deterministic.

### Lỗi thật phát hiện trong phase
1. **Báo cáo nhầm do bộ lọc test**: lần chạy đầu tôi lọc test bằng tên tiếng Việt; doctest trên Windows nhận argv theo mã ANSI nên các test đó **không chạy**, nhưng vẫn báo "11 passed". Chạy toàn bộ mới lộ ra 2 test fail. Từ nay chỉ lọc bằng chuỗi ASCII hoặc chạy toàn bộ.
2. `move_toward` vừa bật cờ walk vừa truyền độ lớn < 1, nên tốc độ NPC chỉ còn 0,52 m/s (người thân thiện không kịp tới cho ăn). Đã sửa.
3. Hai kỳ vọng test sai (trạng thái sau khi bỏ chạy thành công; người chơi đi quá ngưỡng bỏ theo 20 m). Đã sửa test, không sửa code để chiều test.
4. Thời gian chờ của NPC bị bốc ngẫu nhiên lại mỗi tick. Đã sửa.

## F. Known issues
- Waypoint graph theo tim đường, **chưa có navmesh**: NPC đi giữa lòng đường thay vì vỉa hè; chưa né nhau (không có local avoidance giữa NPC).
- Người NPC là hình khối placeholder, chưa có animation.
- Lịch hoạt động theo giờ của người đi bộ sẽ làm ở Phase 9 (world clock).
- Kẻ trộm chó (thief) mới có trong thiết kế, triển khai ở Phase 14.

## G. Tiêu chí
| Tiêu chí | Trạng thái |
|---|---|
| NPC di chuyển giữa waypoint/POI hợp lệ | PASS (kịch bản 60 s, 0 NPC trong nhà) |
| Phản ứng khác nhau theo tính cách và hành vi người chơi | PASS (5 test hành vi + tai tiếng) |
| Chó NPC có trạng thái cơ bản | PASS (roam/sniff/seek food/water/rest/follow/growl/fight/flee) |
| Không lặp đường vô hạn; có timeout/fallback | PASS (test NoPath/Truncated; kẹt → bỏ đích) |
| Chi phí AI đo được và có giới hạn config | PASS |
| Spawn/despawn theo chunk đúng | PASS (test deterministic + gỡ khi đi xa) |
