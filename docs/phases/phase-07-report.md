# Phase 7 — Báo cáo: Survival, Interaction, Inventory & Combat

## A. Kết luận: **PASS**

## B. Đã triển khai
- Luật gameplay thuần trong `pb_sim` (survival, resource node, inventory, interaction, combat, PvP zone, respawn, HUD model). Xem `docs/gameplay/survival.md`, `combat.md`.
- Renderer: pass **overlay 2D** (HUD) dùng stb_easy_font, alpha blend sau tonemap.
- Client: `GameSession` chạy vòng chơi với 2 chó NPC tập (đứng yên; AI ở Phase 8), đánh hơi, HUD.
- QA: kịch bản `--scenario survival` chạy trong client thật, đã đưa vào CTest.

## D. Lệnh và kết quả thật
| Lệnh | Kết quả |
|---|---|
| Build Debug | OK, 0 warning |
| `pb_tests` | **65 test case pass** (10 test gameplay mới) |
| `ctest` | **12/12 pass** |
| `pawbound_client --world out/world_demo --scenario survival --validation` | 10/10 bước PASS: uống đúng +30, ăn đúng +35, lấy đồ vào túi, dùng chai nước, cắn NPC (HP 100 → 29, có choáng), cắn tốn thể lực, nghỉ hồi rest; 0 lỗi validation |

Screenshot HUD: `screenshots/phase7_hud.png`.

Lỗi thật phát hiện trong phase:
- Dòng xử lý né viết sai (dereference con trỏ null): phát hiện khi đọc lại code, sửa **trước** khi build.
- Kịch bản QA bấm E hai lần do `tick_count()` cộng dồn trước khi chạy các tick trong frame. Sửa bằng bộ đếm riêng; kiểm tra siết thành "đúng một lần".

## F. Known issues
- **HUD chỉ hiển thị ASCII**, chưa có tiếng Việt có dấu (cần stb_truetype + font OFL như Noto Sans).
- NPC tập đứng yên, chưa đánh trả (Phase 8).
- Phím dùng túi 1–6 / G / Z đang xử lý trực tiếp trong client (chưa đưa vào `input.json`).
- `Application::tick_count()` trả tổng tick **của cả frame** trong lúc chạy `on_fixed_update`. Không sai, nhưng dễ dùng nhầm (đã ghi chú).

## G. Tiêu chí
| Tiêu chí | Trạng thái | Bằng chứng |
|---|---|---|
| Tìm và dùng thức ăn/nước | PASS | kịch bản client + test |
| Chỉ số thay đổi theo simulation time | PASS | test "độc lập kích thước bước" |
| Nghỉ hồi theo luật | PASS | test + kịch bản |
| Combat có hit validation, hồi chiêu, tốn thể lực | PASS | test chiến đấu |
| Inventory không vượt giới hạn, không nhân bản | PASS | test stack/ô, `apply_once`, JSON giả mạo |
| Luật vùng PvP có test | PASS | |
| HP = 0 không mất nhân vật; có chính sách gục/hồi sinh | PASS | test respawn |
