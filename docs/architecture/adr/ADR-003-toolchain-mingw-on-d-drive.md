# ADR-003 — Toolchain Windows: MinGW-w64 GCC, công cụ cài trên ổ D

- **Trạng thái:** Accepted (cần xác minh build thực tế ở Phase 1)
- **Ngày:** 2026-11-06

## Bối cảnh

Ổ C của máy phát triển chỉ còn khoảng 5,8 GB trống. Visual Studio Build Tools (MSVC) luôn ghi các thành phần dùng chung (Windows SDK, package cache, installer) vào ổ C, thường vài GB, dù chọn đường dẫn cài trên ổ khác. Máy đã có sẵn MinGW-w64 GCC 15.2.0 (`C:\mingw64`, posix threads, SEH).

## Quyết định

- Compiler Windows: **MinGW-w64 GCC 15.2.0** (đã có, không cài thêm vào C). Chuẩn **C++20**.
- Công cụ cài vào `D:\DevTools`:
  - CMake 4.4.5 → `D:\DevTools\cmake`
  - Ninja 1.13.2 → `D:\DevTools\ninja`
  - Vulkan SDK 1.4.363.1 → `D:\DevTools\VulkanSDK\1.4.363.1` (gồm loader, validation layers, `glslc`)
- Script `scripts/env.ps1` (Phase 1) thêm các đường dẫn này vào `PATH` của phiên PowerShell, không sửa PATH hệ thống.
- Cache dependency (CMake FetchContent) đặt trong `D:\Choi\.deps-cache` hoặc thư mục build trên ổ D.
- Linux Dedicated Server build bằng GCC trong Docker container (Phase 10). Rủi ro: dữ liệu Docker Desktop/WSL mặc định nằm trên C. Phải chuyển "Disk image location" sang ổ D trước khi build image.

## Hệ quả và rủi ro

- SDL3, GLM, Jolt, ENet, miniaudio đều hỗ trợ MinGW, nhưng phải xác minh bằng build thật ở Phase 1–2.
- Vulkan: link với `vulkan-1` từ SDK. MinGW link được thư viện import này; nếu lỗi thì load loader động qua `volk`. Quyết định ở Phase 3.
- Không dùng được công cụ chỉ có cho MSVC như PIX hay debugger của Visual Studio. Debug bằng gdb hoặc RenderDoc.
- Nếu sau này có đủ dung lượng ổ C, có thể thêm preset MSVC/clang-cl; code phải giữ portable (không dùng extension riêng của GCC).
