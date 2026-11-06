# ADR-004 — Networking: ENet + protocol tự xây, Dedicated Server authoritative

- **Trạng thái:** Accepted (version ENet pin ở Phase 10)
- **Ngày:** 2026-11-06

## Quyết định
- Transport: ENet (UDP; kênh reliable và unreliable). Protocol PAWBOUND nằm trên ENet. Header mỗi packet gồm:
  `protocol_version (u16)`, `packet_type (u16)`, `sequence (u32)`, `ack (u32)`, `server_tick (u32)`, `payload_len (u16)`.
- Server tick cố định **30 Hz**. Gửi snapshot **15–20 Hz** tùy khoảng cách (AOI).
- Client chỉ gửi **input command** (hướng di chuyển, chạy, nhảy, tương tác, tấn công) kèm số thứ tự. Server tự tính toàn bộ kết quả.
- Client interpolation với độ trễ khoảng 100 ms. Prediction chỉ áp dụng cho chuyển động của chính người chơi, kèm reconciliation. Chỉ bật sau khi baseline ổn định (Phase 10).
- Xác thực: client trình **session ticket** ngắn hạn do Backend ký (Ed25519 qua libsodium). Server verify bằng public key, không cần gọi DB.
- Mã hóa payload realtime: chưa bật ở MVP (dữ liệu game không nhạy cảm, ticket đã được ký). Nếu cần, dùng libsodium (`crypto_kx` + `crypto_secretstream`/AEAD). Không tự viết crypto.

## Lý do
ENet nhẹ, ổn định, license MIT, hỗ trợ MinGW và Linux. 2.5D cùng mục tiêu 30 người không cần giải pháp nặng hơn.

## Phương án bị loại
- GameNetworkingSockets: mạnh hơn nhưng dependency nặng (protobuf, OpenSSL). Có thể xem lại nếu ENet không đạt benchmark.
- TCP/WebSocket cho realtime: bị head-of-line blocking khi mất gói. Loại.
