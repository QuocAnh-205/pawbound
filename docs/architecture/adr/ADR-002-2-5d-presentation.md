# ADR-002 — Trình bày đồ họa 2.5D

- **Trạng thái:** Bị thay thế một phần bởi [ADR-008](ADR-008-3d-camera.md) (2026-11-07): camera mặc định giờ là 3D; chế độ 2.5D vẫn giữ làm tùy chọn.
- **Ngày:** 2026-11-06
- **Người quyết định:** Chủ dự án (yêu cầu trực tiếp: "đồ họa 2,5d thôi, đừng 3d hay 2d")

## Bối cảnh

MASTER PROMPT gốc nhắm tới 3D photorealistic với camera góc nhìn thứ ba tự do. Chủ dự án đổi yêu cầu sang 2.5D.
"2.5D" có hai cách làm phổ biến:

1. **Sprite isometric dựng sẵn (pre-rendered sprites):** mỗi vật thể là ảnh 2D vẽ hoặc render sẵn theo 4–8 hướng.
2. **Hình học 3D thật, camera góc cố định:** thế giới và nhân vật là mesh 3D. Camera luôn nhìn chéo từ trên xuống ở góc cố định, gameplay diễn ra chủ yếu trên mặt phẳng.

## Quyết định

Chọn **cách 2: thế giới 3D + camera góc cố định (fixed-angle 3/4 top-down)**.

- Camera: projection **orthographic**, pitch mặc định khoảng 50° (cấu hình được trong khoảng 35°–60°), yaw xoay theo bước 90° bằng phím (Q/E), zoom có giới hạn. Không xoay tự do, không có góc nhìn ngang tầm mắt.
- Camera bám theo chú chó, có làm mượt. Khi tường/mái nhà che nhân vật, renderer làm mờ hoặc cắt mặt che (cutaway) thay vì dời camera.
- Gameplay: di chuyển trên mặt phẳng XZ, cộng độ cao từ terrain cho dốc, bậc thang và nhảy. Logic va chạm và AI chủ yếu chạy trên 2D cộng chiều cao.
- Asset: mesh glTF/GLB (low/mid-poly, stylized-realistic), ánh sáng PBR, đổ bóng, ngày/đêm động.

## Lý do

- Giữ được ánh sáng động ngày/đêm, thời tiết và đổ bóng mà không phải render lại sprite cho mọi hướng × mọi điều kiện sáng.
- Thế giới sinh tự động bằng code (đường, nhà, cây) dễ ghép từ mesh module hơn từ sprite.
- Vẫn dùng đúng stack đã chốt: Vulkan, glTF, Jolt, Blender CLI.
- Camera cố định giảm mạnh độ khó so với 3D tự do:
  - culling theo hình chữ nhật chiếu xuống mặt đất;
  - gần như không cần LOD xa vì không có đường chân trời;
  - shadow map một tầng là đủ;
  - không cần xử lý camera xuyên tường;
  - asset ít polygon hơn, số người chơi nhìn thấy cùng lúc bị giới hạn tự nhiên.
- Server dùng lưới 2D cho interest management, dẫn tới snapshot nhỏ hơn và dễ đạt mục tiêu 20–30 người.

## Hệ quả

- Mục tiêu "photorealistic" được thay bằng **"stylized-realistic chất lượng cao ở góc nhìn 2.5D"**. Không quảng cáo là photorealistic.
- Phase 3: thêm cutaway/dithered fade cho vật che. Không cần sky dome phức tạp, chỉ cần màu trời/ambient theo giờ.
- Phase 6: camera là fixed-angle follow + xoay 90°, không phải orbit camera. Tiêu chí "camera tránh tường" được thay bằng "vật che nhân vật được làm mờ/cắt".
- Jolt vẫn được dùng cho va chạm và character controller, nhưng chuyển động bị ràng buộc gần mặt đất.
- Nếu sau này muốn chuyển sang 3D tự do, renderer và asset vẫn tái sử dụng được. Phải viết lại camera, culling và LOD.

## Phương án bị loại

- **Sprite isometric dựng sẵn:** rẻ cho renderer nhưng tốn công tạo art (8 hướng × mọi animation × mọi breed), khó làm ánh sáng động, khó sinh thế giới tự động. Loại.
- **3D tự do:** chủ dự án không muốn. Loại.
