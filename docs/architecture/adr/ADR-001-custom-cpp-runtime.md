# ADR-001 — Tự xây runtime C++ thay vì dùng engine thương mại

- **Trạng thái:** Accepted
- **Ngày:** 2026-11-06

## Bối cảnh
MASTER PROMPT cấm dùng Unreal, Unity, Godot và visual scripting. Chủ dự án đã được cảnh báo về chi phí thời gian (2026-11-05) và vẫn chọn hướng này.

## Quyết định
Tự xây runtime bằng C++20, ghép từ các thư viện nền tảng có license cho phép dùng thương mại:
SDL3 (cửa sổ/input), Vulkan (render), GLM (toán), Jolt (va chạm), ENet (transport), miniaudio (âm thanh), cgltf hoặc fastgltf (đọc glTF, chọn ở Phase 4).

## Hệ quả
- Phase 2–5 chiếm phần lớn công sức. Phải giữ phạm vi nhỏ: chỉ xây những gì game cần, không xây engine tổng quát.
- 2.5D (ADR-002) giảm đáng kể độ khó renderer.
- Không có editor. Mọi nội dung đến từ code, config và CLI.
- Rủi ro lịch trình cao, xem R-01 trong risk register.
