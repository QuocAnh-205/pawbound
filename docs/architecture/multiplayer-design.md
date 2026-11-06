# PAWBOUND — Multiplayer Design (v0.1, Phase 0)

Xem ADR-004 về transport.

## 1. Mô hình
Dedicated Server **authoritative**. Client gửi input, server mô phỏng và gửi snapshot. Client không bao giờ gửi vị trí, HP hay kết quả.

## 2. Tick và tần suất
| Thông số | Giá trị khởi điểm |
|---|---|
| Server sim tick | 30 Hz |
| Client gửi input | 30 Hz (mỗi packet kèm 3 input gần nhất để chống mất gói) |
| Snapshot | 20 Hz cho entity ≤ 25 m; 10 Hz cho 25–60 m; ngoài 60 m không gửi |
| Interpolation delay | 100 ms (≈ 2 snapshot) |
| Timeout | 10 giây không nhận packet thì disconnect |
| Reconnect window | 60 giây giữ nhân vật trên server (trạng thái "lingering", vẫn có thể bị tấn công trong vùng contested để chống thoát combat) |

## 3. Packet (trên ENet)
Header 16 byte: `u16 protocol_version · u16 type · u32 sequence · u32 ack · u32 server_tick`, sau đó payload dài tối đa 1200 byte.

| Type | Hướng | Kênh ENet | Nội dung |
|---|---|---|---|
| Hello | C→S | reliable | protocol_version, session_ticket, world_hash, client_build |
| Welcome / Reject | S→C | reliable | player_entity_id, world_time, weather; hoặc lý do từ chối |
| Input | C→S | unreliable | seq, move_x/move_z (int8), buttons bitmask, aim_yaw (u16) |
| Snapshot | S→C | unreliable | tick, ack_input_seq, danh sách entity (id, kiểu, pos cm, yaw, anim, flags), delta theo baseline đã ack |
| Event | S→C | reliable | sát thương, chết, nhặt đồ, sự kiện thế giới, chat |
| Action | C→S | reliable | interact(entity_id), use_item(slot), chat(text) |
| Ping/Pong | 2 chiều | unreliable | đo RTT |

Mọi payload được parse bằng reader có kiểm tra biên. Lỗi parse thì tăng bộ đếm vi phạm; vượt ngưỡng thì kick.

## 4. Kiểm tra phía server
- Tốc độ: vị trí mới do server tính từ input, nên client không thể teleport. Input bị kẹp (clamp) về khoảng hợp lệ.
- Tần suất input: tối đa 40 input/giây (dư 33%). Vượt ngưỡng thì drop và ghi vi phạm.
- Cắn: server kiểm tra cooldown, thể lực, khoảng cách và góc theo **vị trí đã lag-compensate có giới hạn** (tối đa rewind 150 ms).
- Tương tác: khoảng cách ≤ 2 m, entity tồn tại, còn tài nguyên, đúng cooldown.
- Sequence: bỏ packet cũ hoặc trùng (cửa sổ 1024). Ticket chỉ dùng một lần (nonce cache).
- Rate limit: tối đa 64 KB/giây và 120 packet/giây mỗi client; chat 1 tin/giây, burst 5.

## 5. Interest management (AOI)
Lưới 32 m. Mỗi client nhận entity trong 3×3 ô quanh nó (≈ 96 m, lớn hơn vùng nhìn của camera 2.5D). Entity ra khỏi vùng thì gửi `Despawn`.

## 6. Ước lượng băng thông (sẽ đo thật ở Phase 10)
Mỗi entity trong snapshot khoảng 14 byte (delta còn khoảng 6). Giả sử 30 entity nhìn thấy ở 20 Hz: ≈ 30 × 10 × 20 = 6 KB/giây xuống mỗi client. 30 client ≈ 180 KB/giây ra từ server. Đây là **ước lượng, không phải số đo**.

## 7. Ngưỡng nghiệm thu benchmark (dùng ở Phase 10 và 15)
Máy server tham chiếu: 4 vCPU, 8 GB RAM, Linux. Bot headless gửi input ngẫu nhiên có mục đích (đi lại, cắn, ăn).

| Số người | Tick p95 | Tick p99 | RAM server | Băng thông ra | Mất gói giả lập | Tiêu chí khác |
|---|---|---|---|---|---|---|
| 2 / 8 / 16 | < 10 ms | < 20 ms | < 1 GB | < 1 Mbit/s | 0% và 2% | không desync, không crash |
| 20 | < 15 ms | < 25 ms | < 1,5 GB | < 2 Mbit/s | 2% và 5% | |
| 30 | < 20 ms | < 30 ms | < 2 GB | < 3 Mbit/s | 2% và 5% | soak test 2 giờ không leak > 5% |

Tick budget là 33,3 ms. Chỉ đánh **PASS** cho mức 20–30 người khi đo đạt các ngưỡng trên.

## 8. Đồng bộ thế giới
- `world_time` và `weather_state` nằm trong snapshot mỗi 1 giây. Client nội suy ánh sáng.
- Sự kiện động gửi qua Event (reliable) cho người chơi trong AOI hoặc cùng party/guild.

## 9. Test mạng
- Harness `pawbound_bot` (Phase 10): chạy N client headless trên một máy.
- Giả lập trễ, jitter và mất gói ngay trong lớp transport (lớp bọc có cấu hình), không phụ thuộc công cụ hệ điều hành.
- Kịch bản: disconnect giữa combat, reconnect trong và ngoài cửa sổ 60 giây, packet quá cỡ, packet sai version, ticket hết hạn hoặc dùng lại.
