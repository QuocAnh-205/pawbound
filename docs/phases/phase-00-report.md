# Phase 0 — Báo cáo

## A. Kết luận: **PASS**

## B. Đã triển khai
- Bộ tài liệu thiết kế: GDD, kiến trúc kỹ thuật, world generation, multiplayer, database, threat model, asset pipeline, risk register, 7 ADR.
- Phân loại toàn bộ yêu cầu gốc thành MVP, post-MVP hoặc deferred, kèm lý do. Định nghĩa MVP.
- Checklist nghiệm thu đo được cho Phase 1–15; ngưỡng benchmark 2/8/16/20/30 người.
- Ghi nhận thay đổi của chủ dự án: **2.5D** (ADR-002), **MinGW + công cụ trên ổ D** (ADR-003).
- Cài công cụ vào `D:\DevTools`: CMake 4.4.5, Ninja 1.13.2, Vulkan SDK 1.4.363.1.
- Technical spike: CMake + Ninja + MinGW GCC 15.2 build và chạy chương trình Vulkan tạo instance, liệt kê 2 GPU.

## C. File thêm
```
docs/project-rules.md
docs/architecture/{game-design-document,technical-architecture,world-generation-design,multiplayer-design,database-design,risk-register}.md
docs/architecture/adr/ADR-001 … ADR-007
docs/security/threat-model.md
docs/art-pipeline/asset-pipeline-design.md
docs/operations/dev-environment.md
docs/phases/phase-00-scope-and-acceptance.md, phase-00-report.md
README.md, CHANGELOG.md, .gitignore
```
Repository git được khởi tạo tại `D:\Choi`.

## D. Lệnh đã chạy và kết quả
| Lệnh | Kết quả |
|---|---|
| `cmake --version` | 4.4.5 |
| `ninja --version` | 1.13.2 |
| `glslc --version` | shaderc v2026.4 |
| `vulkaninfoSDK --summary` | Instance 1.4.363; Intel UHD (1.3.215), GTX 1650 Max-Q (1.3.194) |
| Spike `cmake -G Ninja` + build + run | build OK, chạy in ra loader 1.4.363 và 2 GPU |
| Bộ cài Vulkan SDK lần 1 (không admin) | **Lỗi**: "Cannot elevate access rights while running from command line". Chạy lại với UAC: thành công |

## E. Benchmark/screenshot
Không áp dụng (Phase tài liệu).

## F. Known issues và rủi ro
- Driver NVIDIA cũ (2022); nên cập nhật trước Phase 3.
- Docker Desktop lưu dữ liệu trên C theo mặc định; phải chuyển sang D trước Phase 10/11.
- Chưa xác minh SDL3, Jolt, ENet, miniaudio với MinGW (Phase 1–2).
- R-01 (thời gian tự viết engine) và R-02 (asset chó) là hai rủi ro lớn nhất.

## G. Tiêu chí nghiệm thu
| Tiêu chí | Trạng thái | Bằng chứng |
|---|---|---|
| Phân loại tất cả yêu cầu gốc, có lý do | PASS | phase-00-scope-and-acceptance §2 |
| Sơ đồ kiến trúc và luồng dữ liệu client/server/backend | PASS | technical-architecture §1, §5 |
| Định nghĩa authority, dữ liệu client không được quyết định | PASS | technical-architecture §4 |
| Thiết kế world deterministic theo seed | PASS | world-generation-design, ADR-007 |
| Threat model và risk register | PASS | security/threat-model, risk-register |
| Mỗi Phase sau có mục tiêu, dependency, tiêu chí | PASS | phase-00-scope-and-acceptance §3–4 |
| Không viết code sản phẩm lớn | PASS | chỉ có spike nằm ngoài repo |

## H. Chạy lại spike
Xem `docs/operations/dev-environment.md`.

## I. Việc cần làm trước Phase 1
1. (Khuyến nghị) Cập nhật driver NVIDIA.
2. Gửi prompt Phase 1.

## Quyết định giữ cố định cho các Phase sau
2.5D với camera ortho cố định · C++20 · MinGW GCC 15.2 · Y-up, 1 unit = 1 m, mặt trước +Z · sim 30 Hz · PCG32 + tọa độ logic số nguyên cm · server authoritative · ticket Ed25519 · NestJS + PostgreSQL + Prisma · thanh toán chỉ mock/sandbox.
