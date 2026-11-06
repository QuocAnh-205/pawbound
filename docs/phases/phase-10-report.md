# Phase 10 — Báo cáo: Multiplayer, ENet & Authoritative Dedicated Server

## A. Kết luận: **PARTIAL**
Server authoritative, protocol, bảo mật handshake, load test 2→30 người: **đạt** trên máy dev. Lý do chưa PASS toàn phần:
1. **Linux dedicated server chưa build/chạy thật** (Dockerfile đã viết; Docker Desktop lưu dữ liệu trên ổ C gần đầy).
2. **Chưa có client prediction/reconciliation**: điều khiển online có độ trễ ≈ RTT + 1 tick. Đúng chủ trương "chỉ bật prediction sau khi baseline ổn định", nhưng là thiếu sót so với trải nghiệm chơi.
3. Load test chạy **server + bot trên cùng một laptop Windows** (localhost), không phải máy tham chiếu Linux 4 vCPU trong thiết kế.

## B. Đã triển khai
Xem `docs/networking/networking.md`. Dependency mới: ENet 1.3.18 (MIT, build từ source), libsodium 1.0.22 (ISC; gói MinGW chính thức, **chữ ký minisign đã xác minh** bằng `tools/verify_minisign.js`, thử thêm với file bị sửa thì báo SAI).

## D. Lệnh và kết quả thật
| Lệnh | Kết quả |
|---|---|
| Build Debug/Release | OK, 0 warning |
| `pb_tests` | xem mục cập nhật cuối báo cáo |
| Test tích hợp `server:` (UDP thật, server trong process) | 5/5 PASS: hai client thấy nhau di chuyển (3–7,5 m sau 1 s chạy); input √2 không tăng tốc; gói Snapshot giả không đổi HP; gói rác → kick, client khác vẫn chơi; từ chối ticket bị sửa/hết hạn/sai server/world khác/dùng lại; reconnect giữ nhân vật và vị trí |
| Test đơn vị `net:` | 7/7 PASS: round-trip mọi gói; gói sai; **fuzz 20.000 gói** + mọi độ dài cắt cụt; sequence quấn vòng; ticket Ed25519 (7 kịch bản); token bucket; NetSim |
| `scripts/loadtest.py` | xem `docs/networking/loadtest-results.md` |

### Load test (Release, laptop i7 12 luồng, Windows 11, server + bot cùng máy, mỗi nấc 45 s)
Chi tiết: `docs/networking/loadtest-results.md`. Lần chạy đầu (giữ lại ở `loadtest-results-v1-15hz.md`) để lộ 2 lỗi, đã sửa trước khi lấy số cuối:
1. **Snapshot chỉ ~14,5 Hz** thay vì 20 Hz (lịch `now + 50 ms` bị làm tròn theo tick 33 ms). Đã sửa thành lịch cộng dồn; thêm gửi entity xa (> 25 m) 10 Hz theo đúng thiết kế.
2. **Script tính sai đơn vị băng thông** (KiB × 8 / 1000). Tính đúng thì 30 người lúc đó là ~3,0 Mbit/s, **vượt** ngưỡng 3 Mbit/s, tức "PASS" của lần chạy đó là sai. Đã sửa công thức và nén vị trí entity sang độ lệch int16 (20 → 16 byte/entity).

| Người | Mạng | Tick p95 / p99 (ms) | RAM | Ra | Snapshot/bot/s | Vào game / rớt | Ngưỡng §7 |
|---|---|---|---|---|---|---|---|
| 2 | sạch | 0,17 / 0,31 | 7 MB | 0,04 Mbit/s | 19,8 | 2/2, 0 | PASS |
| 8 | sạch | 0,14 / 0,42 | 7 MB | 0,27 Mbit/s | 19,8 | 8/8, 0 | PASS |
| 16 | sạch | 0,22 / 0,52 | 7 MB | 0,80 Mbit/s | 19,7 | 16/16, 0 | PASS |
| 20 | sạch | 0,25 / 0,56 | 7 MB | 1,15 Mbit/s | 19,6 | 20/20, 0 | PASS |
| 30 | sạch | 0,76 / 1,16 | 7 MB | 2,48 Mbit/s | 19,3 | 30/30, 0 | PASS |
| 20 | mất 2%/5%, trễ 50±10 ms | ≤ 0,35 / 0,62 | 7 MB | 1,18 Mbit/s | 18,5–19,1 | 20/20, 0 | PASS |
| 30 | mất 2%/5%, trễ 50±10 ms | ≤ 0,22 / 0,41 | 7 MB | 2,49 Mbit/s | 18,3–18,9 | 30/30, 0 | PASS |

Ghi chú trung thực:
- Bot tụ quanh điểm spawn nên **mọi bot nằm trong AOI của nhau** (trường hợp xấu cho băng thông). Tick time rất thấp vì world MVP nhỏ (512 m) và AI rẻ.
- Đây là số đo trên **máy dev**, chưa phải máy Linux 4 vCPU tham chiếu, và chưa qua Internet thật. RTT trong bảng là localhost (+ trễ mô phỏng).
- Mục tiêu 20–30 người/server **đạt trên máy này**; cần đo lại trên máy chủ thật trước khi công bố.

### Online với client thật
`scripts/net_smoke.py` (CTest `net_online_smoke`): server + 3 bot + client có renderer kết nối, nhận Welcome, chạy 240 frame, 0 lỗi validation. HUD "ONLINE #4 · 4 players nearby". Ảnh: `build/win-debug/screenshots/online.png`.

### Toàn bộ
- `pb_tests`: **97/97 test case pass**.
- `ctest`: **15/15 pass**.

## F. Known issues
- Chưa có **client prediction/reconciliation** → điều khiển trễ ≈ RTT + tick. Motor deterministic dùng chung nên đã sẵn sàng để thêm.
- Linux build + Docker chưa chạy thật (`docker/server.Dockerfile` chưa build). GCC bản bookworm có thể không đủ `<format>`; đã ghi chú trong Dockerfile.
- Luật gameplay phía server (`tick_player`) **trùng lặp một phần** với `GameSession` (single-player). Nợ kỹ thuật: gộp thành module luật chung ở Phase 15.
- Chưa có delta compression (chưa cần theo số đo). Chưa mã hóa payload UDP (ADR-004).
- Trạng thái người chơi chưa lưu bền: server khởi động lại thì mất. Phase 11 (Backend persistence).
- Hello được kiểm tra world hash trước chữ ký ticket: client không có ticket vẫn biết được world hash khớp hay không. Rủi ro thấp (world là dữ liệu công khai), nhưng nên đảo thứ tự.

## G. Tiêu chí
| Tiêu chí | Trạng thái | Bằng chứng |
|---|---|---|
| Hai client thấy nhau di chuyển | PASS | test `server:` + smoke online |
| Combat và survival do server quyết định | PASS | không có gói client đặt HP/vị trí; `tick_player` |
| Không set HP / teleport / tạo item bằng gói giả | PASS | test kẹp input, gói Snapshot giả |
| Reconnect/disconnect không crash | PASS | test reconnect; bot rớt/kết nối lại |
| Gói sai/quá cỡ bị từ chối an toàn | PASS | fuzz + test kick |
| Benchmark từng nấc người chơi | PASS (trên máy dev) | `loadtest-results.md` |
| PASS 20–30 người theo ngưỡng Phase 0 | PASS trên máy dev; **chưa đo trên máy chủ tham chiếu** | |
| Linux server + Dockerfile | **PARTIAL** (đã viết, chưa build thật) | |
