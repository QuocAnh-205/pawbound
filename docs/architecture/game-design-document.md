# PAWBOUND — Game Design Document (v0.1, Phase 0)

> Trạng thái: bản thiết kế. Mọi con số cân bằng đều là **giá trị khởi điểm**, nằm trong file config và sẽ được tinh chỉnh qua playtest.

## 1. Player fantasy
"Mình là một chú chó tự do giữa thành phố lớn." Người chơi đánh hơi, tìm đồ ăn, tránh nguy hiểm, kết bầy với chó khác (người chơi thật), giành lãnh thổ và giải cứu bạn bè bị bắt.

## 2. Trình bày
- **2.5D** (ADR-002): thế giới 3D, camera orthographic nhìn chéo khoảng 50°, xoay theo bước 90°, zoom giới hạn. Phong cách stylized-realistic.
- Ngày/đêm và thời tiết động. Đêm tối thật, có đèn đường; mưa làm giảm tầm nhìn và mùi.

## 3. Core loop
```
Khám phá ─▶ Đánh hơi / tìm tài nguyên ─▶ Ăn, uống ─▶ (gặp chó/người/sự kiện)
   ▲                                                   │
   │                                     Né / giao tiếp / chiến đấu
   │                                                   │
   └──── Nghỉ ngơi ở nơi trú ẩn (hồi thể lực, lưu tiến trình) ◀───┘
```
Vòng dài: lên kỹ năng (mũi thính, sức bền, tốc độ), kết đàn, vào bang hội, giữ lãnh thổ, làm sự kiện giải cứu.

## 4. Di chuyển (Phase 6)
| Trạng thái | Tốc độ khởi điểm | Thể lực |
|---|---|---|
| Đi bộ | 1,5 m/s | hồi |
| Chạy nước kiệu | 3,5 m/s | hồi chậm |
| Chạy | 6 m/s | −4/giây |
| Nước rút | 9 m/s | −12/giây, cần ≥ 15 thể lực để bắt đầu |
| Nhảy | lên khoảng 0,8 m | −8 mỗi lần |

Điều khiển (chốt ở Phase 6, đổi được trong `config/input.json`): WASD/mũi tên đi theo hướng camera, Shift nước rút, Ctrl đi bộ, Space nhảy, E tương tác, J cắn, K né, F đánh hơi, Q/R xoay camera 90°, cuộn chuột để zoom, F3 debug. Gamepad: cần trái, A nhảy, RT nước rút, X cắn, B né, Y tương tác, LB/RB xoay camera.

## 5. Sinh tồn (Phase 7)
| Chỉ số | Khoảng | Suy giảm khởi điểm | Hậu quả khi thấp |
|---|---|---|---|
| HP | 0–100 | — | 0 → gục (xem §11) |
| Đói | 0–100 (100 = no) | về 0 sau khoảng 2 ngày game (≈ 80 phút thật) | < 20: không hồi HP; = 0: −1 HP/30 giây |
| Khát | 0–100 | về 0 sau khoảng 1,5 ngày game | < 20: thể lực tối đa giảm 30%; = 0: −1 HP/20 giây |
| Thể lực | 0–100 | theo hành động | 0: không chạy hoặc cắn được |
| Mệt (rest) | 0–100 | giảm dần khi thức | < 15: tốc độ −20% |

Nguyên tắc: **không bắt người chơi grind**. Một bữa ăn tốt lấp khoảng 40–60 điểm đói. Tài nguyên phổ biến ở khu dân cư và công viên.

## 6. Khám phá và đánh hơi
- Phím F bật "mũi": hiện vệt mùi tới thức ăn, nước, chó khác và sự kiện trong bán kính phụ thuộc kỹ năng. Mưa làm giảm bán kính.
- POI: thùng rác, bát nước, vòi nước công viên, quán ăn (người cho ăn), nơi trú ẩn (gầm cầu, nhà hoang), bãi phế liệu.

