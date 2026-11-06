# ADR-006 — Physics: Jolt cho va chạm, chuyển động ràng buộc gần mặt đất

- **Trạng thái:** Superseded một phần (2026-11-07) — Phase 6 dùng **phương án dự phòng: va chạm 2.5D tự viết**
- **Ngày:** 2026-11-06

## Quyết định
- Dùng Jolt Physics (MIT) cho: va chạm tĩnh (terrain, nhà, props), `CharacterVirtual` cho chó, raycast (line-of-sight, kiểm tra tương tác).
- Server và client dùng chung code mô phỏng. Server chạy Jolt headless.
- Vì đồ họa 2.5D (ADR-002), không mô phỏng ragdoll hay vật thể rơi tự do phức tạp. Combat hit validation dùng hình học đơn giản (cung tròn và khoảng cách) trên mặt phẳng.

## Phương án dự phòng
Nếu Jolt không build được với MinGW hoặc quá nặng cho server 30 người: dùng va chạm 2D tự viết (lưới + AABB/capsule trên mặt phẳng + heightfield). Hình học 2.5D cho phép phương án này.

## Cập nhật 2026-11-07 (Phase 6): chọn phương án dự phòng
Nhân vật dùng `src/sim/collision.*` + `src/sim/dog_motor.*` thay vì Jolt. Lý do:
1. **Deterministic giữa client và server** (Windows/MinGW và Linux/GCC): không có solver lặp, không phụ thuộc thứ tự broadphase. Đây là điều kiện để server authoritative và client prediction cho cùng kết quả.
2. Hình học 2.5D chỉ gồm hộp (nhà), hình tròn (cây, đèn, hồ) và heightfield. Va chạm hình tròn vs hộp/hình tròn là đủ.
3. Chi phí server 30 người thấp: lưới không gian 8 m, mỗi tick vài chục phép kiểm tra cho mỗi con chó.
4. Không thêm dependency lớn (Jolt khoảng 200 file nguồn).

Jolt **chưa bị loại**: sẽ tích hợp nếu cần vật thể vật lý động (đồ vật bị đẩy, rơi, xe). Khi đó Jolt chỉ dùng cho vật thể, còn nhân vật giữ motor deterministic.
Đây là **sai khác so với MASTER PROMPT mục 10** và cần chủ dự án biết.
