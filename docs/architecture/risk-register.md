# PAWBOUND — Risk Register (v0.1, Phase 0)

Xác suất (P) và ảnh hưởng (I): 1 thấp … 5 rất cao. Điểm = P × I.

| ID | Rủi ro | P | I | Điểm | Giảm thiểu | Phase |
|---|---|---|---|---|---|---|
| R-01 | Tự viết engine khiến tiến độ kéo dài nhiều năm, chưa có sản phẩm để bán | 5 | 5 | **25** | 2.5D (ADR-002); chỉ xây phần game cần; mốc chơi được sớm (Phase 5–7); đo tiến độ từng Phase; cân nhắc bán bản early access khi vòng lặp single-player hoàn chỉnh | 0–7 |
| R-02 | Thiếu asset nghệ thuật chất lượng (model và animation chó) | 5 | 4 | **20** | placeholder sinh bằng code; khung xương chung cho mọi breed; ngân sách thuê artist hoặc mua asset; asset CC0 cho prototype | 4, 6 |
| R-03 | Không đạt 20–30 người/server | 3 | 4 | 12 | 2.5D + AOI lưới; tick 30 Hz; đo sớm từ 2 → 30 người; bot harness; giới hạn AI theo chunk | 8, 10, 15 |
| R-04 | Dependency không build được với MinGW (Jolt, SDL3…) | 2 | 3 | 6 | spike Vulkan đã PASS; pin version; phương án dự phòng va chạm 2D (ADR-006); clang (llvm-mingw) trên ổ D | 1, 2, 6 |
| R-05 | Ổ C đầy làm hỏng build hoặc Docker | 4 | 3 | 12 | mọi công cụ và cache trên D; chuyển Docker disk image sang D trước Phase 10/11; theo dõi dung lượng trước mỗi Phase | 1, 10, 11 |
| R-06 | Lỗi đồng bộ Vulkan (crash, validation error) trên driver cũ | 3 | 3 | 9 | validation layer trong Debug; Vulkan 1.3 core (dynamic rendering, sync2); test trên cả Intel iGPU và GTX 1650; cập nhật driver NVIDIA (đang là 511.x, năm 2022) | 3 |
| R-07 | World generator không deterministic giữa Windows và Linux | 3 | 3 | 9 | PCG32 tự viết, logic dùng số nguyên cm, không dùng `std::*_distribution`; test hash chéo nền tảng | 5, 10 |
| R-08 | Thanh toán sai, gian lận hoặc vi phạm pháp lý | 3 | 5 | **15** | chỉ mock/sandbox; webhook có chữ ký + idempotency + đối soát; legal review trước khi bật thật | 13, 15 |
| R-09 | Cheat (speed, dup item) phá kinh tế | 3 | 4 | 12 | server authoritative; idempotency inventory; test exploit | 7, 10, 14 |
| R-10 | Phạm vi phình to (feature creep) | 4 | 4 | **16** | phân loại MVP/post-MVP/deferred (phase-00 §2); không làm Phase sau khi Phase trước chưa PASS | tất cả |
| R-11 | Một người phát triển + AI agent: kiến thức nằm rải rác, khó bảo trì | 4 | 3 | 12 | ADR, phase report, test tự động, commit nhỏ | tất cả |
| R-12 | Nội dung "trộm chó" gây phản cảm hoặc bị nền tảng hạn chế | 2 | 3 | 6 | trình bày nhẹ nhàng, không bạo lực đồ họa; kiểm tra chính sách store/age rating | 14, 15 |
| R-13 | GPU người chơi yếu (laptop iGPU) | 3 | 3 | 9 | 2.5D nhẹ; preset đồ họa thấp; đo FPS trên Intel UHD | 3, 15 |
| R-14 | Dữ liệu người chơi mất do lỗi save hoặc DB | 2 | 5 | 10 | optimistic version; backup + restore test; checkpoint định kỳ | 11, 15 |
