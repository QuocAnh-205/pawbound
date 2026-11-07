# Changelog

Định dạng theo [Keep a Changelog](https://keepachangelog.com/vi/1.1.0/). Phiên bản theo SemVer; giao thức mạng có số riêng (`kProtocolVersion`).

## [0.1.0-rc1] — 2026-11-07 — Release candidate (alpha thử nghiệm)
Trạng thái chi tiết: `docs/phases/phase-15-report.md`. **Không** phải bản production: chưa có máy chủ công khai, chưa có thanh toán thật, chưa có bản điện thoại.

### Thêm
- Engine C++20/Vulkan 1.3 tự viết, đồ họa **2.5D** (camera cố định góc nghiêng, xoay 90°), PBR, bóng, cutaway, tonemap ACES; SDL3.
- Thế giới mở sinh thủ tục, tất định theo seed (cùng hash trên Windows và Linux), streaming chunk.
- Chó điều khiển được (di chuyển, nước rút, nhảy, đánh hơi), sinh tồn (đói, khát, thể lực, nghỉ), túi đồ, chiến đấu, NPC người/chó có tính cách, ngày/đêm, thời tiết, sự kiện động.
- Multiplayer: dedicated server authoritative (ENet/UDP), ticket Ed25519, interest management, 20 Hz snapshot.
- Backend NestJS + PostgreSQL: tài khoản, nhân vật, matchmaking, lưu trạng thái, bạn bè, chặn, party, bang hội (ma trận quyền), chat, thông báo, admin, audit.
- Thanh toán **mock** phí lập bang 100.000 VND (webhook ký, idempotent, sổ cái bất biến), ví "xương" và cosmetic (không pay-to-win).
- Gameplay nâng cao: kẻ trộm chó (cảnh báo → bắt → giải cứu), companion, kỹ năng có trần, nhiệm vụ hằng ngày, lãnh thổ bang (MVP).
- Vận hành: `/health` + `/metrics` (Prometheus) cho game server, crash report cục bộ, image Docker non-root có healthcheck, compose mẫu, sao lưu/khôi phục DB đã test, runbook.

### Bảo mật
- Vá `multer` 2.5.0 và `deepmerge-ts` 8.0.2 qua `overrides`; dependency production: 0 lỗ hổng (`npm audit --omit=dev`).
- Xem `docs/security/security-review.md` (12 phát hiện, trạng thái từng mục).

### Đã biết (chưa sửa)
- Chưa có client prediction (điều khiển online trễ ≈ RTT).
- HUD dùng font ASCII: tiếng Việt hiển thị không dấu.
- Client chưa có màn hình đăng nhập/bạn bè/bang/cửa hàng (API đã có).
- Kẻ trộm đi thẳng (chưa tìm đường); giải cứu mới là tương tác tại chuồng.
- Model chó là Fox prototype (CC-BY) + placeholder.

## [0.0.x] — lịch sử phát triển trước RC (Phase 0)
### Added
- Phase 0: tài liệu thiết kế, ADR-001..007, threat model, risk register, tiêu chí nghiệm thu Phase 1–15.
- Chuyển hướng đồ họa sang 2.5D (ADR-002); toolchain MinGW + công cụ trên ổ D (ADR-003).
