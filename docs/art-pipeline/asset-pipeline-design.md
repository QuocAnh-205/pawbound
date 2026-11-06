# PAWBOUND — Asset Pipeline Design (v0.1, Phase 0)

## 1. Nguyên tắc
- Không có editor. Mọi bước nhập, kiểm tra, chuyển đổi và đặt asset đều chạy bằng CLI hoặc script.
- **Code không tự tạo được model chó đẹp.** Model, rig, animation, texture và âm thanh phải được tạo bởi artist hoặc lấy từ nguồn có giấy phép rõ ràng (CC0/CC-BY/mua bản quyền). Pipeline chỉ tự động hóa phần xử lý.
- Ba cấp chất lượng, luôn ghi trong manifest: `placeholder` (sinh bằng code), `prototype` (asset tạm có license), `production`.

## 2. Quy ước
| Mục | Quy ước |
|---|---|
| Định dạng | glTF 2.0 binary (`.glb`). Texture PNG (nguồn) → KTX2/BC7 (runtime, Phase 15 nếu cần) |
| Đơn vị | 1 unit = 1 m |
| Trục | Y lên, mặt trước nhân vật/công trình hướng **+Z** (theo glTF). Blender export với "+Y Up" |
| Pivot | ở đáy, tâm footprint |
| Kích thước tham chiếu | chó cỡ vừa cao khoảng 0,55 m tới vai, dài khoảng 0,9 m; cửa nhà cao 2,1 m |
| Material | PBR metallic-roughness: baseColor, normal, ORM (occlusion/roughness/metallic) |
| Đặt tên | `<category>_<name>_<variant>`, ví dụ `bld_house_small_a`, `prop_bench_wood_a`, `dog_shiba_base` |
| Collision | mesh con tên `UCX_*` (convex) hoặc metadata `collision: box/capsule` trong manifest |
| LOD | 2.5D gần như không cần LOD; tùy chọn `_LOD1` cho cây và nhà khi zoom xa |
| Skeleton chó | khung xương chung `dog_rig_v1` cho mọi breed (scale tỷ lệ). Tên xương cố định: `root, pelvis, spine_01..03, neck, head, jaw, tail_01..04, fl/fr/bl/br_upper/lower/paw` |
| Animation | clip tối thiểu: `idle, walk, trot, run, sprint, jump_start/loop/land, bite, dodge, sniff, eat, drink, lie_down, sleep, hurt, down` |

## 3. Asset manifest (`assets/manifest.json`, mỗi asset một mục)
```json
{
  "id": "bld_house_small_a",
  "path": "assets/buildings/bld_house_small_a.glb",
  "type": "building",
  "quality": "prototype",
  "source": "https://example.invalid/asset-page",
  "author": "Tên tác giả",
  "license": "CC0-1.0",
  "scale": 1.0,
  "sha256": "…",
  "footprint_m": [8.0, 10.0],
  "height_m": 6.5,
  "collision": { "type": "mesh", "node": "UCX_main" },
  "tags": ["residential", "small"],
  "dependencies": [],
  "import": { "generate_tangents": true, "max_texture_size": 2048 }
}
```
`license` bắt buộc. Thiếu, `UNKNOWN`, hoặc không nằm trong danh sách cho phép (`CC0-1.0, CC-BY-4.0, Proprietary-Owned, MIT`) thì validator **fail**.

## 4. CLI (Phase 4)
- `pawbound asset validate [path|--all]`: kiểm tra định dạng, texture thiếu, material lỗi, skeleton hợp lệ (đúng tên xương), số polygon trong giới hạn theo type, scale (bounding box so với khoảng hợp lệ), license, hash. Exit code 0/1/2.
- `pawbound asset inspect <file>`: in ra node, mesh, material, joint, clip, bounds.
- `pawbound asset manifest --scan assets/`: tạo hoặc cập nhật mục manifest (hash, bounds), giữ nguyên trường license do người nhập.
- `scripts/blender_batch.ps1`: gọi `blender --background --python tools/blender/<script>.py -- <args>` để chuẩn hóa scale/trục, áp transform, đổi tên, xuất GLB. Blender đặt ở `D:\DevTools\blender` (portable zip, cài ở Phase 4).

## 5. Placeholder
Sinh bằng code khi chưa có asset: hộp nhà theo footprint, trụ và cầu cho cây, capsule kèm hộp đầu cho chó (`placeholder:dog`), màu theo category. Khung xương placeholder cho chó được sinh bằng code để test animation pipeline, đánh dấu `quality: placeholder`.

## 6. Nguồn asset gợi ý (phải kiểm tra license từng asset trước khi dùng)
Kho CC0 (ví dụ Poly Haven, Kenney, Quaternius) phù hợp cho prototype. Model chó chất lượng cao cho production nhiều khả năng cần **thuê artist hoặc mua**. Đây là chi phí thật cần lên ngân sách.
