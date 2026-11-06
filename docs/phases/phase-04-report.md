# Phase 4 — Báo cáo: 3D Asset Pipeline & Animation Data

## A. Kết luận: **PASS**

## B. Đã triển khai
Xem `docs/art-pipeline/asset-pipeline.md`. Dependency mới: cgltf 1.15 (MIT). Công cụ mới trên ổ D: Blender 5.2.3 LTS portable (`D:\DevTools\blender`, SHA256 đã đối chiếu với file `.sha256` chính thức).

## D. Lệnh và kết quả thật
| Lệnh | Kết quả |
|---|---|
| Build Debug | OK, 0 warning |
| `pb_tests` | 38 test case, 591 assertion, tất cả pass |
| `ctest` | 8/8 pass (gồm `asset_manifest_validate`, `asset_inspect_fox`, `renderer_validation_smoke`) |
| `pawbound asset validate` khi thiếu sha256 | exit **1**, báo lỗi đúng |
| `pawbound asset validate` sau `manifest --update` | exit **0** |
| `blender_batch.ps1 -Source Fox.glb -Dest fox_normalized.glb -Scale 0.0075` | headless, exit 0; kết quả cao 0,593 m, 24 joint, 3 animation |
| Client nạp Fox.glb + placeholder_dog.glb vào renderer | 0 lỗi validation; ảnh: `screenshots/phase4_glb.png` |

## G. Tiêu chí
| Tiêu chí | Trạng thái | Bằng chứng |
|---|---|---|
| Load GLB có mesh + material vào renderer | PASS | screenshot, log "đã nạp ... 576 tam giác" |
| Test transform, bounds, index, UV, file lỗi | PASS | `test_asset.cpp`: roundtrip, file rác, file cắt cụt, file không tồn tại |
| Skeleton + ít nhất một animation clip | PASS | Fox thật (24 joint; Survey/Walk/Run) và placeholder (8 joint; idle/walk/run); test sampling/blend/loop |
| CLI validate trả exit code đúng | PASS | 0/1/2 |
| Batch import headless | PASS | Blender `--background` |
| Báo rõ asset thiếu hoặc sai license | PASS | test: license rỗng, UNKNOWN, GPL đều bị từ chối |

## F. Known issues
- Fox là **cáo**, chỉ là asset prototype. Chưa có model chó production.
- GPU skinning chưa làm (dự kiến Phase 6).
- Blender khi xuất lại tạo 72 track/animation (thay vì 21). Hành vi mặc định của exporter (bake mọi kênh), không phải lỗi.
