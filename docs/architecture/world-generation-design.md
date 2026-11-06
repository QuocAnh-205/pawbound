# PAWBOUND — World Generation Design (v0.1, Phase 0)

Thế giới được sinh **hoàn toàn bằng code + config + seed**, không có map editor. Xem ADR-007 về tính deterministic.

## 1. Đơn vị và hệ tọa độ
- 1 đơn vị = 1 mét. Hệ tay phải, **Y hướng lên**, mặt đất là mặt phẳng XZ (khớp với glTF).
- Chunk = 64 m × 64 m. MVP: khu vực 512 m × 512 m = 8 × 8 = 64 chunk.
- Logic world lưu tọa độ dạng số nguyên **centimet** (`int32`) để hash ổn định giữa các compiler.

## 2. Đầu vào: `WorldConfig` (JSON)
```json
{
  "generator_version": 1,
  "seed": 20261106,
  "size_m": [512, 512],
  "chunk_size_m": 64,
  "terrain": { "base_height_m": 0, "amplitude_m": 3.0, "frequency": 0.004, "octaves": 3, "water_level_m": -0.5 },
  "districts": { "downtown": 0.25, "residential": 0.40, "park": 0.20, "industrial": 0.15 },
  "roads": { "arterial_spacing_m": 160, "local_spacing_m": [48, 72], "jitter_m": 6, "sidewalk_m": 2.0 },
  "buildings": { "density": 0.8, "setback_m": 2.0 },
  "vegetation": { "trees_per_ha": { "park": 120, "residential": 25, "downtown": 8, "industrial": 4 } },
  "pois": { "water_per_district": [2, 4], "food_per_district": [3, 6], "shelters": [2, 3] },
  "pvp_zones": { "park": "safe", "residential": "restricted", "downtown": "restricted", "industrial": "contested" },
  "asset_rules": "data/world/asset_rules.json"
}
```
Config có schema và validation (giá trị mặc định, khoảng hợp lệ). Thiếu field thì dùng mặc định và log cảnh báo.

## 3. Pipeline (mỗi bước có sub-seed riêng)
| # | Bước | Đầu ra | Ghi chú |
|---|---|---|---|
| 1 | **District layout** | bản đồ district theo ô 16 m | Voronoi với các tâm có trọng số; ràng buộc downtown ở gần tâm, industrial ở rìa |
| 2 | **Terrain** | heightfield 1 m/ô, mask nước | noise fBm biên độ thấp (2.5D: địa hình gần phẳng, dốc nhẹ); làm phẳng dưới đường và lô đất |
| 3 | **Road graph** | nodes, edges (cấp arterial/local/path) | lưới arterial bị lệch nhẹ (jitter), lưới local chia theo district; công viên dùng path cong. Đảm bảo đồ thị liên thông (BFS); cạnh cắt nhau thì tạo giao lộ |
| 4 | **Blocks và parcels** | polygon block → lot | block = vùng khép kín giữa các đường; chia lot bằng phương pháp OBB-split theo kích thước district |
| 5 | **Buildings** | instance {asset_id, footprint, rot, height_class} | chọn asset theo `asset_rules` (district × kích thước lot); xoay mặt tiền về phía đường gần nhất; kiểm tra footprint nằm trong lot |
| 6 | **Vegetation và props** | instances | Poisson-disk sampling theo mật độ district; loại trừ vùng đường, nhà, POI; đèn đường và thùng rác đặt dọc vỉa hè theo khoảng cách cố định |
| 7 | **POI** | water, food, shelter, shop, spawn | đặt theo số lượng mỗi district; spawn point ở safe zone, cách vật cản ≥ 1,5 m |
| 8 | **Zones** | polygon PvP/safe | lấy từ district + config |
| 9 | **Navigation** | waypoint graph (vỉa hè, path, giao lộ) | MVP: graph dọc vỉa hè và path; navmesh/lưới walkable để sau (Phase 8) |
| 10 | **Collision** | heightfield + box/convex của building/props | dùng cho Jolt |
| 11 | **Chunk export** | `chunks/{x}_{z}.bin` | mỗi instance thuộc chunk chứa tâm của nó; building lớn ghi thêm `spans_chunks` |

## 4. Asset rules và placeholder
- `asset_rules.json` ánh xạ `(district, category, size_class)` sang danh sách `asset_id` có trọng số.
- Asset manifest (Phase 4) cho biết footprint, chiều cao, collision và license.
- **Khi chưa có asset thật:** generator dùng `placeholder:*` (hộp nhà theo footprint và chiều cao, trụ cây, hộp props). Renderer vẽ bằng mesh sinh bằng code, màu theo category, có viền "PLACEHOLDER" trong chế độ debug. Muốn thay asset thì chỉ sửa `asset_rules`, không sửa generator.

## 5. Định dạng file world
```
world_out/
  world.json          # format_version, generator_version, seed, config_hash, size, chunk list, stats
  logical_hash.txt    # SHA-256 trên dữ liệu logic đã chuẩn hóa (int cm, thứ tự sắp xếp ổn định)
  roads.bin  nav.bin  zones.bin
  chunks/0_0.bin ...  # header {magic 'PBCK', version, chunk_x, chunk_z, counts} + mảng instance
  report.json         # thống kê và kết quả validator
```
Định dạng nhị phân little-endian, có magic và version. Loader từ chối khi version không khớp.

## 6. Validator (`pawbound world validate <dir>`)
| Kiểm tra | Lỗi khi |
|---|---|
| Road connectivity | có nút đường không tới được từ nút gốc |
| Building overlap | hai footprint giao nhau (SAT trên OBB) |
| Building ∩ road | footprint giao với dải đường hoặc vỉa hè |
| Prop/tree trong vật cản | tâm hoặc bán kính nằm trong building/road |
| POI unreachable | POI cách nút nav gần nhất > 10 m hoặc không liên thông |
| Spawn invalid | spawn nằm trong vật cản, ngoài safe zone hoặc trên nước |
| Chunk boundary | instance ghi sai chunk, hoặc trùng ID giữa các chunk |
| Hash | `logical_hash.txt` không khớp với dữ liệu |

Exit code: 0 = sạch, 1 = có lỗi, 2 = không đọc được input. Có bộ **fixture lỗi cố ý** để test validator.

## 7. Streaming
- Client load các chunk trong bán kính 2 chunk quanh camera (camera ortho nhìn được khoảng 60 × 40 m, nên 2 chunk là thừa).
- Dùng hysteresis để tránh load/unload liên tục khi đứng ở mép chunk.
- Server load **toàn bộ** khu MVP (512 m² rất nhỏ) nhưng chỉ kích hoạt AI và tài nguyên ở chunk có người chơi trong bán kính 3 chunk.

## 8. Persistence
Thế giới tĩnh được sinh lại từ seed. Trạng thái động (tài nguyên đã dùng, lãnh thổ, sự kiện) lưu dạng delta theo ID instance ổn định: `(chunk_x, chunk_z, local_index, generator_version)`.

## 9. Deterministic test
- `generate(seed, config)` chạy 2 lần cho cùng hash; chạy trên Windows/MinGW và Linux/GCC cũng cho cùng hash (CI ở Phase 10).
- Một golden hash cho seed demo được lưu trong test. Hash thay đổi thì bắt buộc phải tăng `generator_version`.
