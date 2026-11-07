# Phase 13 — Báo cáo: Thanh toán (mock), phí lập bang 100.000 VND, kinh tế

## A. Kết luận: **PASS (mock/sandbox)** — chưa có cổng thanh toán thật (đúng quy tắc dự án)
| Tiêu chí | Kết quả |
|---|---|
| Dev mock tạo order 100.000 VND và hoàn tất theo luồng giả lập | Đạt |
| Webhook signature/validation hoạt động trong mock test | Đạt (chữ ký sai/sửa/thiếu/quá hạn → 401) |
| Webhook lặp không tạo entitlement/guild trùng | Đạt (tuần tự + 10 bản đồng thời; 2 lần lập bang song song → 1 bang) |
| Sai số tiền hoặc currency bị từ chối | Đạt (422, order vẫn PENDING, có sổ + audit) |
| Client không thể tự đánh dấu paid | Đạt |
| Không có giao dịch tiền thật trong dev/test | Đạt — chỉ có mock; production cấm mock |

## B. Đã triển khai
- `src/payments/provider.ts` (interface + MockPaymentProvider), `payments.ts` (order, webhook, mock checkout, admin hoàn tiền/báo cáo/reconcile), `wallet.ts` (ví "xương", cửa hàng cosmetic), `economy.config.ts`.
- `data/economy/pricing.json`, `cosmetics.json` (loader chặn trường không phải ngoại hình).
- Migration `20261107120000_payments`: 6 bảng + **trigger append-only** cho `PaymentEvent`, `CurrencyTx`; CHECK `amount > 0`, `balance >= 0`.
- `POST /guilds` cần entitlement (402 kèm số tiền nếu chưa trả); tiêu entitlement trong cùng transaction.
- Env mới: `APP_ENV`, `PAYMENT_PROVIDER`, `MOCK_PAYMENT_WEBHOOK_SECRET` (secret thật chỉ ở `.env`, đã gitignore).
- Bộ lọc lỗi trả thêm trường nguyên thủy có chủ đích (vd `amount`, `currency` của 402).
- Tài liệu: `docs/payments/payments.md`, `docs/security/payment-threat-model.md` (20 mối đe dọa).

## D. Lệnh và kết quả thật
| Lệnh | Kết quả |
|---|---|
| `npm test` | 11/11 PASS (chạy lặp 8 lần ổn định sau khi sửa test chập chờn) |
| `npm run test:e2e` | **35/35 PASS** (13 cũ + 13 xã hội + 9 thanh toán), PostgreSQL thật |
| `npm run test:integration` | 32/32 PASS (không hồi quy) |
| Mutation test | bỏ kiểm tra số tiền → test "sai số tiền" FAIL; đã khôi phục |

## E. Lỗi gặp và cách xử lý
1. Test "chữ ký bị sửa" của Phase 11 **chập chờn** (thay 2 ký tự cuối bằng `AA`, đôi khi trùng chữ ký gốc) — phát hiện khi chạy lại; sửa thành lật một bit.
2. Throttle tạo order (10/phút/IP) làm e2e lỗi khi chạy cả bộ → cấu hình qua env, mặc định giữ 10.
3. Bộ lọc lỗi bỏ mất trường `amount` của 402 → cho phép trường nguyên thủy bổ sung.

## F. Rủi ro / còn thiếu
- **Chưa chọn và chưa tích hợp cổng thật** — người vận hành phải đối chiếu tài liệu chính thức, ký hợp đồng merchant, kiểm tra pháp lý/thuế/hóa đơn và chính sách cửa hàng ứng dụng (thanh toán trong app Android/iOS có thể bắt buộc dùng billing của store).
- Game server chưa cộng "xương" (API đã có); client chưa có màn hình thanh toán/cửa hàng.
- Hoàn tiền khi bang đã lập: chỉ gắn cờ cho admin, chưa có quy trình xử lý bang.
