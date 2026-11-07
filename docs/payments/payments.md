# Thanh toán & kinh tế (Phase 13)

> **Trạng thái:** chỉ có **mock provider**. Không có cổng thanh toán thật, không có tiền thật trong dev/test.
> `APP_ENV=production` + `PAYMENT_PROVIDER=mock` → backend **từ chối khởi động**.

## Phí lập bang hội 100.000 VND
Giá nằm ở `data/economy/pricing.json` (`guild_creation.amount = 100000`, `currency = VND`, `order_ttl_s = 1800`). Client **không** gửi số tiền — mọi field lạ trong request bị từ chối (400).

```
Người chơi                Backend                         Provider (mock / thật)
POST /payments/orders ──► tạo PaymentOrder PENDING
  {purpose, idempotencyKey}  amount/currency từ config
                          ──► createCheckout ───────────►  trả providerRef + checkoutUrl
◄── {id, checkoutUrl, 100000 VND, expiresAt}
mở checkoutUrl, trả tiền ─────────────────────────────────► 
                          ◄── POST /payments/webhooks/mock (ký HMAC trên raw body)
                          xác minh chữ ký → ghi PaymentEvent (chỉ thêm)
                          khóa order FOR UPDATE → đối chiếu providerRef, amount, currency
                          PENDING→PAID + tạo Entitlement (1 order = 1 entitlement)
GET /payments/orders/:id ─► PAID, entitlement AVAILABLE
POST /guilds {name} ──────► trong MỘT transaction: tạo bang + entitlement → CONSUMED
```
- Trang quay về (`GET /payments/return?orderId=`) **chỉ đọc** trạng thái; query string kiểu `status=PAID` không có tác dụng.
- Lập bang thất bại (trùng tên…) → transaction rollback → entitlement vẫn `AVAILABLE`, thử lại với tên khác. Đây là cơ chế "retry/reconcile": quyền không mất cho đến khi bang thật sự được tạo.
- Đã có entitlement chưa dùng hoặc đã ở trong bang → không cho tạo order mới (tránh trả tiền thừa).

## Trạng thái order
| Từ \ Sự kiện | PAID | FAILED / CANCELLED | REFUNDED |
|---|---|---|---|
| PENDING | → PAID (APPLIED) | → FAILED/CANCELLED | bỏ qua |
| EXPIRED / FAILED / CANCELLED | → PAID (**APPLIED_LATE** — tiền đã trừ thật thì phải ghi nhận) | bỏ qua | bỏ qua |
| PAID | bỏ qua (IGNORED_ALREADY_PAID) | bỏ qua | → REFUNDED; entitlement chưa dùng → REVOKED; đã lập bang → giữ bang, outcome `APPLIED_REFUND_GUILD_KEPT` để admin xem xét |
| REFUNDED | bỏ qua | bỏ qua | bỏ qua |

Order `PENDING` quá hạn → `EXPIRED` (quét mỗi 60 s, khi đọc order, và `POST /admin/payments/reconcile`).

## Sổ cái & kiểm toán
- `PaymentEvent`: mọi webhook nhận được (kể cả chữ ký sai, sai số tiền) với `outcome`, `payloadSha256`; **trigger DB chặn UPDATE/DELETE**. `(provider, providerEventId)` UNIQUE → webhook lặp (kể cả 10 bản đồng thời) chỉ áp dụng một lần.
- `AuditLog`: tạo order, paid, webhook bị từ chối, yêu cầu hoàn tiền, tiêu entitlement.
- Admin: `GET /admin/payments`, `GET /admin/payments/report?from&to` (tổng theo trạng thái + thống kê outcome webhook), `POST /admin/payments/:id/refund`, `POST /admin/payments/reconcile`.

## Provider
`src/payments/provider.ts` — interface `PaymentProvider { createCheckout, verifyWebhook(rawBody, headers, now) }`. Domain (order, entitlement, guild) không biết provider cụ thể.

**MockPaymentProvider:** header `X-Mock-Signature: t=<unix>,v1=<hex HMAC-SHA256(secret, "t.rawBody")>`, lệch giờ > 300 s bị từ chối, so sánh thời gian hằng. Trang giả lập: `GET /payments/mock/checkout/:ref`, `POST …/complete {outcome: success|fail|cancel}` (chỉ chủ order) — dựng webhook đã ký và đi qua **đúng** đường xử lý webhook.

### Chọn cổng thật — VIỆC CỦA NGƯỜI VẬN HÀNH, chưa làm
Ứng viên phổ biến ở Việt Nam: VNPAY, payOS, MoMo, ZaloPay. Tôi **không** tích hợp API thật và không giả định chi tiết API: tra cứu trong phiên này không lấy được tài liệu chính thức đầy đủ (chỉ có bài viết bên thứ ba, ví dụ VNPAY ký tham số bằng HMAC-SHA512 và IPN là kênh server-to-server cập nhật trạng thái). Trước khi nhận tiền thật cần:
1. Đọc tài liệu **chính thức** + môi trường sandbox của cổng được chọn: cách ký, header, mã phản hồi IPN, chính sách gửi lại, API hoàn tiền/tra cứu.
2. Viết `XxxProvider implements PaymentProvider` + test sandbox; giữ nguyên các kiểm tra amount/currency/providerRef/idempotency.
3. Ký hợp đồng merchant; kiểm tra **pháp lý, thuế, hóa đơn**, chính sách của App Store/Google Play (mua nội dung số trong app di động có thể bắt buộc dùng thanh toán của cửa hàng), quy định bảo vệ người tiêu dùng và hoàn tiền.
4. Không lưu dữ liệu thẻ: chỉ dùng hosted checkout của provider.

## Kinh tế trong game
- Ví "xương" (`Wallet`, sổ `CurrencyTx` chỉ thêm, CHECK số dư ≥ 0). **Chỉ game server** cộng (`POST /internal/wallet/:numericId/earn`, token dịch vụ, trần mỗi lần, idempotent theo `refKey`). Client không có endpoint tăng số dư.
- Tiêu: `POST /shop/cosmetics/:id/buy` — trừ có điều kiện, mua song song không âm.
- **Không pay-to-win:** cosmetic chỉ có trường ngoại hình (loader từ chối file có trường khác); không bán xương bằng tiền thật.
- Chưa làm: game server chưa gọi API cộng xương (chưa có hoạt động thưởng); client chưa có màn hình cửa hàng.
