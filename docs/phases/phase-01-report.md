# Phase 1 — Báo cáo: Development Environment & Repository Foundation

## A. Kết luận: **PASS** (Linux build: mới chuẩn bị, chưa chạy thật)

## B. Đã triển khai
- CMake modular: `pb_core`, `pb_platform` (SDL3), `pawbound_client`, `pawbound_server` (headless, không link SDL), `pawbound` (CLI), `pb_tests`.
- `CMakePresets.json`: `win-debug`, `win-release` (MinGW GCC 15.2 + Ninja), `linux-server-debug/release` (chỉ server + tools + tests).
- Dependency pin theo tag + SHA256, cache trên ổ D (`.deps-cache`). SDL3 link tĩnh; runtime MinGW link tĩnh nên `.exe` chạy không cần DLL.
- Logger, config client có validation (thiếu field thì dùng mặc định và cảnh báo; sai kiểu hoặc ngoài khoảng thì báo lỗi, exit code 3).
- Client mở cửa sổ SDL3, xử lý đóng bằng nút X hoặc Esc, thoát sạch. Có `--frames N --hidden` cho test tự động.
- Script PowerShell: `env`, `configure`, `build`, `test`, `clean`, `run-client`. Script Linux: `build-server-linux.sh`.
- `.clang-format`, `.gitignore`.

## D. Lệnh và kết quả thật
| Lệnh | Kết quả |
|---|---|
| `scripts\build.ps1 -Preset win-debug` (build sạch, 279 bước) | OK, 0 warning trong code PAWBOUND |
| `scripts\build.ps1 -Preset win-release` | OK |
| `scripts\test.ps1 -Preset win-debug` | 5/5 passed |
| `scripts\test.ps1 -Preset win-release` | 5/5 passed |

Test gồm: `unit` (logger, config: 6 test case), `cli_version`, `cli_bad_command` (exit code ≠ 0), `server_headless_ticks`, `client_window_smoke` (cửa sổ SDL thật, 30 frame, exit 0).

## F. Known issues
- **Linux build chưa test thật.** Docker Desktop lưu image trên ổ C (còn 5,7 GB). Cần chuyển Disk image location sang D rồi chạy `scripts/build-server-linux.sh` trong container `gcc`.
- clang-format/clang-tidy chưa cài (không có trong Vulkan SDK). Chỉ có file config.
- Không tạo CI vì repo chưa có remote GitHub.
- Lỗi đã gặp và sửa: (1) PowerShell 5.1 đọc `.ps1` không BOM thành ký tự lỗi, đã lưu UTF-8 BOM; (2) `$ErrorActionPreference='Stop'` làm dừng build khi cmake ghi stderr, đã đổi sang kiểm tra `$LASTEXITCODE`.

## G. Tiêu chí
| Tiêu chí | Trạng thái |
|---|---|
| Configure từ build sạch | PASS |
| Build Debug và Release | PASS |
| Mở cửa sổ SDL, đóng sạch | PASS (smoke test, exit 0) |
| CTest có test thật | PASS (5) |
| Không commit binary hoặc secret | PASS (.gitignore) |
| Version pin, license ghi nhận | PASS (`docs/operations/dependencies.md`) |
| Ghi rõ trạng thái Linux | PASS: ghi "chưa test thật" |

## H. Chạy lại (PowerShell)
```powershell
cd D:\Choi
.\scripts\build.ps1 -Preset win-debug
.\scripts\test.ps1 -Preset win-debug
.\scripts\run-client.ps1
```
