# Thế giới động (Phase 9)

Mã: `src/sim/world_sim.{hpp,cpp}` (logic thuần, chạy được trên server). Dữ liệu: `data/gameplay/world_sim.json`. Client: ánh sáng (`apply_lighting`), mưa/chớp, cột sự kiện, HUD (`game_session.cpp`).

## Đồng hồ
`WorldClock`: 1 ngày game = `day_length_real_s` giây thật (mặc định 2400 s = 40 phút). Có tăng ngày khi qua nửa đêm, đặt giờ, và lưu/nạp (ngày, giờ). Chỉ nhận dt mô phỏng, không phụ thuộc FPS.

## Ánh sáng
`lighting_for(giờ, hiệu ứng thời tiết)`: hướng và màu mặt trời (6h mọc, 18h lặn; đêm là mặt trăng), màu trời, ambient, sương mù, mức bật đèn đường (đêm hoặc trời âm u). Bão: chớp làm tăng exposure ngắn.

## Thời tiết
Máy trạng thái clear / cloudy / rain / fog / storm. Mỗi trạng thái có thời lượng [min, max] và bảng xác suất trạng thái kế tiếp. **Blend 90 s** khi chuyển (test: mỗi tick đổi < 0,001). Deterministic theo seed thế giới.

Ảnh hưởng gameplay (blend):
| | tầm nhìn | mùi (bán kính đánh hơi) | tốc độ | người đi bộ |
|---|---|---|---|---|
| mưa | 0,7 | 0,6 | 0,95 | 0,5 |
| sương | 0,45 | 1,2 | 1 | 0,7 |
| bão | 0,5 | 0,4 | 0,9 | 0,2 |

## Lịch hoạt động
- Mật độ người đi bộ theo giờ (24 giá trị) × thời tiết. NPC có "ngưỡng hiện diện" deterministic: mật độ giảm thì người có ngưỡng cao rời trước, sáng ra **đúng những người đó** quay lại (test).
- Thức ăn hồi ×3 trong giờ ăn (7–9, 11–13, 17–20).

## Sự kiện động
| Sự kiện | Khu vực | Hoàn thành |
|---|---|---|
| `lost_puppy` | dân cư, công viên (6–21h) | tới gần, bấm E → +xương |
| `food_hotspot` | trung tâm, dân cư, công nghiệp | tạo nguồn thức ăn tạm (5 phần), gỡ khi hết giờ |
| `dog_conflict` | công nghiệp, dân cư | (Phase 14: chó hung dữ tụ tập) |
| `rescue_signal` | công nghiệp, trung tâm | tới gần, bấm E (Phase 14 mở rộng thành giải cứu) |
| `area_closure` | trung tâm (7–20h) | hết giờ |

Mỗi 45 s kiểm tra; tối đa 2 sự kiện cùng lúc. Chọn theo trọng số; **cooldown** theo loại; **chống lặp** cùng loại liên tiếp. Vị trí bám nút nav (đi được), đúng district, không trên hồ, cách người chơi 12–90 m, không chồng sự kiện khác.

## Lưu trạng thái
`WorldSimSnapshot` → JSON (version 1): ngày, giờ, thời tiết + thời gian còn lại + blend, trạng thái RNG (chuỗi, giữ đủ 64 bit), cooldown, sự kiện đang chạy, các nguồn tài nguyên đang vơi. Không lưu hạt mưa hay chi tiết đồ họa. Test: lưu → nạp vào sim seed khác → chạy tiếp 10 phút cho kết quả giống hệt.

## Debug / QA
Phím: T (+1 giờ), Y (thời tiết kế tiếp), U (sự kiện ngẫu nhiên gần người chơi). Tham số: `--time H`, `--weather clear|cloudy|rain|fog|storm`, `--event <loại>`.
