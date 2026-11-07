# Gameplay nâng cao (Phase 14)

Tất cả trạng thái quan trọng **authoritative trên game server** (`src/sim/advanced.*`, `src/server/advanced_server.cpp`). Thông số: `data/gameplay/advanced.json` (bản test nhanh: `tests/data/advanced_fast.json`).

## Bắt chó (kẻ trộm) & giải cứu
```
Normal ──(kẻ trộm vào tầm 18 m)──► Threatened ──(chạy ≥ 28 m / vào khu an toàn / cắn kẻ trộm / kẻ trộm bỏ cuộc 12 s)──► Normal + miễn nhiễm 30'
                                      │ (sát ≤ 1,6 m VÀ đã cảnh báo ≥ 3 s)
                                      ▼
                                   Captured ──(đồng đội đứng cạnh chuồng + Tương tác)──► RescueInProgress ──(giữ 4 s)──► Normal (Rescued) + miễn nhiễm
                                      │  ▲                                                   │ (người cứu rời đi / rớt mạng / chết)
                                      │  └───────────────────────────────────────────────────┘ gián đoạn, bắt đầu lại
                                      └──(hết hạn giữ ≤ 10 phút)──► Normal (Released) tại khu an toàn + miễn nhiễm
```
Luật kẻ trộm xuất hiện: chỉ giờ đêm (20–5h giờ thế giới), chỉ khu **không an toàn**, xuất hiện cách 30–45 m (ngoài tầm cảnh báo — luôn thấy trước), tối đa 3 kẻ trộm/server, một kẻ/người, **không nhắm người vào phiên < 5 phút** và người đang miễn nhiễm.

Bảo vệ người chơi:
- Bị bắt **không** xóa nhân vật, **không** mất túi đồ, **không** liên quan tiền thật. Chỉ bị đưa tới khu giữ; vẫn đi lại trong bán kính 4 m, chat được.
- Thời gian giữ có trần (mặc định 10 phút; loader chặn > 1 giờ). Đồng hồ tính theo giờ thật, chạy cả khi offline.
- HUD hiện trạng thái + đồng hồ (`SelfState.capture`, `capture_s` — giao thức v2).

Chống exploit (đã test):
| Exploit | Biện pháp | Test |
|---|---|---|
| Relog để thoát khi bị bắt | trạng thái lưu ngay khi bị bắt; nạp lại → về chuồng nếu còn hạn | unit `exploit…`, tích hợp backend "RELOG khi bị bắt" |
| Hết hạn lúc offline | vào lại → được thả ở khu an toàn | unit |
| Người cứu rớt mạng giữa chừng | gián đoạn, nạn nhân vẫn bị giữ | unit, `capture_smoke.py` |
| Hai người cùng cứu / spam tương tác | một người cứu tại một thời điểm; thưởng khóa `rescue:<nhân vật>:<lần bắt>` | unit, smoke (thưởng 1 lần), e2e 6 request song song → cộng 1 lần |
| Kill loop (bị bắt liên tục) | miễn nhiễm 30 phút sau mọi kết thúc | unit |
| Dịch chuyển để cày XP | bỏ qua quãng nhảy > 20 m | — |

Manh mối & thông báo: đồng đội **cùng server** (party + bang) nhận sự kiện "bị bắt — dấu vết ở hướng X, khoảng N m". Backend gửi thông báo cho party/bang (`POST /internal/events/capture`): chỉ tên + quận, **không** tọa độ; không gửi cho bạn bè thường; tối đa 1 thông báo "bị bắt" / người / 10 phút; khóa dedupe theo lần bắt.

Chưa làm (so với đề bài): cảnh "đột nhập/đánh lạc hướng" và "hộ tống về điểm an toàn" — giải cứu hiện là tương tác giữ nút tại chuồng.

## Companion
Đứng cạnh **chó hoang thân thiện** và Tương tác khi có `meat_bone` (tiêu 1) → có bạn đồng hành. Tối đa 1 (lưu trong state; bấm nhiều/relog không nhân bản). Đi theo chủ, cảnh báo kẻ trộm từ 30 m (sớm hơn cảnh báo thường), đánh hơi đồ ăn; cooldown 20 s. **Không tấn công người chơi**, không cộng chỉ số chiến đấu.

## Kỹ năng
`speed`, `stamina`, `strength`, `scent`; level = ⌊√(xp/100)⌋, tối đa 5. Tốc độ +1%/cấp (trần +5%, loader chặn > 2%/cấp). Strength chỉ tăng sát thương lên NPC/kẻ trộm — **không** áp lên người chơi (cân bằng PvP). XP: đi bộ, nước rút, cắn kẻ trộm, đánh hơi. Dữ liệu lưu hỏng/âm/NaN bị lọc (không làm sập server).

## Nhiệm vụ
Định nghĩa trong `advanced.json`: mục tiêu (`forage`, `drink`, `travel`, `rescue`, `escape_thief`), số lượng, thưởng (xương), timeout, lặp hằng ngày (UTC). Tiến độ lưu trong state; mỗi nhiệm vụ hoàn thành tối đa 1 lần / kỳ; thưởng qua ví Backend với khóa `quest:<nhân vật>:<id>:<ngày>` (gửi lại không cộng trùng).

## Lãnh thổ (MVP)
Khu = ô quận của world. Game server: bang có ≥ 2 thành viên online, còn sống, không bị bắt, **không có bang khác hiện diện**, giữ đủ 60 s → xin chiếm. Backend (`POST /internal/territory/claim`, khóa hàng): cùng bang → gia hạn duy trì 24 h; bang khác → chỉ chiếm được khi hết bảo hộ 1 h; tối đa 3 khu/bang. Chủ lãnh thổ **chỉ là uy tín** (`GET /territories`), không cho sức mạnh, không mua bằng tiền thật. Chưa có hiệu ứng trong game và hiển thị bản đồ ở client.
