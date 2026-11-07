# Phase 14 — Báo cáo: Gameplay nâng cao (bắt chó, giải cứu, companion, kỹ năng, nhiệm vụ, lãnh thổ)

## A. Kết luận: **PARTIAL**
Các tiêu chí nghiệm thu chính đạt và có test qua mạng thật + Backend thật. PARTIAL vì: nhiệm vụ giải cứu mới là "tới chuồng + giữ tương tác" (chưa có đột nhập/đánh lạc hướng/hộ tống), kẻ trộm đi thẳng (không tìm đường), lãnh thổ chưa hiển thị trong client, client chưa có hình riêng cho kẻ trộm/companion (dùng lại model người/chó NPC).

| Tiêu chí | Kết quả | Bằng chứng |
|---|---|---|
| Bắt chó có cảnh báo và cơ hội phản ứng | Đạt | `capture_smoke.py`: cảnh báo trước, ≥ 3 s, khu an toàn không bị nhắm |
| Đồng đội nhận thông tin và giải cứu | Đạt | sự kiện manh mối cho đồng đội cùng server; thông báo Backend cho party/bang (e2e); giải cứu qua mạng (smoke + tích hợp) |
| Không mất nhân vật/túi đồ/tiền thật | Đạt | chỉ dịch chuyển tới khu giữ; state giữ nguyên; giữ tối đa 10 phút |
| Chống race + reconnect exploit | Đạt | relog khi bị bắt vẫn bị bắt (tích hợp Backend); người cứu rớt mạng → gián đoạn; một người cứu |
| Companion/kỹ năng không phá PvP | Đạt | strength không áp lên người chơi; tốc độ trần +5%; companion không tấn công |
| Lãnh thổ/nhiệm vụ có test concurrent + chống nhân đôi thưởng | Đạt | e2e 4 claim đồng thời → 1 thắng; 6 lần cộng thưởng đồng thời → 1; nhiệm vụ 1 lần/kỳ |

## D. Lệnh và kết quả thật
| Lệnh | Kết quả |
|---|---|
| `pb_tests` | **110/110** (thêm 10 test advanced: máy trạng thái, exploit, config, companion, kỹ năng, nhiệm vụ) |
| `ctest --test-dir build/win-debug` | **17/17** (thêm `net_capture_rescue` 17/17 kiểm tra) |
| `npm run test:e2e` | **38/38** (thêm 3 test lãnh thổ/thưởng/thông báo) |
| `npm test` | 11/11 |
| `npm run test:integration` | **42/42** (thêm: lưu ngay khi bị bắt, relog vẫn bị bắt, cứu sau relog, thưởng vào ví đúng 1 lần, số dư khớp sổ) |
| Build Debug | 0 warning |

## E. Lỗi gặp và cách xử lý
1. **Lỗi thật do test bắt được:** state lưu sai kiểu (`"speed": "x"`) làm `json::value` ném exception → có thể làm sập server khi nạp nhân vật hỏng. Thêm đọc an toàn cho mọi trường Phase 14.
2. Test config `spawn_chance_per_min = 600` bị loader từ chối (ngoài [0,10]) — validation hoạt động; chỉnh test.
3. Smoke test không thấy kẻ trộm: bot đứng ở spawn = **khu an toàn** (đúng luật). Test dịch nạn nhân ra khu không an toàn; giữ kiểm tra "ở khu an toàn không bị nhắm".
4. Bot ghi stdout có đệm khi vào pipe → test chỉ thấy dòng khi bot thoát. Thêm `fflush`.
5. Thông báo "tối đa 0 phút" khi thời hạn < 60 s → hiển thị giây.
6. Kỳ vọng ví = 80 sai vì nhiệm vụ "giải cứu" cộng thêm 50 → kiểm theo sổ giao dịch (mỗi khóa đúng 1 lần, số dư = tổng sổ).
7. Mã lần bắt chuyển từ bộ đếm toàn server sang **tăng theo nạn nhân (được lưu)** để khóa thưởng duy nhất qua nhiều server/khởi động lại.

## F. Rủi ro / còn thiếu
- Kẻ trộm đi thẳng, xuyên vật cản; chưa dùng NavGraph.
- Cân bằng (tốc độ kẻ trộm 4,6 m/s, xác suất 0,25/phút) chưa qua playtest với người thật.
- Giao thức lên v2: client/server cũ không tương thích (bị từ chối "sai phiên bản giao thức" — có chủ đích).
- Lãnh thổ: chưa có hiệu ứng/hiển thị trong client.
