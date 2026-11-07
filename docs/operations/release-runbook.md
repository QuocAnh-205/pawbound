# Runbook phát hành & vận hành — PAWBOUND 0.1.0-rc1

> Bản thử nghiệm (alpha). Chưa có máy chủ công khai. Không bật thanh toán thật (xem `docs/payments/payments.md`).

## 1. Build & kiểm thử bản phát hành
```powershell
# Windows (D:\DevTools: CMake, Ninja; MinGW GCC 15.2; Vulkan SDK)
cmake --preset win-release; cmake --build --preset win-release; ctest --test-dir build/win-release
cd backend; npm ci; npm run build; npm test; npm run test:e2e; npm run test:integration; npm run test:backup
```
```bash
# Linux dedicated server (distro WSL "PawboundBuild" trên ổ D, hoặc máy Linux bất kỳ: Ubuntu 24.04, GCC 13)
./scripts/build-server-linux.sh linux-server-release      # build + ctest
```
Đóng gói + checksum: `python scripts/gen_licenses.py && python scripts/package_release.py --version 0.1.0-rc1` → `dist/0.1.0-rc1/` (zip Windows, tar.gz Linux, `SHA256SUMS.txt`, `release.json` có commit + hash world).
Image: `docker build -f docker/server.Dockerfile -t pawbound-server:0.1.0 .` (build + chạy toàn bộ test Linux bên trong; test lỗi → không ra image), `docker build -f docker/backend.Dockerfile -t pawbound-backend:0.1.0 .`

## 2. Triển khai (một máy, docker compose)
1. Bí mật (KHÔNG commit) trong `docker/secrets/`:
   - `db_password.txt` — chuỗi ngẫu nhiên.
   - `ticket_sk.b64`, `ticket_pk.b64` — `pawbound net keygen --out docker/secrets` (đổi tên đúng).
2. `docker/stack.env`: `DATABASE_URL=postgresql://pawbound:<mật khẩu>@db:5432/pawbound?schema=public`, `JWT_SECRET`, `INTERNAL_SERVICE_TOKEN`, `MOCK_PAYMENT_WEBHOOK_SECRET` (mỗi cái ≥ 32 ký tự ngẫu nhiên), `PAWBOUND_PUBLIC_ADDRESS=<IP công khai>`, `APP_ENV=development` (mock thanh toán bị CẤM khi `production`).
3. `docker compose -f docker/compose.yml --env-file docker/stack.env up -d` — thứ tự: db (healthy) → migrate (một lần) → backend (healthy) → gameserver.
4. Kiểm tra: `curl http://127.0.0.1:3000/api/v1/health/ready`, `curl http://127.0.0.1:8081/health`, `curl http://127.0.0.1:8081/metrics`; `GET /api/v1/servers` thấy server ONLINE.
5. Mở ra Internet: chỉ UDP 27015 (game) và HTTPS qua reverse proxy (Caddy/Nginx) tới backend `127.0.0.1:3000`. Không mở DB, health, API nội bộ.

## 3. Cập nhật & rollback
- Cập nhật: build image tag mới → `migrate` (Prisma chỉ áp migration mới, idempotent) → `docker compose up -d backend gameserver`. Game server nhận SIGTERM → lưu mọi nhân vật (≤ 15 s, `stop_grace_period: 30s`) → báo `DRAINING`.
- **Sao lưu DB TRƯỚC mỗi lần migrate** (mục 4).
- Rollback ứng dụng: `PAWBOUND_VERSION=<tag cũ> docker compose up -d backend gameserver`. Migration hiện tại chỉ THÊM bảng/cột → mã cũ chạy được trên schema mới. Nếu một bản sau có migration phá vỡ: khôi phục DB từ bản sao lưu trước migrate.
- Giao thức client–server có version (`kProtocolVersion`); client cũ bị từ chối rõ ràng "sai phiên bản giao thức" → phát hành client mới cùng lúc với server.

## 4. Sao lưu & khôi phục PostgreSQL (đã test: `npm run test:backup`)
```bash
# sao lưu hằng ngày (giữ 14 bản), định dạng custom nén
docker compose exec -T db pg_dump -U pawbound -Fc -Z 6 pawbound > backup/pawbound-$(date -u +%Y%m%dT%H%MZ).dump
find backup -name 'pawbound-*.dump' -mtime +14 -delete
# khôi phục vào DB MỚI rồi chuyển hướng (không ghi đè DB đang chạy)
docker compose exec -T db createdb -U pawbound pawbound_restore
docker compose exec -T db pg_restore -U pawbound --no-owner --exit-on-error -d pawbound_restore < backup/<file>.dump
```
Bài test tự động khôi phục vào DB mới và kiểm: mọi bảng khớp số dòng + checksum, trigger append-only của sổ cái còn hoạt động, UNIQUE còn, sequence tiếp tục đúng, `migrate deploy` sau khôi phục không lỗi. Lưu bản sao lưu ra **ngoài máy chủ** (object storage có mã hóa).
Redis: dự án **không dùng Redis** ở phiên bản này (rate limit trong bộ nhớ) — không có quy trình phục hồi Redis.

## 5. Giám sát
- Game server `/metrics` (Prometheus): người online/đang nạp/đang bị bắt, NPC, kẻ trộm, tick p50/p95/p99, gói bị từ chối/kick/sai, chat, hàng đợi Backend, RAM, uptime. `/health` = 503 nếu vòng lặp game không cập nhật > 10 s.
- Ngưỡng cảnh báo gợi ý: tick p95 > 20 ms (5 phút), `pawbound_backend_pending` > 100, RAM tăng liên tục, `/health` lỗi.
- Backend: log JSON một dòng mỗi request (không body), audit log trong DB (thanh toán, quản trị, lãnh thổ).
- Crash: `logs/crash-*.txt` cạnh tiến trình (không dữ liệu cá nhân).

## 6. Sự cố thường gặp
| Triệu chứng | Xử lý |
|---|---|
| Người chơi bị "không nạp được dữ liệu nhân vật" | kiểm `backend` healthy, `INTERNAL_SERVICE_TOKEN` trùng ở hai bên |
| Log `xung đột stateVersion` | hai server ghi cùng nhân vật — kiểm tra người chơi đăng nhập hai nơi; server tự dừng lưu + kick |
| Webhook `REJECTED_*` trong báo cáo thanh toán | đối soát với provider; không sửa sổ cái (append-only) — xử lý qua hoàn tiền/admin |
| `/health` 503 | vòng lặp game treo: lấy `logs/`, khởi động lại container (dữ liệu đã autosave ≤ 60 s) |

## 7. Checklist trước khi mở cho người chơi thật (CHƯA đạt — cần người vận hành)
- [ ] Chính sách quyền riêng tư, điều khoản sử dụng, quy tắc cộng đồng — **cần luật sư review**
- [ ] Kiểm duyệt nội dung chat (dịch vụ/đội ngũ), kênh hỗ trợ người chơi, quy trình xử lý báo cáo
- [ ] Cổng thanh toán thật: hợp đồng merchant, sandbox, chính sách hoàn tiền, thuế/hóa đơn, chính sách cửa hàng ứng dụng
- [ ] Phân loại độ tuổi (IARC/cửa hàng), quy định trò chơi điện tử trực tuyến tại Việt Nam (giấy phép G1 nếu có thanh toán/online đại chúng)
- [ ] Pentest độc lập; chống DDoS cho UDP
- [ ] Máy chủ thật (Linux, ≥ 4 vCPU) — đo lại load test 20–30 người trên đó
- [ ] Bản Android/iOS (chưa có)
