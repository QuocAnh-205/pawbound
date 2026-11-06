# Backend PAWBOUND (Phase 11)

Thư mục `backend/`. NestJS 11.2.8 + Prisma 6.19.3 + PostgreSQL 18.4, TypeScript 5.9.3, Node 24.

## Vai trò
| Thành phần | Trách nhiệm |
|---|---|
| Backend (HTTP/JSON) | Tài khoản, đăng nhập, nhân vật, matchmaking, **cấp session ticket Ed25519**, lưu trạng thái nhân vật, audit log, admin |
| Game server (UDP/ENet) | Mô phỏng authoritative. **Không** đọc DB trực tiếp: chỉ gọi API nội bộ `/api/v1/internal/*` bằng `X-Service-Token` |
| Client | Chỉ gọi API công khai bằng JWT; vào game bằng ticket nhận từ `matchmaking/join` |

## API (tiền tố `/api/v1`, Swagger tại `/api/docs`)
| Nhóm | Endpoint | Bảo vệ |
|---|---|---|
| Health | `GET /health`, `GET /health/ready` (kiểm tra DB) | công khai |
| Auth | `POST /auth/register` (5/phút/IP), `POST /auth/login` (10/phút/IP), `POST /auth/refresh`, `POST /auth/logout` | throttle |
| Người dùng | `GET /me` | JWT |
| Nhân vật | `GET/POST /characters`, `GET/PATCH/DELETE /characters/:id` (tối đa 3, xóa mềm, của người khác → 404) | JWT |
| Matchmaking | `POST /matchmaking/join` → `{server, ticket, expiresAt}`; `GET /servers` | JWT |
| Nội bộ | `POST /internal/servers/heartbeat`, `GET/PUT /internal/characters/:numericId/state`, `POST /internal/sessions/:jti/end` | `X-Service-Token` |
| Admin | `GET /admin/users`, `POST /admin/users/:id/ban` | JWT + role ADMIN |

## Bảo mật
- Mật khẩu: argon2id. Access token JWT HS256 15 phút; refresh token ngẫu nhiên, chỉ lưu **SHA-256**, xoay vòng mỗi lần dùng, **dùng lại token cũ ⇒ thu hồi cả họ token** (phát hiện đánh cắp).
- Ticket vào game: Ed25519 (libsodium bên C++, `node:crypto` bên Node — định dạng khóa libsodium được chuyển sang PKCS#8). Payload `{v,sub,chr,srv,exp,jti}`, TTL 60 s, server từ chối dùng lại `jti`. Tương thích hai chiều đã kiểm tra bằng `pawbound net verify-ticket`.
- Token dịch vụ so sánh thời gian hằng (so SHA-256 bằng `timingSafeEqual`).
- `ValidationPipe` whitelist + forbidNonWhitelisted; log request **không** ghi body; lỗi trả `{code,message}` không lộ stack.
- Bí mật chỉ nằm trong `backend/.env` (đã .gitignore) hoặc biến môi trường. Game server đọc token từ `PAWBOUND_SERVICE_TOKEN` (không qua tham số dòng lệnh).

## Lưu trạng thái nhân vật
- `DogCharacter.state` (JSON) + `stateVersion` (optimistic concurrency). Backend kiểm tra cấu trúc (`validateState`): vị trí, chỉ số 0–100, inventory ≤ 6 ô, ≤ 16 KB.
- `PUT state` cần `expectedVersion` + `idempotencyKey`: gửi lại cùng key ⇒ trả kết quả cũ, không ghi lặp (bảng `SaveOperation`); version lệch ⇒ **409** `STATE_VERSION_CONFLICT`.
- Game server (`src/server/backend_link.*`): HTTP chạy trên **một thread nền**, vòng tick chỉ đẩy job/nhận kết quả.
  - Vào game: nạp trạng thái trước khi gửi `Welcome` (chưa nạp xong thì không mô phỏng, không xuất hiện với người khác). Dữ liệu nạp vẫn được kiểm tra lại: vị trí ngoài world ⇒ về spawn; inventory sai luật (vd vượt `max_stack`) ⇒ túi rỗng + cảnh báo.
  - Nhân vật không có trong DB (404) ⇒ kick, không tạo "nhân vật ma".
  - Lưu định kỳ (`--autosave`, mặc định 60 s), lưu lần cuối khi bị gỡ sau thời gian giữ chỗ, và lưu hết khi tắt (`quit`/SIGINT) rồi báo `DRAINING`.
  - 409 ⇒ dừng lưu nhân vật đó và kick (tránh ghi đè dữ liệu do server khác ghi). Lỗi mạng/5xx ⇒ thử lại 3 lần trong worker, sau đó thử lại ở lượt sau.
  - Hàng đợi FIFO một thread ⇒ lần lưu cuối của phiên cũ luôn chạy **trước** lần nạp của phiên mới cùng nhân vật.
- Heartbeat 10 s; matchmaking chỉ chọn server `ONLINE` có heartbeat trong 30 s, ưu tiên đúng vùng và tải thấp.

## Chạy local
```powershell
# PostgreSQL portable: D:\DevTools\pgsql, dữ liệu D:\DevTools\pgdata, cổng 54329
D:\DevTools\pgsql\bin\pg_ctl.exe -D D:\DevTools\pgdata -l D:\DevTools\pgdata\server.log start
cd backend
npm ci --cache D:\DevTools\npm-cache
copy .env.example .env   # rồi điền giá trị thật
npx prisma migrate deploy
npm run build; npm start   # http://127.0.0.1:3000/api/docs

# Game server nối backend (token lấy từ INTERNAL_SERVICE_TOKEN trong .env)
$env:PAWBOUND_SERVICE_TOKEN = "<token>"
build\win-debug\bin\pawbound_server.exe --world out\world_demo --backend http://127.0.0.1:3000/api/v1 --server-id dev-1
```
API nội bộ dùng `http://` — chỉ đặt trong mạng riêng (cùng máy/VPC) hoặc sau proxy TLS; không mở ra Internet.

## Kiểm thử
| Lệnh | Nội dung |
|---|---|
| `npm test` | Unit: ticket Ed25519 (ký/xác minh, sửa payload, hết hạn, sai server) |
| `npm run test:e2e` | E2E HTTP với PostgreSQL thật trên database tạm (tạo → migrate deploy → xóa) |
| `npm run test:integration` | Backend + `pawbound_server --backend` + `pawbound_bot` + PostgreSQL thật (database tạm) |
