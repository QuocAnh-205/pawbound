# Phase 2 — Báo cáo: Core Game Engine Architecture

## A. Kết luận: **PASS**

## B. Đã triển khai
Xem `docs/architecture/core-engine.md`. Server dùng `Application` + ECS (4 entity mẫu với Position/Velocity sinh bằng code, sẽ được thay bằng component gameplay thật).

## D. Lệnh và kết quả
| Lệnh | Kết quả |
|---|---|
| `scripts\build.ps1 -Preset win-debug` | OK, 0 warning trong code PAWBOUND |
| `build\win-debug\bin\pb_tests.exe` | 24 test case, 359 assertion, tất cả pass |
| `ctest` | 5/5 pass |
| `pawbound_server --ticks 60` | chạy 60 tick trong khoảng 2,03 giây (30 Hz), thoát exit 0 |

## G. Tiêu chí
| Tiêu chí | Trạng thái | Bằng chứng |
|---|---|---|
| Lifecycle chạy lặp nhiều lần | PASS | test "vòng đời chạy lặp nhiều lần": init 3 lần, shutdown 3 lần |
| Fixed timestep ổn định, test delta lớn | PASS | test delta 5 giây cho đúng 5 tick, alpha ∈ [0,1) |
| ECS không trả entity đã destroy | PASS | test generation và tái sử dụng index |
| Event unsubscribe an toàn | PASS | test hủy trong dispatch, channel bị hủy trước subscription |
| Resource handle invalid được phát hiện | PASS | test release + tái sử dụng slot |
| Unit tests pass | PASS | |
| Log FPS, frame time, tick | PASS | `Application::on_stats` |
| Không có renderer đầy đủ | PASS | |

## F. Known issues
- Chưa có công cụ phát hiện leak trên MinGW (ASan không hỗ trợ MinGW). Sẽ chạy ASan/LSan trên Linux build ở Phase 10/15. Lifecycle hiện chỉ được kiểm tra bằng bộ đếm init/shutdown và RAII.
