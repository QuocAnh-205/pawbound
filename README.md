# PAWBOUND

Game mô phỏng cuộc sống của chó trong thành phố mở, đồ họa **2.5D**, multiplayer (mục tiêu benchmark 20–30 người/server, **chưa đo**).
Engine tự viết bằng C++20 + Vulkan 1.3; thế giới sinh hoàn toàn bằng code theo seed.

## Trạng thái
| Phase | Nội dung | Trạng thái |
|---|---|---|
| 0 | Thiết kế, yêu cầu, spike | PASS |
| 1 | Build system, SDL3, test | PASS (Linux chưa build thật) |
| 2 | Core engine (ECS, event, resource, timestep, job) | PASS |
| 3 | Vulkan renderer 2.5D (PBR, shadow, cutaway, tonemap) | PASS |
| 4 | Asset pipeline glTF, animation, manifest, Blender CLI | PASS |
| 5 | Sinh thế giới procedural + streaming | PASS (hitch khi nạp chunk) |
| 6 | Chó điều khiển được, camera, va chạm, animation | PASS (Fox prototype; va chạm tự viết thay Jolt — ADR-006) |
| 7 | Sinh tồn, tương tác, inventory, chiến đấu, HUD | PASS (HUD chưa có dấu tiếng Việt) |
| 8 | NPC người + chó, waypoint nav, A*, AI theo tính cách | PASS (chưa navmesh) |
| 9 | Đồng hồ, ngày/đêm, thời tiết, lịch NPC, sự kiện động, lưu trạng thái | PASS |
| 10 | Dedicated server authoritative (ENet), ticket Ed25519, bot, load test 2→30 | PARTIAL (Linux chưa build thật; chưa có client prediction) |
| 11–15 | Xem [phạm vi](docs/phases/phase-00-scope-and-acceptance.md) | Chưa làm |

Báo cáo từng phase: `docs/phases/phase-XX-report.md`.

## Yêu cầu
Windows 10/11, GPU Vulkan 1.3. Công cụ (cài trên ổ D, xem [dev-environment](docs/operations/dev-environment.md)): MinGW-w64 GCC 15.2, CMake 4.4.5, Ninja 1.13.2, Vulkan SDK 1.4.363.1, Blender 5.2.3 (tùy chọn).

## Build, test, chạy (PowerShell)
```powershell
cd D:\Choi
.\scripts\build.ps1 -Preset win-debug          # hoặc win-release
.\scripts\test.ps1  -Preset win-debug
.\build\win-debug\bin\pawbound.exe world generate --seed 20261106 --config data\world\demo.json --output out\world_demo
.\build\win-debug\bin\pawbound.exe world validate out\world_demo
.\build\win-debug\bin\pawbound_client.exe --world out\world_demo
```
Điều khiển: WASD đi, Shift nước rút, Ctrl đi bộ, Space nhảy, E tương tác, G lấy đồ, 1–6 dùng đồ, Z nghỉ, J cắn, K né, F (giữ) đánh hơi, Q/R xoay camera, cuộn chuột zoom, T đổi giờ, F3 debug, F12 chụp màn hình, Esc thoát.

## Tài liệu
[Quy tắc dự án](docs/project-rules.md) · [Thiết kế game](docs/architecture/game-design-document.md) · [Kiến trúc](docs/architecture/technical-architecture.md) · [ADR](docs/architecture/adr/) · [Asset & giấy phép](docs/art-pipeline/asset-pipeline.md) · [Dependencies](docs/operations/dependencies.md)

## Ghi công asset
Fox (prototype): model PixelMannen (CC0), rigging/animation tomkranis (CC-BY 4.0), chuyển đổi glTF @AsoboStudio & @scurest (CC-BY 4.0) — KhronosGroup/glTF-Sample-Assets.