## 7. NPC (Phase 8)
- Người đi bộ có tính cách: thân thiện (cho ăn, vuốt ve), sợ (né), khó chịu (đuổi, gọi người), người bán hàng, kẻ trộm chó.
- Chó NPC: đi lang thang, tranh ăn, gầm gừ, đánh nhau, đi theo nếu được kết bạn.

## 8. Chiến đấu chó-với-chó (Phase 7)
- Cắn: 12 sát thương, hồi chiêu 0,8 giây, −10 thể lực, tầm 1,2 m trong cung 90° phía trước.
- Né: 0,3 giây miễn trúng, −15 thể lực, hồi chiêu 1,5 giây.
- Bị cắn liên tiếp 3 lần trong 2 giây thì choáng (stagger) 0,5 giây.
- Thoát: chạy khỏi tầm 8 m trong 3 giây thì combat kết thúc.
- **Server quyết định mọi kết quả.** Client chỉ gửi "tôi bấm cắn theo hướng X".

## 9. Vùng PvP (load từ world config)
| Loại | Ví dụ | Luật |
|---|---|---|
| Safe | công viên trung tâm, khu tân thủ | Không PvP |
| Restricted | khu dân cư | PvP chỉ khi cả hai bật "hung hăng" |
| Contested | bãi phế liệu, khu công nghiệp | PvP tự do, có lãnh thổ |

Người chơi dưới cấp 5 không bị tấn công ngoài vùng contested.

## 10. Bị bắt và giải cứu (Phase 14)
Trạng thái: bình thường → bị đe dọa (cảnh báo, có cửa sổ 5–8 giây để thoát) → bị bắt → đang được cứu → được cứu, **hoặc** tự thoát/được thả sau thời gian giới hạn (khoảng 10–20 phút, cấu hình được). Trong lúc chờ, người chơi vẫn có hoạt động khác (minigame trong chuồng, chat).
**Không xóa nhân vật, không mất đồ có giá trị lâu dài, không mất tiền thật.**

## 11. Gục và hồi sinh
HP = 0: chó bị gục 10 giây, đồng đội có thể "liếm hồi" (giữ E trong 3 giây). Nếu không ai cứu: hồi sinh ở nơi trú ẩn gần nhất với 50% HP, rơi tối đa 50% vật phẩm *tiêu hao* trong túi. Không mất cosmetic, kỹ năng hay tiền.

## 12. Xã hội (Phase 12)
- Bạn bè, chặn, mời chơi.
- **Đàn (party):** tối đa 5 (config), có leader, chia sẻ vị trí và cảnh báo.
- **Bang hội:** tối đa 20 thành viên khởi điểm (config). Vai trò owner/officer/member. Phí lập bang **100.000 VND** (mock/sandbox cho tới khi có kiểm tra pháp lý, Phase 13).
- Chat: local (khoảng cách), party, guild, global (rate limit chặt).

## 13. Tiến trình (Phase 14)
Kỹ năng: Mũi thính, Sức bền, Tốc độ, Sức mạnh, Bơi (post-MVP). Chênh lệch tối đa giữa người mới và người lâu năm khoảng 25% ở mỗi chỉ số chiến đấu, để người mới không bị áp đảo.

## 14. Kiếm tiền (monetization)
| Nguồn | Mô tả | Ràng buộc |
|---|---|---|
| Phí lập bang hội | 100.000 VND một lần | Chỉ bật thật sau kiểm tra pháp lý, thuế, hợp đồng cổng thanh toán |
| Cosmetic | vòng cổ, áo, màu lông, emblem bang | **Không** cộng chỉ số chiến đấu hoặc sinh tồn |
| (Cân nhắc) Bán game một lần hoặc gói Supporter | Steam/itch.io | Quyết định sau khi có bản chơi thử |

Không bán: sức mạnh, kỹ năng, quyền thắng lãnh thổ, rút ngắn thời gian bị bắt ở mức phá cân bằng.

## 15. Phân loại yêu cầu
Xem `docs/phases/phase-00-scope-and-acceptance.md` §2.
