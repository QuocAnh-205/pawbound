# Phase 15 — Báo cáo: Tối ưu, bảo mật, QA, triển khai & phát hành (0.1.0-rc1)

## A. Kết luận: **PARTIAL** — Release Candidate thử nghiệm, KHÔNG phải production
Không còn bug blocker/critical đã biết, mọi test tự động pass trên Windows và Linux, có artifact + checksum, triển khai Docker đã chạy thật. PARTIAL vì: benchmark chưa chạy trên máy chủ Linux tham chiếu (chỉ laptop dev), bản Android **chưa chạy trên điện thoại thật**, checklist pháp lý/vận hành cộng đồng chưa có người chịu trách nhiệm, Phase 10/12/14 còn PARTIAL.

| Tiêu chí | Kết quả | Bằng chứng |
|---|---|---|
| Client cài/chạy được trên môi trường công bố (Windows 10/11 x64, Vulkan 1.3) | **Đạt** | giải nén zip vào thư mục mới → kiểm SHA256 → sinh world (đúng hash) → client chạy 200 frame, validation 0 lỗi |
| Dedicated Server Linux headless, có health/metrics | **Đạt** | build Ubuntu 24.04/GCC 13 (WSL trên ổ D): CTest 11/11; `/health`, `/metrics` (Prometheus); Windows bot → server Linux 5/5 |
| Benchmark 20–30 người có báo cáo thực tế | **Đạt trên máy dev**, chưa có máy chủ thật | xem mục C |
| Không còn blocker/critical đã biết | Đạt | `docs/security/security-review.md`; danh sách "đã biết" trong CHANGELOG đều mức major/minor |
| Backup/restore DB đã test | **Đạt** | `npm run test:backup` 9/9 |
| Hướng dẫn deploy/rollback + checksum artifact | **Đạt** | `docs/operations/release-runbook.md`, `dist/0.1.0-rc1/SHA256SUMS.txt` |
| License dependency/asset được ghi nhận | **Đạt** | `THIRD_PARTY_LICENSES.md` (nguyên văn 8 giấy phép + ghi công asset), sinh bằng script |
| Security review có findings + trạng thái | **Đạt** | 12 phát hiện; 2 High đã vá; 0 Critical/High còn mở |
| README/docs đúng thực tế | Đạt | README cập nhật, không quảng cáo quá mức |

## B. Đã làm
- **Linux:** distro WSL riêng `PawboundBuild` import vào `D:\DevTools\wsl` (rootfs Ubuntu 24.04 chính thức, SHA256 đã kiểm) — không động tới distro Ubuntu có sẵn trên ổ C. Build + test server Linux; world sinh trên Linux **cùng hash** với Windows.
- **Docker** (engine chạy trong distro trên ổ D): `docker/server.Dockerfile` (build + chạy toàn bộ test Linux trong image, runtime non-root uid 10001, HEALTHCHECK, SIGTERM lưu dữ liệu), `docker/backend.Dockerfile` (non-root, healthcheck), `docker/compose.yml` (db → migrate → backend → gameserver, giới hạn CPU/RAM, xoay log 10 MB×5, bí mật qua secret file, chỉ mở UDP game). `docker/compose-smoke.sh`: **11/11** (stack healthy, non-root, heartbeat, người chơi vào bằng ticket thật qua UDP, tắt nhẹ nhàng lưu dữ liệu + DRAINING, xoay log).
- **Quan sát:** game server `--health-port` (HTTP chỉ đọc: `/health` 503 khi vòng lặp treo > 10 s, `/metrics` 16 chỉ số); crash report cục bộ không dữ liệu cá nhân (đã kiểm bằng `--crash-test`).
- **Bảo mật:** `npm audit` → vá multer 2.5.0 + deepmerge-ts 8.0.2; production 0 lỗ hổng; quét bí mật toàn lịch sử git sạch.
- **Đóng gói:** `scripts/package_release.py` (zip Windows, tar.gz server Linux, APK Android, `SHA256SUMS.txt`, `release.json` có commit + hash world; từ chối file khóa). `scripts/gen_licenses.py`.
- **Android (theo yêu cầu "chơi trên điện thoại"):** SDK/NDK 28.2 cài ở `D:\DevTools\android-sdk`; `android/` (Gradle, SDLActivity của SDL 3.4.20); engine C++ biên dịch thành `libmain.so` arm64; lần chạy đầu chép asset từ APK + sinh world theo seed; **điều khiển cảm ứng** (joystick ảo, nút cắn/nhảy/chạy/tương tác/đánh hơi/nhặt/nghỉ/né, xoay camera, ô đồ 1–6, chat, chụm 2 ngón để zoom). APK release ký bằng khóa nằm ngoài repo. Yêu cầu Android 13+ và GPU Vulkan 1.3.
- CMake: `PB_BUILD_SERVER`; tách `pb_ticket` (libsodium) khỏi `pb_net` → client không còn phụ thuộc libsodium.

