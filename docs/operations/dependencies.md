# Dependencies (pin version + SHA256)

Khai báo trong `cmake/Dependencies.cmake`. Archive được cache ở `.deps-cache/archives/` (không commit).

| Thư viện | Version | License | Dùng cho | Từ Phase |
|---|---|---|---|---|
| SDL3 | release-3.4.20 | Zlib | cửa sổ, input, timer (client) | 1 |
| GLM | 1.0.3 | MIT (hoặc Happy Bunny) | toán vector/matrix | 1 |
| nlohmann/json | 3.12.0 | MIT | đọc config/manifest/world JSON | 1 |
| doctest | 2.5.3 | MIT | unit test | 1 |
| Vulkan SDK (hệ thống) | 1.4.363.1 | Apache-2.0 / MIT (từng thành phần) | renderer, glslc | 3 |
| Jolt Physics | 5.6.0 (đã tải, chưa dùng) | MIT | va chạm | 6 |
| ENet | 1.3.18 (đã tải, chưa dùng) | MIT | transport UDP | 10 |
| miniaudio | 0.11.26 (đã tải, chưa dùng) | Public domain / MIT-0 | âm thanh | sau |
| cgltf | 1.15 (đã tải, chưa dùng) | MIT | đọc glTF | 4 |

SHA256 xem trong `cmake/Dependencies.cmake` (chỉ dependency đang dùng mới được khai báo).

## Quyết định
- **Logging:** tự viết (`src/core/log.*`, khoảng 120 dòng) thay vì spdlog, để giảm dependency và thời gian build. Logger có level, timestamp UTC, category, ghi console và file, thread-safe.
- **Test framework:** doctest (header-only, build nhanh hơn Catch2 v3), chạy qua CTest.
- **Audio:** miniaudio (một file, không phụ thuộc). Tích hợp khi có nhu cầu âm thanh.
