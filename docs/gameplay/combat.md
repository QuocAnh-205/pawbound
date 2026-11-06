# Chiến đấu và vùng PvP (Phase 7)

Mã: `src/sim/gameplay.cpp` (`request_bite`, `request_dodge`, `pvp_allowed`, `tick_combat`). Thông số: `data/gameplay/gameplay.json → combat`.

## Cắn (J / X gamepad)
Thứ tự kiểm tra phía "server":
1. Kẻ tấn công còn sống, không choáng.
2. Hết hồi chiêu (0,8 s).
3. Mục tiêu còn sống.
4. Luật PvP (chỉ khi cả hai là người chơi).
5. Đủ thể lực (10).

Sau bước 5, cú cắn **được thực hiện**: tốn thể lực và bắt đầu hồi chiêu dù trúng hay trượt. Tiếp theo kiểm tra tầm (1,3 m), cung trước mặt (90°), và mục tiêu có đang né (iframes) không.

- Sát thương deterministic: `12 × (1 + str) × (1 − def)`, str/def bị kẹp ±25% (GDD §13).
- 3 cú trúng trong 2 s gây choáng 0,5 s.
- Yêu cầu bị từ chối (cấm PvP, hồi chiêu, hết thể lực) thì **không** trừ thể lực.

## Né (K / B gamepad)
0,3 s miễn trúng, −15 thể lực, hồi chiêu 1,5 s.

## Vùng (`pvp_zones` trong world config)
| Vùng | PvP giữa người chơi |
|---|---|
| safe (công viên) | cấm |
| restricted (trung tâm, dân cư) | chỉ khi **cả hai** bật hung hăng và cả hai cấp ≥ 5 |
| contested (công nghiệp) | tự do |

Đánh NPC không áp luật PvP, nhưng cắn NPC làm tăng `notoriety` (+10, giảm dần 0,05/s). Phase 8 dùng chỉ số này để người dân phản ứng; không có hình phạt nặng.

## Server authoritative (chuẩn bị cho Phase 10)
Client chỉ gửi ý định "cắn theo hướng đang nhìn". Vị trí, hướng, HP, thể lực và kết quả do bên mô phỏng tính bằng chính các hàm trên.
