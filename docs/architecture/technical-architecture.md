# PAWBOUND — Technical Architecture (v0.1, Phase 0)

## 1. Thành phần

```
┌──────────────────────────── Máy người chơi ────────────────────────────┐
│  pawbound_client (C++20, Windows)                                      │
│   ├─ Platform (SDL3: window, input, timer)    ├─ Audio (miniaudio)     │
│   ├─ Renderer (Vulkan, 2.5D ortho camera)      ├─ UI/HUD (đọc data model)│
│   ├─ Net client (ENet + PAWBOUND protocol)     └─ Prediction/Interp.   │
│   └─ pawbound_shared  ◀── dùng chung với server                        │
└───────────────┬───────────────────────────────────────────┬────────────┘
     HTTPS REST │ (login, character, matchmaking, social)   │ UDP/ENet (gameplay)
                ▼                                           ▼
┌──────────────────────────────┐          ┌───────────────────────────────────┐
│ Backend (NestJS, Docker)     │◀────────▶│ pawbound_server (C++20, headless,  │
│  auth · players · characters │ HTTP nội │ Linux/Windows)                    │
│  inventory · saves · sessions│ bộ +     │  ├─ pawbound_shared (sim, ECS,     │
│  matchmaking · social · guild│ service  │  │   survival, combat, AI, world)  │
│  payments (mock/sandbox)     │ token    │  ├─ Jolt (headless)                │
│  admin · health · metrics    │          │  └─ Net server, AOI, anti-cheat    │
└───────┬───────────────┬──────┘          └───────────────────────────────────┘
        ▼               ▼
  PostgreSQL         Redis (presence, rate-limit)       ▲ load world data
                                                          │
                                       pawbound_tools (CLI): world generate/validate,
                                       asset validate/inspect/manifest, Blender CLI
```

## 2. Module C++ (CMake target)

| Target | Loại | Phụ thuộc | Nội dung |
|---|---|---|---|
| `pb_core` | static lib | GLM, logging | lifecycle, clock/fixed timestep, ECS, event, resource handle, config, log, job |
| `pb_world` | static lib | pb_core | PRNG, noise, world generator, world format, chunk streaming, validator |
| `pb_sim` | static lib | pb_core, pb_world, Jolt | character controller, survival, inventory, combat, AI, world clock, events |
| `pb_net` | static lib | pb_core, ENet, libsodium | protocol, serialization, connection, snapshot/delta, ticket verify |
| `pb_render` | static lib | pb_core, Vulkan, SDL3 | Vulkan device, swapchain, pipeline, mesh/material, shadow, cutaway |
| `pb_asset` | static lib | pb_core, glTF loader | glTF import, skeleton/animation, manifest |
| `pb_audio` | static lib | miniaudio | âm thanh |
| `pawbound_client` | exe | tất cả trừ server-only | game client |
| `pawbound_server` | exe | pb_core, pb_world, pb_sim, pb_net | dedicated server headless (không link SDL video/Vulkan) |
| `pawbound` (tools) | exe | pb_world, pb_asset | CLI `pawbound world ...`, `pawbound asset ...` |
| `pb_tests` | exe | Catch2/GoogleTest | unit, deterministic, integration tests |

**Quy tắc phụ thuộc:** `pb_sim` không được include `pb_render`. Server không link renderer. UI chỉ đọc data model.

## 3. Vòng lặp và thời gian
- Simulation fixed timestep **30 Hz** (33,3 ms) trên cả client và server. Render chạy theo vsync, nội suy giữa hai trạng thái sim.
- Accumulator giới hạn tối đa 5 tick mỗi frame để tránh "spiral of death". Delta lớn (alt-tab) bị cắt.
- World clock: 1 ngày game = 40 phút thật (config). Server là nguồn sự thật, client nhận `world_time` trong snapshot.

## 4. Quyền quyết định (authority)
| Dữ liệu | Ai quyết định | Client được gửi |
|---|---|---|
| Vị trí, vận tốc | Server (mô phỏng từ input) | input hướng/nút + sequence |
| HP, đói, khát, thể lực, mệt | Server | — |
| Kết quả cắn, né, sát thương | Server | "bấm cắn, hướng θ" |
| Nhặt, ăn, uống, dùng item | Server (kiểm tra khoảng cách, cooldown, tồn kho) | "tương tác entity #id" |
| Inventory | Server; lưu bền ở Backend | yêu cầu thao tác |
| Spawn tài nguyên, NPC, sự kiện | Server | — |
| Bị bắt / giải cứu | Server | yêu cầu hành động |
| Thời gian, thời tiết | Server | — |
| Tài khoản, nhân vật, bang hội, thanh toán | Backend | request HTTP có auth |
| Camera, UI, âm thanh, hiệu ứng | Client | — |

## 5. Luồng dữ liệu chính
1. **Đăng nhập:** Client → `POST /api/v1/auth/login` → access + refresh token.
2. **Vào game:** Client → `POST /api/v1/matchmaking/join {characterId, region}` → Backend chọn server, cấp **session ticket** (Ed25519, hạn 60 giây, một lần, gắn `server_id`) → Client kết nối ENet tới server, gửi `Hello{protocol_version, ticket, world_hash}`.
3. **Server verify ticket** bằng public key, kiểm tra `server_id`, hạn và nonce chưa dùng. Sau đó gọi Backend `GET /internal/v1/characters/{id}/state` để lấy dữ liệu nhân vật.
4. **Chơi:** Client gửi input 30 Hz. Server gửi snapshot 15–20 Hz trong vùng AOI.
5. **Lưu:** Server gửi `PUT /internal/v1/characters/{id}/state` (kèm idempotency key và version) khi checkpoint mỗi 60 giây, khi nghỉ ở nơi trú ẩn, và khi disconnect.
6. **Thanh toán bang hội:** Client → Backend tạo order → hosted checkout của provider → webhook → Backend verify chữ ký → entitlement. Client **không bao giờ** tự đánh dấu đã thanh toán.

## 6. Quan sát hệ thống
- Log có cấu trúc (JSON lines trên server, text trên client) với category và level.
- Server xuất metrics: tick time p50/p95/p99, số người chơi, entity, packets/s, bytes/s, RAM. Cổng HTTP `/metrics` (Prometheus text) thêm ở Phase 10/15.
- Client overlay F3: FPS, frame time, draw calls, ping, tick.

## 7. Triển khai
- Dev: Windows, MinGW GCC, công cụ ở `D:\DevTools` (ADR-003). Backend, PostgreSQL và Redis chạy bằng `docker compose`.
- Server Linux: build trong container Debian/Ubuntu với GCC, image chạy non-root (Phase 10/15).
