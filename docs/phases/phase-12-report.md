# Phase 12 — Báo cáo: Xã hội, bạn bè, chat, party, guild

## A. Kết luận: **PARTIAL**
Phần backend + game server đạt toàn bộ tiêu chí nghiệm thu và có test thật. Chưa PASS toàn phần vì **client chưa có giao diện bạn bè / party / guild / chat xã hội** (client chưa đăng nhập Backend; hiện chỉ có chat gần trong game). Kho đồ bang và lãnh thổ để sau theo đúng phạm vi.

| Tiêu chí | Trạng thái | Bằng chứng |
|---|---|---|
| Kết bạn và quản lý lời mời | Đạt (API) | e2e "bạn bè + chặn" (3 test) |
| Party cho phép nhóm vào cùng server | Đạt | e2e "matchmaking xếp người cùng party…" |
| Guild CRUD + membership kiểm tra quyền ở backend | Đạt | e2e "ma trận quyền qua HTTP", unit 6 test ma trận |
| Không tự nâng quyền bằng request giả | Đạt | field lạ → 400, tự thăng → 422, MEMBER/OFFICER → 403 |
| Không có hai guild cùng tên khi race | Đạt | 8 request song song → 1 × 201, 7 × 409 |
| Chat có rate limit và block/mute | Đạt | e2e chat + `net_local_chat` + test tích hợp backend↔server |

## B. Đã triển khai
- Backend `src/social/`: friends (bạn bè, chặn, presence), party, guild (+ `guild.permissions.ts`), chat, moderation, notifications, `social.config.ts`. Admin: `POST /admin/users/:id/mute`. Matchmaking ưu tiên server có đồng đội.
- Migration `20261107090000_social` (10 bảng mới) + SQL viết tay: index unique một phần cho lời mời PENDING, CHECK `memberLimit`, CHECK độ dài chat. Sinh bằng `prisma migrate diff` (không đụng DB dev).
- Internal API: `GET /internal/users/:numericId/blocks`; `GET /internal/characters/:id/state` trả thêm `chatMutedUntil`, `blockedUserIds`.
- Giao thức: `ChatSay` (10), `ChatMsg` (11), `sanitize_chat` (UTF-8 chặt, bỏ ký tự điều khiển/bidi).
- Game server: chat gần bán kính 30 m, token bucket, lọc từ cấm (cùng file với Backend), cấm chat + chặn hai chiều lấy từ Backend; lệnh console `tp`, `chatstats`.
- Client: ô chat (Enter/Esc, SDL text input), hiển thị tin; HUD bỏ dấu tiếng Việt thay vì in `?`; Esc khi đang gõ không thoát game.
- `utf8_args` (Windows): tham số dòng lệnh tiếng Việt không bị hỏng.
- Tài liệu: `docs/social/social.md`.

## D. Lệnh và kết quả thật
| Lệnh | Kết quả |
|---|---|
| `npm test` (backend unit) | 11/11 PASS (5 ticket + 6 ma trận quyền) |
| `npm run test:e2e` | **26/26 PASS** (13 cũ + 13 xã hội), PostgreSQL thật, DB tạm |
| `npm run test:integration` | **32/32 PASS**, chạy lặp ổn định (thêm: mute ở Backend chặn chat gần; chặn hai chiều có nhóm đối chứng) |
| `ctest --test-dir build/win-debug` | **16/16 PASS** (thêm `net_local_chat` 13/13) |
| `pb_tests` | 100/100 PASS (thêm 3 test chat/text + kiểm tra mọi loại gói qua được header) |
| `net_smoke.py` + `--say` | client thật gửi chat, ảnh chụp hiện "Ban#4: Xin chao ca dan!", validation 0 lỗi |
| Build Debug | 0 warning |

**Mutation test** (chứng minh test race có tác dụng): bỏ `FOR UPDATE` khi khóa guild → test "duyệt đồng thời nhiều đơn" FAIL (6 người vào bang giới hạn 3). Đã khôi phục.

## E. Lỗi gặp và cách xử lý
1. **Chat gần không chạy:** `decode_header` chỉ chấp nhận loại gói ≤ `Bye`, nên `ChatSay` bị coi là gói rác (âm thầm cộng điểm vi phạm). Sửa giới hạn + thêm test "mọi loại gói đã khai báo đều qua header" để lần sau thêm gói không quên.
2. **Tiếng Việt trong tham số dòng lệnh bị hỏng trên Windows** (argv MinGW theo mã ANSI) → thêm `utf8_args` (GetCommandLineW), dùng cho bot và client.
3. Script sinh mã bằng heredoc làm hỏng chuỗi escape (`\n`, `\0`) → sửa từng chỗ, kiểm tra lại byte.
4. Test tích hợp "B không nhận tin của A" ban đầu có thể PASS sai nếu B đã rời game → thêm nhóm đối chứng (B vẫn nhận tin của C; A vẫn nhận tin của C).

## F. Rủi ro / còn thiếu
- Client chưa có UI xã hội; chat gần chưa lọc theo chặn **phía client** (đã lọc phía server — chắc hơn).
- Font HUD chỉ ASCII: tiếng Việt hiển thị không dấu.
- Danh sách chặn ở game server làm mới theo nhịp autosave (mặc định 60 s): chặn giữa phiên có hiệu lực chậm tối đa 60 s ở chat gần (chat Backend có hiệu lực ngay).
- Polling cho thông báo/chat xã hội: tải DB tăng theo số người online — cần WebSocket/gateway hoặc cache khi quy mô lớn.
- Bộ lọc từ cấm rất đơn giản; cần kiểm duyệt thật trước khi mở công khai.
