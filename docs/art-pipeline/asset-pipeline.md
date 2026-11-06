# Asset pipeline (đã triển khai, Phase 4)

Thiết kế gốc: `asset-pipeline-design.md`. Tài liệu này mô tả phần **đã có code**.

## Thành phần
| Module | Nội dung |
|---|---|
| `src/asset/gltf_loader.cpp` | glTF 2.0/GLB qua **cgltf 1.15** (MIT). Đọc mesh (position, normal, UV0, tangent, color, joints/weights), index, material PBR, ảnh nhúng hoặc file ngoài, node hierarchy (sắp parent trước child), skin thành `Skeleton`, animation (T/R/S; step/linear/cubic). Thiếu normal hoặc tangent thì tự sinh và cảnh báo. File lỗi trả thông báo rõ ràng, không crash. |
| `src/asset/animation.*` | `Skeleton`, `AnimationClip`, `sample()` (loop/clamp), `blend()` (slerp), `model_matrices()`, `skinning_matrices()`, `validate_skeleton()`. |
| `src/asset/manifest.*` | Đọc/ghi `assets/manifest.json`; `validate_entry()` kiểm tra license (whitelist SPDX), source/author, quality, type, file tồn tại, **sha256**, glTF đọc được, skeleton, animation bắt buộc, số tam giác và chiều cao sau scale theo loại. |
| `src/asset/sha256.*` | SHA-256 dùng làm checksum nội dung (**không** dùng cho bảo mật). Đã kiểm tra bằng vector NIST và so với `Get-FileHash` trên Fox.glb. |
| `src/asset/glb_writer.*` | Ghi GLB tối giản (mesh, material, texture PNG nhúng, skin, animation linear/step). Dùng để sinh fixture và placeholder. |
| `src/asset/placeholders.*` | Chó placeholder sinh bằng code: 8 joint, animation idle/walk/run. |
| `src/client/model_upload.*` | Đưa Model lên renderer: ảnh màu dùng sRGB, ảnh dữ liệu dùng linear; mỗi primitive thành một DrawItem. |
| `tools/blender/normalize_export.py` + `scripts/blender_batch.ps1` | Blender 5.2.3 LTS **headless**: nhập glb/gltf/fbx/obj, scale, đổi tên theo tiền tố, xuất GLB +Y up, giữ animation. |

## CLI
```
pawbound asset inspect <file.glb>
pawbound asset validate [--manifest assets/manifest.json] [id ...]     # exit 0 sạch, 1 có lỗi, 2 lỗi I/O
pawbound asset manifest [--manifest ...] --update                        # cập nhật sha256
pawbound asset manifest [--manifest ...] --scan <dir>                    # thêm .glb mới với license UNKNOWN (bắt buộc điền tay)
pawbound asset make-placeholders [--out assets/placeholder]
.\scripts\blender_batch.ps1 -Source in.glb -Dest out.glb [-Scale s] [-Prefix p]
```

## Asset hiện có
| id | quality | license | ghi chú |
|---|---|---|---|
| `char_fox_prototype` | **prototype** | CC0-1.0 AND CC-BY-4.0 | Fox của Khronos glTF-Sample-Assets (commit `8f49239`). Model: PixelMannen (CC0). Rigging/animation: tomkranis (CC-BY 4.0). Chuyển đổi glTF: @AsoboStudio & @scurest (CC-BY 4.0). **Bắt buộc ghi công trong credits của game.** Là cáo chứ không phải chó; chỉ dùng tạm cho prototype. |
| `char_dog_placeholder` | placeholder | PAWBOUND-Generated | Sinh bằng code. |

**Chưa có asset production nào.** Model chó production cần thuê artist hoặc mua (xem R-02).

## Giới hạn
- Chưa hỗ trợ morph target, alpha blend (vẽ như mask), ảnh data-URI trong `.gltf`, KTX2.
- GPU skinning chưa có (Phase 6). Phase 4 render model ở bind pose tĩnh.
