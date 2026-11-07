# Xã hội: bạn bè, chặn, party, guild, chat, thông báo (Phase 12)

**Nguồn sự thật:** Backend (PostgreSQL). Client chỉ hiển thị; game server chỉ nhận dữ liệu cần cho mô phỏng (cấm chat, danh sách chặn).
**Cấu hình:** `data/social/social.json` — MỘT nơi cho mọi giới hạn (bạn bè 100, party 4, guild 20, độ dài chat 200, 5 tin / 10 s, TTL lời mời, từ cấm). Backend và game server đọc cùng file.

## Kênh truyền
| Loại | Đi qua | Lưu? | Thời gian thực |
|---|---|---|---|
| Chat **gần** (local, bán kính 30 m) | Game server (UDP/ENet: `ChatSay` → `ChatMsg`) | **Không** lưu, không log nội dung | Có (cùng kết nối game) |
| Chat **global / party / guild** | Backend HTTP `POST/GET /api/v1/chat/:channel` | Lưu bảng `ChatMessage` | Polling `?after=<id>` (5–15 s) |
| Lời mời, thông báo | Backend `GET /api/v1/notifications` | Lưu, hết hạn theo TTL | Polling |

Quyết định: giai đoạn này dùng **polling** thay vì WebSocket — đơn giản, chạy được sau mọi proxy, đủ cho chat xã hội không cần độ trễ thấp. Khi cần đẩy realtime: thêm gateway WebSocket phát sự kiện từ cùng các service (không đổi API ghi).

## Bạn bè & chặn
- Một hàng `Friendship` cho mỗi **cặp** (`pairKey` = hai id sắp xếp, UNIQUE): A gửi B và B gửi A đồng thời → DB chặn bản thứ hai, logic chạy lại và **tự chấp nhận**.
- Gửi lại cùng lời mời → trả lời mời cũ (idempotent). Lời mời hết hạn sau 14 ngày.
- Bạn bè hiển thị online/offline + server đang chơi (từ phiên game chưa đóng).
- Chặn: hủy kết bạn và lời mời party đang chờ; người bị chặn gửi lời mời nhận 404 (không lộ việc bị chặn); tin chat của hai bên bị ẩn với nhau ở **cả** chat Backend lẫn chat gần (game server lọc theo danh sách chặn lấy từ Backend lúc vào game và làm mới mỗi lần autosave).

## Party / đàn
- Tạm thời, tối đa 4 (config). Trưởng nhóm mời / kick / chuyển quyền; trưởng nhóm rời → người vào sớm nhất lên thay; người cuối rời → giải tán.
- Mỗi người một party (khóa chính `PartyMember.userId`). Lời mời PENDING duy nhất cho mỗi (party, người) bằng **index unique một phần** (SQL viết tay trong migration). Mọi thao tác khóa hàng `Party` (`FOR UPDATE`) rồi đọc lại quyền.
- **Vào game cùng nhau:** `matchmaking/join` ưu tiên server mà đồng đội đang chơi (nếu server còn sống và còn chỗ).

## Guild — ma trận quyền
Định nghĩa duy nhất: `backend/src/social/guild.permissions.ts` (unit test riêng). Server luôn kiểm tra; `GET /guilds/mine` trả `permissions` chỉ để client ẩn/hiện nút.

| Hành động | OWNER | OFFICER | MEMBER |
|---|---|---|---|
| Xem đơn/lời mời, mời, duyệt đơn xin vào | ✔ | ✔ | ✘ |
| Sửa mô tả, emblem | ✔ | ✔ | ✘ |
| Đổi tên | ✔ | ✘ | ✘ |
| Kick | người cấp thấp hơn | chỉ MEMBER | ✘ |
| Thăng MEMBER→OFFICER / giáng OFFICER→MEMBER | ✔ | ✘ | ✘ |
| Chuyển quyền chủ bang, giải tán | ✔ | ✘ | ✘ |
| Rời bang | ✘ (phải chuyển quyền/giải tán) | ✔ | ✔ |

Quy tắc chống leo thang quyền: không có endpoint nhận "role" từ client (body có field lạ → 400); thao tác nhắm vào chính mình → 422; mọi thao tác chạy trong transaction có khóa hàng `Guild` và đọc vai trò **bên trong** transaction.

Ràng buộc nhất quán:
- Tên bang: chuẩn hóa NFC + gộp khoảng trắng; `nameKey` (chữ thường) UNIQUE → nhiều request tạo cùng tên đồng thời chỉ một thành công (đã test 8 request song song). Giải tán đặt `nameKey = NULL` để tên dùng lại được.
- Mỗi người một bang (khóa chính `GuildMember.userId`). Giới hạn thành viên kiểm tra sau khi khóa hàng bang (đã test duyệt 6 đơn song song khi còn 2 chỗ → đúng 2 người vào; **mutation test**: bỏ `FOR UPDATE` thì test này FAIL với 6 người vào).
- Lời mời/đơn: PENDING duy nhất (index unique một phần), hết hạn 7 ngày, dùng lại → 404/409, chấp nhận vào bang tự hủy các yêu cầu đang chờ khác của người đó.
- Tạo bang dùng `GuildService.createWithin(tx, …)` để Phase 13 (phí 100.000 VND) tiêu entitlement **trong cùng transaction**.

Chưa làm (có chủ đích): kho đồ/quyên góp của bang (inventory nằm trong trạng thái game server, cần giao dịch hai pha để không nhân bản đồ), lãnh thổ (Phase 14).

## Chat
- Làm sạch: NFC, thay ký tự điều khiển/định dạng vô hình (U+200B…, U+202A–U+202E, U+2066–U+2069, BOM) bằng khoảng trắng, gộp khoảng trắng; độ dài 1–200 ký tự (Backend) / ≤ 240 byte UTF-8 (chat gần, gói mạng). UTF-8 sai → từ chối.
- Rate limit theo **người dùng**: 5 tin / 10 s, đếm trong DB dưới `pg_advisory_xact_lock` theo user (gửi song song cũng không lọt). Lớp throttle theo IP phía trước. Chat gần: token bucket 3 tin liền, sau đó 1 tin / 2 s; vượt → cảnh báo + điểm vi phạm.
- Cấm chat: admin `POST /admin/users/:id/mute {minutes}` (audit). Game server nhận `chatMutedUntil` khi nạp nhân vật và làm mới định kỳ.
- Kiểm duyệt: `ModerationService` (che từ cấm theo ranh giới từ Unicode, không bắt nhầm chuỗi con) — điểm móc để thay bằng dịch vụ ngoài. Báo cáo tin: `POST /chat/messages/:id/report` → AuditLog (chỉ admin đọc).
- Kênh party/guild suy ra từ membership hiện tại phía server — client không chỉ định được kênh của nhóm khác; báo cáo tin ở kênh mình không thuộc → 404.
- Quyền riêng tư: log HTTP không ghi body; game server không ghi nội dung chat gần.

## Client
- Online: **Enter** mở ô chat, gõ (hỗ trợ IME/bàn phím ảo qua SDL text input), **Enter** gửi, **Esc** hủy (không thoát game khi đang gõ); trong lúc gõ chó đứng yên.
- HUD dùng font bitmap ASCII: tiếng Việt hiển thị **bỏ dấu** (`core/text.cpp: ascii_fold`). Font Unicode đầy đủ: việc của Phase 15 (UI).
- Chưa có màn hình bạn bè/bang trong client (client chưa có đăng nhập Backend); API đã sẵn (Swagger `/api/docs`).
