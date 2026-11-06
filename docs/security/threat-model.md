# PAWBOUND — Threat Model (v0.1, Phase 0)

Phương pháp: STRIDE theo ranh giới tin cậy. **Client là không đáng tin** trong mọi trường hợp.

## 1. Ranh giới tin cậy
```
[Client] ──(Internet, UDP)──▶ [Game Server] ──(mạng nội bộ, service token)──▶ [Backend] ──▶ [PostgreSQL/Redis]
[Client] ──(Internet, HTTPS)──────────────────────────────────────────────────▶ [Backend]
[Payment Provider] ──(webhook HTTPS, có chữ ký)───────────────────────────────▶ [Backend]
```

## 2. Mối đe dọa và biện pháp
| # | Mối đe dọa | Ví dụ | Biện pháp | Phase |
|---|---|---|---|---|
| T1 | Speed hack | client gửi vị trí xa | client không gửi vị trí; server tính từ input đã clamp; giới hạn số input/giây | 10 |
| T2 | Teleport | sửa bộ nhớ client | như T1; server so vị trí với collision; reconciliation kéo client về | 10 |
| T3 | Damage spoofing | gửi "gây 999 sát thương" | không có packet sát thương từ client; server tính từ hành động cắn + cooldown + khoảng cách | 7, 10 |
| T4 | Nhân bản item | gửi lặp "nhặt", disconnect giữa lúc giao dịch, ghi save trùng | thao tác inventory nguyên tử ở server; tài nguyên có số lượng; `inventory_ops` có idempotency key; save có `state_version` | 7, 10, 11 |
| T5 | Replay packet | phát lại packet cũ hoặc ticket cũ | sequence window; ticket một lần (jti + nonce cache), hạn 60 giây, gắn server_id | 10, 11 |
| T6 | Webhook thanh toán giả | POST trực tiếp vào endpoint webhook | xác minh chữ ký theo tài liệu provider; kiểm tra amount, currency, order ownership; `provider_event_id` unique; đối chiếu bằng API provider khi nghi ngờ | 13 |
| T7 | Redirect giả "thanh toán thành công" | sửa URL return | redirect không bao giờ là bằng chứng; chỉ webhook đã xác minh hoặc truy vấn provider | 13 |
| T8 | Chiếm tài khoản | credential stuffing, đánh cắp refresh token | Argon2id; rate limit đăng nhập theo IP và account; refresh token xoay vòng + phát hiện dùng lại; (post-MVP) 2FA | 11 |
| T9 | DoS server game | flood UDP, packet quá cỡ, nhiều kết nối giả | giới hạn kích thước payload; giới hạn packet/byte mỗi peer; giới hạn kết nối chưa xác thực theo IP; handshake rẻ trước khi cấp tài nguyên; firewall/provider DDoS ở hạ tầng | 10, 15 |
| T10 | DoS backend | spam API | rate limit (Redis), phân trang bắt buộc, giới hạn body | 11 |
| T11 | Leo thang quyền | member tự set role officer, sửa guild khác | mọi kiểm tra quyền ở backend theo permission matrix; không nhận `role` từ client; test âm tính | 12 |
| T12 | Lộ secret | key trong repo hoặc log | `.env` không commit; `.env.example` không có giá trị thật; logger lọc trường nhạy cảm; quét secret trong CI | 1, 11, 15 |
| T13 | Packet malformed/RCE | tràn bộ đệm khi parse | reader có kiểm tra biên; fuzz test parser; build server có sanitizer (ASan/UBSan trên Linux) | 10, 15 |
| T14 | Lỗi suy luận từ client | client sửa world data | `world_hash` kiểm tra trong handshake; server dùng dữ liệu của chính nó | 5, 10 |
| T15 | Lạm dụng chat | spam, quấy rối | rate limit, block/mute, hook kiểm duyệt, giới hạn độ dài | 12 |
| T16 | Exploit bị bắt/giải cứu | relog để thoát, nhận thưởng hai lần | trạng thái bị bắt lưu ở server/backend; thưởng có idempotency theo (event_id, character_id) | 14 |
| T17 | Dependency bị lỗ hổng | thư viện cũ | pin version, theo dõi advisory (`npm audit`, kiểm tra thủ công cho C++) | 15 |

## 3. Không tuyên bố
- Không tuyên bố "chống cheat hoàn toàn". Server authoritative chỉ chặn được cheat thay đổi kết quả. Bot, macro và wallhack (xem entity trong AOI) vẫn có thể tồn tại, chỉ giảm được bằng AOI chặt.
- Kênh UDP chưa mã hóa ở MVP (ADR-004). Có thể bị nghe lén nội dung gameplay, không lộ mật khẩu hay token dài hạn.
