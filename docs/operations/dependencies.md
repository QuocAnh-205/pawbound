# Dependencies (pin version + SHA256)

Khai báo trong `cmake/Dependencies.cmake`. Archive được cache ở `.deps-cache/archives/` (không commit).

| Thư viện | Version | License | Dùng cho | Từ Phase |
|---|---|---|---|---|
| SDL3 | release-3.4.20 | Zlib | cửa sổ, input, timer (client) | 1 |
| GLM | 1.0.3 | MIT (hoặc Happy Bunny) | toán vector/matrix | 1 |
| nlohmann/json | 3.12.0 | MIT | đọc config/manifest/world JSON | 1 |
| doctest | 2.5.3 | MIT | unit test | 1 |
| Vulkan SDK (hệ thống) | 1.4.363.1 | Apache-2.0 / MIT (từng thành phần) | renderer, glslc | 3 |
| Jolt Physics | 5.6.0 (không dùng — ADR-006) | MIT | va chạm | 6 |
| ENet | 1.3.18 (build từ source) | MIT | transport UDP | 10 |
| libsodium | 1.0.22 (gói MinGW chính thức, chữ ký minisign đã xác minh) | ISC | Ed25519 ticket | 10 |
| miniaudio | 0.11.26 (đã tải, chưa dùng) | Public domain / MIT-0 | âm thanh | sau |
| cgltf | 1.15 | MIT | đọc glTF | 4 |

## Backend (npm, pin chính xác trong `backend/package.json` + `package-lock.json`)
| Gói | Version | License |
|---|---|---|
| @nestjs/common, core, platform-express | 11.2.8 | MIT |
| @nestjs/swagger | 11.4.8 | MIT |
| @nestjs/throttler | 6.7.1 | MIT |
| prisma, @prisma/client | 6.19.3 | Apache-2.0 |
| argon2 | 0.45.2 | MIT |
| class-validator | 0.15.1 | MIT |
| typescript | 5.9.3 | Apache-2.0 |
| jest / ts-jest | 29.x | MIT |
| PostgreSQL (portable, D:\DevTools\pgsql) | 18.4 | PostgreSQL License |

SHA256 xem trong `cmake/Dependencies.cmake` (chỉ dependency đang dùng mới được khai báo).

## Quyết định
- **Logging:** tự viết (`src/core/log.*`, khoảng 120 dòng) thay vì spdlog, để giảm dependency và thời gian build. Logger có level, timestamp UTC, category, ghi console và file, thread-safe.
- **Test framework:** doctest (header-only, build nhanh hơn Catch2 v3), chạy qua CTest.
- **Audio:** miniaudio (một file, không phụ thuộc). Tích hợp khi có nhu cầu âm thanh.
