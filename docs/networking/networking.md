# Multiplayer & Dedicated Server (Phase 10)

Mã: `src/net/` (protocol, ticket, rate limit, mô phỏng mạng), `src/server/` (`pb_server` + `pawbound_server`), `src/bot/` (load test), `src/client/net_game.*` (client online). Thiết kế: `docs/architecture/multiplayer-design.md`, ADR-004.

## Chạy thử trên một máy
```powershell
.\build\win-release\bin\pawbound.exe net keygen                         # một lần: tạo khóa DEV (config/dev_keys, đã .gitignore)
.\build\win-release\bin\pawbound.exe world generate --seed 20261106 --config data\world\demo.json --output out\world_demo
.\build\win-release\bin\pawbound_server.exe --world out\world_demo      # cổng UDP 27015; gõ "status", "players", "kick <id>", "quit"
.\build\win-release\bin\pawbound.exe net ticket --user 1 --char 1 > t1.txt
.\build\win-release\bin\pawbound_client.exe --world out\world_demo --connect 127.0.0.1:27015 --ticket @t1.txt
.\build\win-release\bin\pawbound_bot.exe --count 8 --duration 60         # thêm 8 người chơi bot
```

## Luồng kết nối
1. ENet connect → server tạo `Conn` (chưa xác thực). Không gửi Hello trong 5 s thì bị kick.
2. `Hello{protocol_version, ticket, world_hash}`. Server kiểm tra theo thứ tự:
   - version;
   - world hash (client phải có đúng world);
   - **chữ ký Ed25519 của ticket** (libsodium, chỉ cần public key);
   - hạn dùng, `srv` = server id, `jti` chưa dùng (replay cache).
3. Hợp lệ → `Welcome{player_id}`. Cùng `character_id` đang offline thì **gắn lại nhân vật cũ** (reconnect, giữ 60 s). Đang online ở nơi khác thì phiên cũ bị kick.
4. Rớt mạng: ENet timeout 10 s → nhân vật "offline", giữ 60 s rồi gỡ (Phase 11 sẽ lưu về Backend trước khi gỡ).

## Server authoritative
Client chỉ gửi `InputCmd{seq, hướng di chuyển int8, nút, yaw}`, mỗi gói kèm 3 lệnh gần nhất. Server:
- **Kẹp** độ dài hướng ≤ 1 → không thể chạy nhanh hơn tốc độ cho phép (test: input √2 chỉ cho tốc độ nước rút chuẩn).
- Chỉ nhận lệnh có `seq` mới hơn (chống trùng/replay), hàng đợi tối đa 8, mỗi tick xử lý 1 lệnh. Mất gói thì giữ lệnh trước tối đa 3 tick (không lặp hành động một lần).
- Tự tính: di chuyển + va chạm (`step_motor`), sinh tồn, thể lực (nước rút/nhảy), cắn (server **tự chọn mục tiêu** gần nhất, luật PvP theo vùng), uống/ăn, NPC, thời tiết, sự kiện, hồi sinh.
- Không tồn tại gói nào cho client đặt vị trí, HP hay item. Client gửi loại gói không được phép (vd Snapshot giả) bị tính vi phạm.

## Bảo vệ
| Cơ chế | Giá trị |
|---|---|
| Kích thước gói | header 16 + payload ≤ 1200 byte; vượt thì bị từ chối trước khi parse |
| Rate limit mỗi peer | 150 gói/s (burst 90), 64 KB/s (burst 32 KB), kiểm tra trước khi parse |
| Gói sai (header/parse/loại không cho phép) | cộng điểm vi phạm; ≥ 10 → kick có lý do |
| Bit nút lạ, số lệnh 0 hoặc > 3, byte thừa | gói bị từ chối |
| Fuzz | 20.000 gói ngẫu nhiên + mọi độ dài cắt cụt: không crash (unit test) |

**Không mã hóa payload UDP ở MVP** (ADR-004): người nghe lén thấy được vị trí và hành động, nhưng không lấy được mật khẩu/token dài hạn (ticket hết hạn sau ~60 s và chỉ dùng một lần). **Không tuyên bố chống cheat hoàn toàn**: bot, macro và "wallhack" trong phạm vi AOI vẫn có thể có.

## Đồng bộ
- Tick 30 Hz; snapshot **20 Hz** mỗi người chơi; AOI bán kính 60 m, tối đa 50 entity gần nhất (20 byte/entity).
- Giờ thế giới, ngày, thời tiết nằm trong mọi snapshot. Sự kiện, cắn, cho ăn gửi bằng Event reliable.
- Client: bản thân = snapshot mới nhất; người khác/NPC = **nội suy trễ 100 ms**. **Chưa có client prediction/reconciliation**: điều khiển có độ trễ ≈ RTT + tick (xem known issues).

## Vận hành
- Console: `status`, `players`, `kick <id>`, `quit` (đọc trên thread riêng, xử lý trên thread mô phỏng).
- Log metrics mỗi 10 s: người chơi, NPC, tick p50/p95/p99/max, gói và KB vào/ra mỗi giây, RAM, số từ chối/kick/gói sai. `--metrics file.jsonl` ghi JSON lines cho load test.
- Mô phỏng mạng xấu: `--sim-loss 0.05 --sim-latency 50 --sim-jitter 10` (server và bot).
