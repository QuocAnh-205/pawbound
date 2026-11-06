# World Generation (đã triển khai, Phase 5)

Thiết kế gốc: `world-generation-design.md`. Mã nguồn: `src/world/`, client: `src/client/world_view.*`.

## Lệnh
```powershell
pawbound world generate --seed 20261106 --config data/world/demo.json --output out/world_demo
pawbound world validate out/world_demo        # 0 = hợp lệ, 1 = có lỗi, 2 = không đọc được
pawbound world inspect  out/world_demo
pawbound_client --world out/world_demo [--at X Z] [--zoom 6..30] [--time 0..23]
```

## Pipeline (`generator.cpp`, mỗi bước có sub-seed riêng)
1. **District:** 7 tâm Voronoi. Tâm gần giữa nhất là downtown, xa nhất là industrial; luôn có park và residential. Lưới district ô 16 m.
2. **Địa hình:** value-noise fBm **số nguyên (fixed-point Q16)**, ô 4 m, biên độ 2,5 m.
3. **Đường:** vành đai + lưới đường chính (khoảng 160 m, có jitter) + đường nhánh 48–72 m trong mỗi ô (trừ công viên). Công viên có quảng trường, 4 lối đi tới trung điểm 4 cạnh, và hồ ở một góc phần tư (địa hình bị hạ xuống).
4. **Block → lot → nhà:** block được tính trong **chính ô đường chính** chứa nó. Lot theo bề rộng của district, 1–2 hàng; mặt tiền quay ra đường; có setback; chiều cao theo district; asset chọn theo `asset_rules` có trọng số.
5. **Props:** đèn đường cách 24 m (xen kẽ hai bên), cây ven đường cách 12 m, thùng rác, trụ cứu hỏa, thùng rác lớn ở khu công nghiệp, cây/bụi/đá rải trong công viên (dart-throwing có giới hạn số lần thử), ghế quanh quảng trường, rừng thông và đá ở rìa bản đồ.
6. **Nav graph:** tim đường chia đoạn ≤ 20 m. Dựng **trước** POI.
7. **POI:** spawn (bắt buộc trong vùng safe), nước (mép hồ, quảng trường, gần trụ cứu hỏa), thức ăn (thùng rác, ghế, mặt tiền nhà), chỗ trú, cửa hàng. Chỉ nhận POI nếu có đường thẳng tới mạng nav không xuyên nhà hay hồ; lối nối được chia đoạn ≤ 20 m.
8. **Chunk 64 m + ID ổn định:** `(loại << 28) | (chunk << 16) | chỉ số`, sắp xếp chuẩn theo (z, x, loại).

## Tính deterministic
- PCG32 tự viết, toàn bộ tọa độ logic là số nguyên cm, noise số nguyên. Không dùng `sin`/`cos`/`pow` của libm trong logic (đã thay bằng bảng hằng). `sqrt` là IEEE (làm tròn đúng).
- `logical_hash` = SHA-256 của `world.bin` cộng các chunk theo thứ tự chỉ số.
- Golden hash seed `20261106`: `2733441ce1c7f768e5597ecb39a0f97e0ab55001dba7780ba9795c1a4219e607` (test `world: cùng seed/config...`). Đổi thuật toán thì **phải** tăng `kGeneratorVersion` và cập nhật golden hash.
- **Chưa kiểm tra chéo trên Linux**: cần build Linux (Phase 10).

## Validator (`validator.cpp`)
road_connectivity · building_overlap · building_road (lấn đường, vỉa hè, lối đi, hồ) · prop_obstacle · poi_unreachable (không có nav, nối > 30 m, xuyên nhà/hồ) · spawn (ngoài safe, trên nước, sát vật cản, trên lòng đường) · chunk (sai chunk, trùng id, asset không tồn tại) · hash.

## Streaming (`streaming.hpp`, `world_view.cpp`)
Nạp các chunk trong bán kính 2 (Chebyshev), gỡ khi vượt bán kính 3 (hysteresis). Mỗi chunk có 3 mesh gộp: địa hình, tường (texture cửa sổ), và mái/props/POI (màu vertex). Mesh được giải phóng có hoãn (`Renderer::release_mesh`).

## Giới hạn
- Mọi hình học là **placeholder** (`placeholder:*`). Thay asset thật chỉ cần sửa `asset_rules` và `world_view`, không sửa generator.
- Đường chỉ theo lưới vuông góc (hợp với camera xoay 90° của 2.5D); chưa có đường cong.
- Nav graph chỉ là waypoint theo tim đường, **chưa phải navmesh**.
- Nạp chunk đồng bộ trên main thread gây hitch khoảng 100 ms (xem báo cáo).
