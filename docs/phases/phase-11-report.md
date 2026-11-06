# Phase 11 — Báo cáo: Backend, Database, Auth, Persistence

## A. Kết luận: **PASS** (trên máy dev, chưa deploy)
Backend NestJS + PostgreSQL thật chạy được; game server C++ nạp/lưu trạng thái qua API nội bộ; mọi test unit, e2e và tích hợp đều pass với database thật. Chưa triển khai lên server thật (không thuộc phạm vi; master prompt cấm deploy production).

## B. Đã triển khai
- `backend/`: auth (argon2id, JWT, refresh xoay vòng + phát hiện dùng lại), nhân vật (tối đa 3, khóa hàng `FOR UPDATE` chống tạo vượt giới hạn khi gửi song song), matchmaking + ticket Ed25519, API nội bộ cho game server, admin (phân trang, ban), audit log, health/ready, Swagger. Chi tiết: `docs/backend/backend.md`.
- Prisma schema 11 bảng + migration `20261106191503_init`.
- C++: `src/net/http_client.*` (HTTP/1.1 tối giản, timeout, giới hạn 1 MiB, hỗ trợ chunked), `src/server/backend_link.*` (thread nền), tích hợp vào `GameServer` (nạp trước Welcome, autosave, lưu khi rời/tắt, heartbeat, đóng phiên, xử lý 404/409/5xx).
- `pawbound net verify-ticket` (kiểm tra ticket do Node ký), `pawbound_bot --ticket` (dùng ticket do Backend cấp).
- Dependency mới (đều pin): NestJS 11.2.8, Prisma 6.19.3, TypeScript 5.9.3, Jest 29, argon2 0.45.2, class-validator 0.15.1, @nestjs/throttler 6.7.1, @nestjs/swagger 11.4.8. Không lên NestJS 12 (chỉ ESM), Prisma 8 (RC), TS 7 (thay đổi lớn).

## D. Lệnh và kết quả thật
| Lệnh | Kết quả |
|---|---|
| `npm run lint:types` | OK |
| `npm test` | 5/5 PASS |
| `npm run test:e2e` | 13/13 PASS (PostgreSQL 18.4 thật, database tạm) |
| `npm run test:integration` | **25/25 PASS**, chạy 2 lần liên tiếp ổn định |
| `ctest --test-dir build/win-debug` | 15/15 PASS (không hồi quy) |
| Build C++ Debug | OK, 0 warning |

Test tích hợp kiểm tra: backend lên với DB tạm → server gửi heartbeat, xuất hiện ONLINE → đăng ký, tạo chó, matchmaking cấp ticket → bot vào game bằng ticket đó → server nạp nhân vật mới (v0) → autosave + lưu cuối khi rời, đóng phiên → sửa trạng thái trong DB, vào lại: server nạp đúng bản đã lưu (hp, túi đồ giữ nguyên) → ticket hợp lệ nhưng nhân vật không tồn tại bị từ chối (404) → tắt server khi có người online: lưu xong, báo DRAINING, không còn phiên mở.

## E. Lỗi gặp và cách xử lý
1. `pg_ctl start` treo công cụ PowerShell → khởi chạy qua `Start-Process`.
2. Prisma chặn `migrate reset` (bảo vệ dữ liệu) → **không vượt rào**; test dùng database tạm + `migrate deploy`, chỉ xóa database do chính test tạo.
3. Throttle đăng ký làm e2e hỏng → giới hạn đọc từ biến môi trường (mặc định an toàn 5/phút).
4. Test tích hợp lần đầu 7 FAIL, **đều do script test**: sai đường dẫn health, mong 201 thay vì 200, đọc trạng thái trong khi lần lưu cuối chưa xong, và dữ liệu thử `meat_bone ×3` vượt `max_stack = 2` — server **đúng** khi từ chối túi đồ sai luật. Đã sửa script, không nới logic server.

## F. Rủi ro / còn thiếu
- API nội bộ là `http://` — bắt buộc chạy trong mạng riêng hoặc sau proxy TLS.
- Nếu Backend sập lâu hơn số lần thử lại khi người chơi bị gỡ, thay đổi kể từ lần autosave cuối bị mất (tối đa 60 s chơi). Log ghi rõ `THẤT BẠI`.
- Chưa có reset mật khẩu qua email, xác minh email, 2FA (cần dịch vụ email thật — không đăng ký dịch vụ ngoài theo quy tắc).
- Chưa deploy, chưa backup DB tự động.
