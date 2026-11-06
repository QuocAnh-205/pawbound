# Sinh tồn, tương tác, inventory (Phase 7)

Mã: `src/sim/gameplay.{hpp,cpp}` (luật, chạy được trên server), `src/client/game_session.*` (vòng chơi single-player + HUD), dữ liệu: `data/gameplay/gameplay.json`.

## Chỉ số (giây **mô phỏng**, độc lập FPS — có test so sánh bước 1/30 s và 0,5 s)
| Chỉ số | Luật |
|---|---|
| Đói | 100 → 0 trong 4800 s (≈ 80 phút). Di chuyển ×1,15; nước rút ×1,6 |
| Khát | 100 → 0 trong 3600 s |
| Nghỉ (rest) | giảm khi thức (7200 s); nghỉ (phím Z) hồi 1,2/s, ở nơi trú ẩn ×2; di chuyển thì dừng nghỉ |
| Thể lực | nước rút −12/s; nhảy −8; cắn −10; né −15; hồi 12/s sau 1 s không tiêu; khát < 20 thì tối đa chỉ còn 70% |
| HP | hồi 0,25/s khi đói & khát > 20 (+0,3/s khi nghỉ); đói = 0 thì −1/30 s; khát = 0 thì −1/20 s |
| Mệt | rest < 15 thì tốc độ ×0,8 |

Nước rút: muốn **bắt đầu** cần thể lực ≥ 15; đang chạy thì tiếp tục tới khi cạn.

## Gục và hồi sinh
HP = 0 → **Downed** 10 s (Phase 14: đồng đội cứu) → **Dead** → hồi sinh tại nơi trú ẩn/spawn gần nhất với 50% HP. Rơi `floor(50%)` mỗi chồng đồ **tiêu hao**. **Không** mất cosmetic, nhân vật hay tiền thật.

## Tài nguyên (POI từ world generator)
| Loại | Sức chứa | Mỗi lần | Hồi 1 lượt |
|---|---|---|---|
| Nước | 10 | +30 khát | 60 s |
| Thức ăn | 3 | +35 đói, hoặc lấy 1 `scrap_food` vào túi | 240 s |
Kiểm tra khoảng cách (≤ 2 m) dựa trên vị trí do bên mô phỏng nắm giữ, không tin client. Đã no hoặc đủ nước thì không phí tài nguyên.

## Inventory
6 ô, `max_stack` theo item. `add()` trả số không thêm được. Item lạ bị từ chối. `apply_once(op_id)` chặn áp dụng lặp (gói tin gửi lại). `from_json()` từ chối dữ liệu giả mạo (vượt stack, item lạ, số âm, sai số ô).

## Phím (đổi trong `config/input.json`)
E: tương tác theo ngữ cảnh (uống/ăn/nghỉ ở trú ẩn) · G: lấy đồ ăn vào túi · 1–6: dùng ô túi · Z: nghỉ/thôi nghỉ · F (giữ): đánh hơi, hiện cột màu trên nguồn nước (xanh) và thức ăn (cam) trong 25 m.

## HUD
Thanh HP/FOOD/WATER/STAMINA/REST, trạng thái (HUNGRY/THIRSTY/TIRED/RESTING/DOWN), vùng + giờ, 6 ô túi, gợi ý tương tác, thông báo, máu mục tiêu. Chỉ đọc `HudModel`, không chứa logic. **Chữ hiện là ASCII không dấu** (stb_easy_font); hiển thị tiếng Việt có dấu cần font TTF, chưa làm.
