# PAWBOUND — Quy tắc dự án (tóm tắt MASTER PROMPT + các thay đổi đã chốt)

Tài liệu này tóm tắt MASTER PROMPT gốc để AI agent/người phát triển đọc lại khi tiếp tục dự án.
Khi có mâu thuẫn, **mục "Thay đổi đã chốt" được ưu tiên** hơn MASTER PROMPT gốc.

## Thay đổi đã chốt so với MASTER PROMPT gốc

| Ngày | Thay đổi | Nguồn | ADR |
|---|---|---|---|
| 2026-11-06 | Đồ họa chuyển từ "3D photorealistic, góc nhìn thứ ba" sang **2.5D** (thế giới 3D, camera góc cố định nhìn chéo từ trên xuống). Không làm 3D tự do, không làm 2D thuần. | Yêu cầu trực tiếp của chủ dự án | ADR-002 |
| 2026-11-06 | Toolchain Windows dùng **MinGW-w64 GCC** thay vì MSVC; mọi công cụ cài vào **ổ D** (`D:\DevTools`) vì ổ C gần đầy. | Yêu cầu trực tiếp của chủ dự án | ADR-003 |

## Sản phẩm

- Open-World Dog Life Simulator + Multiplayer Sandbox, người chơi điều khiển một chú chó.
- Thế giới thành phố sinh bằng code theo seed (trung tâm, dân cư, công viên, công nghiệp/phế liệu, ngoại ô). MVP chỉ cần một khu nhỏ.
- Sinh tồn: HP, đói, khát, thể lực, nghỉ ngơi. Gameplay: khám phá, tương tác, chiến đấu chó-với-chó, NPC, sự kiện, bị bắt/giải cứu.
- Xã hội: bạn bè, đàn, chat, bang hội (phí 100.000 VND, chỉ mock/sandbox cho tới khi có kiểm tra pháp lý), lãnh thổ.
- Multiplayer: mục tiêu benchmark 20–30 người/Dedicated Server. Chỉ được tuyên bố khi đã đo.
- Ngày/đêm, thời tiết do server quản lý.

## Công nghệ

C++20 · CMake + Ninja · SDL3 · Vulkan · GLM · Jolt Physics · ENet + protocol tự xây · libsodium (nếu cần mã hóa, không tự viết crypto) ·
glTF/GLB + GLSL · Blender CLI · miniaudio (xác minh ở Phase 1) · Backend NestJS + TypeScript · PostgreSQL + Prisma · Redis khi cần · Docker · Git.
Không dùng Unreal/Unity/Godot, không Blueprint/visual scripting, không world editor GUI.

## Nguyên tắc làm việc (rút gọn)

1. Kiểm tra repo thực tế trước khi sửa. Không reset/force push/xóa file của người dùng.
2. Chỉ làm Phase đang được giao. Nêu kế hoạch → code thật → build/test thật → cập nhật docs → báo cáo.
3. Không có stub/TODO/mock giả để tuyên bố xong. Không báo build/test pass khi chưa chạy.
4. Pin version mọi dependency, ghi license. Không đoán API; đọc tài liệu/header đúng phiên bản.
5. Không đưa secret vào repo. Không tạo giao dịch tiền thật, không deploy production.
6. Phân biệt rõ placeholder / prototype asset / production asset.
7. Server authoritative; client chỉ gửi input/intent. Client không truy cập DB/Redis trực tiếp.

## Định dạng báo cáo mỗi Phase

A. PASS/PARTIAL/BLOCKED · B. Đã triển khai · C. File thêm/sửa · D. Lệnh build/test và kết quả thật ·
E. Benchmark/screenshot · F. Known issues · G. Bảng tiêu chí PASS/FAIL · H. Cách chạy lại · I. Việc trước Phase kế tiếp.
