# ADR-008: Chuyển trình bày sang 3D (góc nhìn thứ ba + thứ nhất)

- Trạng thái: **Chấp nhận** (2026-11-07). Thay thế phần camera của ADR-002 (2.5D).
- Bối cảnh: người dùng yêu cầu "up hẳn đồ họa lên 3D" và thêm góc nhìn thứ nhất, thứ ba.

## Quyết định
- Camera mặc định: **góc thứ ba**, phép chiếu phối cảnh (FOV 60°), xoay tự do bằng chuột/vuốt, khoảng cách 1,8–14 m, pitch −35°…75°, không chui xuống đất.
- **Góc thứ nhất**: mắt con chó (0,55 m), FOV 75°, ẩn model của chính mình. Phím V đổi góc.
- Giữ chế độ 2.5D (`CameraMode::Isometric`, `--camera iso`) cho test và làm nền cho bản đồ sau này.
- Thế giới, mô phỏng, mạng **không đổi**: hướng di chuyển vẫn tính theo yaw camera trên mặt đất, server vẫn authoritative; giao thức giữ phiên bản 2.
- Shader nhận vị trí camera (`camera_back_dir.w = 1`) để tính phản xạ, sương mù theo khoảng cách camera và hướng làm mờ vật chắn.

## Hệ quả
- Tầm nhìn xa hơn → nạp 3 chunk quanh nhân vật, sương mù chân trời che rìa thế giới; vùng bóng đổ 48 m lệch về phía trước.
- Model/asset đơn giản lộ rõ hơn ở góc gần → cần asset 3D tốt hơn trước khi phát hành rộng.
- Chưa có va chạm camera với tường (dùng làm mờ tường thay thế).
