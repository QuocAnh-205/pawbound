# Threat model — Thanh toán (Phase 13)

Tài sản cần bảo vệ: trạng thái PAID của order, entitlement lập bang, sổ cái thanh toán, số dư ví trong game, bí mật webhook.

| # | Mối đe dọa | Biện pháp | Test (backend/test/payments.e2e-spec.ts) |
|---|---|---|---|
| T1 | Webhook giả mạo (kẻ tấn công POST thẳng vào endpoint) | HMAC-SHA256 trên **raw body** + bí mật ≥ 32 ký tự; so sánh thời gian hằng | khóa sai → 401 |
| T2 | Sửa body sau khi ký (đổi số tiền) | chữ ký tính trên byte gốc, không parse lại | body bị sửa → 401 |
| T3 | Replay webhook cũ | timestamp trong chữ ký, lệch > 300 s từ chối; `providerEventId` UNIQUE | t − 3600 s → 401; gửi lại → DUPLICATE |
| T4 | Webhook trùng lặp / đồng thời | UNIQUE (provider, eventId) + khóa order `FOR UPDATE` + entitlement UNIQUE theo order | 10 bản đồng thời → 1 APPLIED, 1 entitlement |
| T5 | Sai số tiền (trả 1.000 thay 100.000) | so với `order.amount` trong DB | → 422 REJECTED_AMOUNT, order vẫn PENDING |
| T6 | Sai tiền tệ | so với `order.currency` | USD → 422 |
| T7 | Webhook của order khác / ref giả / reference độc (SQL) | khớp `providerRef`; reference phải là UUID; truy vấn tham số hóa | → 422, ghi sổ |
| T8 | Client tự đánh dấu PAID | không có endpoint sửa trạng thái; `ValidationPipe` chặn field lạ; trang return chỉ đọc | PATCH/PUT → 404; `?status=PAID` không đổi gì |
| T9 | Client đặt giá | số tiền lấy từ `pricing.json` | gửi `amount` → 400 |
| T10 | Người khác xem/hoàn tất order | lọc theo `userId` → 404 | user khác → 404 |
| T11 | Bấm thanh toán 2 lần | idempotencyKey UNIQUE theo user | cùng key → cùng order |
| T12 | Webhook đến muộn sau hết hạn | ghi nhận PAID (APPLIED_LATE) vì tiền đã bị trừ thật | expired → PAID |
| T13 | Lập 2 bang với 1 lần trả | tiêu entitlement có điều kiện trong transaction tạo bang; mỗi người 1 bang | 2 request song song → 1 bang |
| T14 | Sửa/xóa lịch sử thanh toán | trigger DB append-only trên `PaymentEvent`, `CurrencyTx` | UPDATE/DELETE → lỗi DB |
| T15 | Hoàn tiền rồi vẫn giữ quyền | REFUNDED → entitlement REVOKED; bang đã lập → cờ cho admin | hoàn tiền → 402 khi lập bang |
| T16 | Mock lọt lên production | `loadEnv` từ chối `APP_ENV=production` + mock; endpoint mock kiểm tra lại | (kiểm tra cấu hình) |
| T17 | Lạm phát ví / client tự cộng tiền | chỉ token dịch vụ cộng; trần mỗi lần; refKey idempotent; CHECK ≥ 0 | user gọi earn → 401; vượt trần → 422 |
| T18 | Mua song song làm âm số dư | `updateMany where balance >= price` + CHECK DB | 2 món song song, đủ tiền 1 → 1 thành công |
| T19 | Flood endpoint webhook làm đầy sổ | throttle 120/phút/IP; bản ghi sổ cho chữ ký sai chỉ lưu hash | — (cần WAF/giới hạn ở edge khi lên production) |
| T20 | Timeout giữa chừng / provider không gửi webhook | order hết hạn → EXPIRED; webhook muộn vẫn áp dụng; admin reconcile | expired test |

Còn mở (cần trước khi nhận tiền thật): tích hợp API tra cứu trạng thái của provider thật để đối soát chủ động; cảnh báo khi có `REJECTED_*`; xoay vòng bí mật webhook; giới hạn IP nguồn webhook nếu provider công bố dải IP; quy trình pháp lý/thuế/hoàn tiền.
