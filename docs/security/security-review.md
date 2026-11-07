# Security review — PAWBOUND 0.1.0-rc1 (Phase 15)

Người rà soát: AI (Claude) theo yêu cầu dự án — **không thay thế** kiểm thử xâm nhập độc lập hay review pháp lý. Phạm vi: mã trong repo tại commit phát hành, cấu hình mẫu, dependency.

## Phương pháp
Đọc mã theo từng biên tin cậy (client → game server UDP, client → backend HTTP, game server → backend nội bộ, provider → webhook), đối chiếu test có sẵn, chạy `npm audit`, quét bí mật toàn bộ lịch sử git, mutation test cho các kiểm tra quan trọng (khóa hàng guild, kiểm tra số tiền webhook).

## Kết quả theo hạng mục
| Hạng mục | Hiện trạng | Bằng chứng |
|---|---|---|
| Xác thực tài khoản | argon2id, JWT HS256 15', refresh xoay vòng + thu hồi cả họ khi dùng lại; throttle đăng ký/đăng nhập | e2e auth |
| Session ticket | Ed25519 (libsodium/Node crypto), TTL 60 s, `jti` một lần, ràng buộc server id; kiểm tra chéo C++↔Node | unit + e2e + `net verify-ticket` |
| Kiểm tra gói mạng | header/độ dài/loại/bit lạ; fuzz 20.000 gói; token bucket theo kết nối; điểm vi phạm → kick | `pb_tests` net |
| Server authoritative | client chỉ gửi ý định; vị trí/sinh tồn/bắt chó/thưởng do server quyết định | tích hợp |
| Phân quyền guild/party | ma trận quyền tập trung, khóa hàng trong transaction, field lạ → 400 | e2e + mutation test |
| Inventory / thưởng | thao tác idempotent; thưởng khóa `refKey` UNIQUE; state nạp lại được kiểm tra lại | unit + e2e + tích hợp |
| Thanh toán | xem `payment-threat-model.md` (20 mối đe dọa) | e2e payments |
| Chat | làm sạch Unicode (bidi/zero-width), rate limit theo user (advisory lock), mute, chặn hai chiều cả ở game server | e2e + `net_local_chat` |
| Bí mật | chỉ ở `.env`/biến môi trường/secret file; token dịch vụ không qua dòng lệnh; đóng gói từ chối file khóa | quét git (dưới) |
| Riêng tư | log HTTP không ghi body; không log nội dung chat gần; thông báo bị bắt không có tọa độ; crash report không có dữ liệu cá nhân | review mã |

## Phát hiện và trạng thái xử lý
| ID | Mức | Phát hiện | Trạng thái |
|---|---|---|---|
| S-01 | High | `multer` 2.4.0 (qua `@nestjs/platform-express`) — DoS GHSA-q7qp-rjg7-q26m. Ta không dùng upload file nhưng route vẫn đi qua Express. | **Đã sửa**: `overrides` → multer 2.5.0; e2e 38/38 |
| S-02 | High | `deepmerge-ts` < 8 (qua Prisma CLI) — stack exhaustion. Chỉ ở công cụ migrate, không ở runtime API. | **Đã sửa**: `overrides` → 8.0.2; migrate deploy vẫn chạy (e2e, backup test) |
| S-03 | Low ×7 | chuỗi `jest`/`deepmerge` — chỉ devDependency, không vào image | **Chấp nhận** (bản sửa yêu cầu hạ jest xuống 25) |
| S-04 | Medium | API nội bộ game server → backend là HTTP thường | **Giảm thiểu**: chỉ trong mạng nội bộ docker (compose không publish), token dịch vụ ≥ 32 ký tự so sánh thời gian hằng. Còn mở: mTLS khi chạy nhiều máy |
| S-05 | Medium | Rate limit HTTP theo IP lưu trong bộ nhớ một instance | **Mở**: cần store dùng chung (Redis) khi chạy nhiều instance backend |
| S-06 | Medium | Polling thông báo/chat → tải DB tăng theo người online | **Mở** (hiệu năng/DoS): WebSocket/cache khi mở rộng |
| S-07 | Low | Bộ lọc từ cấm rất đơn giản | **Mở**: cần dịch vụ kiểm duyệt trước khi mở công khai |
| S-08 | Low | Test chập chờn "chữ ký bị sửa" (Phase 11) có thể che lỗi thật | **Đã sửa** (lật bit thay vì thay chuỗi) |
| S-09 | Medium | State nhân vật hỏng kiểu dữ liệu làm game server ném exception khi nạp | **Đã sửa** (Phase 14, đọc an toàn + test) |
| S-10 | Info | Lỗi `decode_header` từ chối gói mới (ChatSay) âm thầm | **Đã sửa** + test mọi loại gói |
| S-11 | Medium | Mock thanh toán nếu bật ở production cho phép "trả tiền" giả | **Đã chặn**: backend từ chối khởi động khi `APP_ENV=production` + mock |
| S-12 | Low | Cổng health/metrics của game server | Mặc định chỉ `127.0.0.1`; compose chỉ publish lên localhost; chỉ đọc, không có dữ liệu cá nhân |

Không còn phát hiện mức Critical/High chưa xử lý.

## Quét bí mật
`git log -p` toàn bộ lịch sử được quét các mẫu khóa (chuỗi base64/hex dài, `JWT_SECRET=`, `INTERNAL_SERVICE_TOKEN=`, `PRIVATE KEY`, `ticket_sk`): chỉ thấy giá trị mẫu `CHANGE_ME…` trong `.env.example`. Khóa dev (`config/dev_keys/`), `.env`, `docker/secrets/` đều trong `.gitignore`; script đóng gói từ chối file khóa.

## Còn phải làm trước khi mở công khai (ngoài khả năng của AI trong repo)
Pentest độc lập; review pháp lý (điều khoản, quyền riêng tư, thanh toán, trẻ vị thành niên); WAF/chống DDoS cho UDP; quy trình phản hồi sự cố; xoay vòng khóa ticket/JWT theo lịch.