## C. Số liệu thật
**Load test** (`docs/networking/loadtest-results-rc1.md`, Release, laptop i7 12 luồng + GTX 1650, server + bot cùng máy, mỗi mức 45 s):

| Người | Mạng | Tick p95 / p99 (ms) | Ra (Mbit/s) | Kết quả |
|---|---|---|---|---|
| 2 / 8 / 16 / 20 | sạch | 0,16–1,25 / 0,35–2,53 | 0,05–1,16 | PASS |
| 30 | sạch | 2,54 / 7,59 | 2,46 | PASS |
| 20, 30 | mất 2–5 %, trễ 50 ms | ≤ 0,78 / ≤ 0,95 | 1,17–2,50 | PASS |

**Lưu ý trung thực:** lần chạy đầu (giữ ở `loadtest-results-rc1-contended.md`) mức 30 người mạng sạch **FAIL** (p95 22 ms) vì chạy cùng lúc build Docker 12 luồng trên cùng máy; chạy lại khi máy rảnh thì PASS. So với Phase 10 (p95 0,76 ms ở 30 người), tick tăng do thêm hệ thống Phase 12–14 nhưng vẫn xa ngưỡng. Chưa đo trên máy chủ Linux 4 vCPU tham chiếu và chưa qua Internet thật.

**Soak 20 phút** (`docs/operations/soak-results.md`): 10 người liên tục + 9 đợt × 10 người vào/ra → 100/100 vào được, 0 rớt, RAM 7,4 → 7,3 MB (đỉnh 7,7), tick p99 tối đa 3,96 ms, server thoát sạch.

## D. Lệnh và kết quả
| Lệnh | Kết quả |
|---|---|
| `ctest` win-debug (gồm GPU) | 17/17 |
| `ctest` win-release (không GPU) | 12/12 |
| `ctest --preset linux-server-release` (WSL) | 11/11 |
| `pb_tests` | 110/110 |
| `npm test` / `test:e2e` / `test:integration` / `test:backup` | 11/11 · 38/38 · 42/42 · 9/9 |
| `docker/compose-smoke.sh` | 11/11 |
| `scripts/soak.py --minutes 20` | 5/5 |
| `gradlew assembleRelease` | APK 5,3 MB, chữ ký hợp lệ (`apksigner verify`) |

## E. Lỗi tìm thấy trong phase này
1. Image server: thiếu thư mục `config/` → test bindings fail **trong Docker build** (test chặn ra image lỗi — đúng mục đích). Đã sửa.
2. Image backend: Prisma sinh engine cho OpenSSL 1.1 nhưng runtime OpenSSL 3 → backend không khởi động. Phát hiện nhờ compose smoke; sửa `binaryTargets` + openssl ở stage build.
3. Smoke compose kiểm "đã lưu" quá sớm (thiết kế: lưu khi hết giữ chỗ/tắt/autosave) → chuyển kiểm tra sau bước tắt.
4. Trùng tên `on_signal` (crash handler vs server) → lỗi link; đổi tên.
5. Android: `local.properties` mất dấu `\`, JDK 25 không chạy được Gradle 8.12 (dùng JDK 17), Vulkan 1.3 cần API 33 → `minSdk 33`.
6. Công cụ `android` CLI mới tự tải ~86 MB vào ổ C → đã xóa đúng thư mục nó tạo, dùng `sdkmanager` với SDK trên D.

## F. BLOCKED / còn thiếu
- **Android chưa chạy trên điện thoại thật** (máy này không có thiết bị; emulator không đảm bảo Vulkan 1.3). Cần một máy Android 13+ có Vulkan 1.3 để thử; rủi ro: pre-rotation swapchain, hiệu năng GPU di động, bàn phím ảo khi chat.
- **iOS: không thể** — cần máy Mac + Xcode + tài khoản Apple Developer.
- Chưa có máy chủ công khai; chưa có cổng thanh toán thật; chưa có review pháp lý (`release-runbook.md` §7).
- Docker Desktop (ổ C) không dùng; image build bằng Docker Engine trong distro WSL trên D.
