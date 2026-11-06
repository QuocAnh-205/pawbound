# Phase 0 — Phạm vi, MVP và tiêu chí nghiệm thu các Phase

## 1. Định nghĩa MVP
**MVP = bản single-player chơi được, kèm multiplayer tối thiểu.**
1. Một khu 512 m × 512 m sinh từ seed: trung tâm, dân cư, công viên, góc công nghiệp.
2. Một chú chó điều khiển được, camera 2.5D.
3. Vòng lặp sinh tồn: đói, khát, thể lực, HP, nghỉ; tìm thức ăn và nước; chiến đấu với chó NPC.
4. NPC người và chó cơ bản, ngày/đêm, thời tiết đơn giản.
5. Dedicated Server cho tối thiểu 8 người, đăng nhập qua Backend, lưu nhân vật.

Bạn bè/chat/bang hội, thanh toán, bị bắt/giải cứu và lãnh thổ là **post-MVP**.

## 2. Phân loại yêu cầu gốc
| Yêu cầu | Phân loại | Lý do |
|---|---|---|
| Điều khiển chó, góc nhìn 2.5D | MVP | Cốt lõi của fantasy |
| Thế giới sinh bằng code, theo seed | MVP | Yêu cầu cứng: không dựng map tay |
| Khu trung tâm/dân cư/công viên/công nghiệp | MVP (khu nhỏ) | Cần đủ đa dạng để khám phá |
| Vùng ngoại ô, thành phố lớn nhiều km | Deferred | Hoàn thiện một khu trước |
| HP/đói/khát/thể lực/nghỉ | MVP | Vòng lặp chính |
| Inventory | MVP (tối giản: 6 ô) | Cần cho đồ ăn mang theo |
| Chiến đấu chó-với-chó (NPC) | MVP | Tạo thử thách |
| PvP theo vùng | Post-MVP (Phase 10 sau khi multiplayer ổn) | Cần server authoritative trước |
| NPC người, chó | MVP (cơ bản) | Thế giới sống |
| Ngày/đêm, thời tiết | MVP (clear/rain/fog) | Server đồng bộ; bão để post-MVP |
| Sự kiện động | Post-MVP (1–2 sự kiện ở MVP) | |
| Multiplayer 20–30 người | Post-MVP (MVP đạt 8 người đã đo) | Phải đo tăng dần |
| Đăng nhập, lưu nhân vật | MVP | Cần cho multiplayer bền vững |
| Bạn bè, party, chat | Post-MVP | |
| Bang hội | Post-MVP | Phụ thuộc social + thanh toán |
| Phí bang hội 100.000 VND | Post-MVP, chỉ mock/sandbox | Cần pháp lý và cổng thanh toán |
| Lãnh thổ | Deferred | Sau load test và guild |
| Bị bắt/giải cứu | Post-MVP | Cần AI + multiplayer ổn định |
| Companion, progression | Post-MVP | |
| Đồ họa photorealistic | **Thay đổi:** stylized-realistic 2.5D (ADR-002) | Chủ dự án chọn 2.5D |
| Cosmetic bán tiền thật | Deferred | Sau khi có người chơi |

## 3. Dependency giữa các Phase
```
0 → 1 → 2 → 3 → 4 → 5 → 6 → 7 → 8 → 9 → 10 → 11 → 12 → 13 → 14 → 15
                    └──────────(5 dùng asset rules của 4)
                                           10 ⇄ 11 (session ticket: 10 dùng ticket giả lập ký bằng key dev cho tới khi 11 xong)
```

## 4. Mục tiêu, đầu ra và tiêu chí nghiệm thu từng Phase
(Đầy đủ theo prompt từng Phase; dưới đây là các tiêu chí đo được, có điều chỉnh cho 2.5D và MinGW.)

### Phase 1 — Môi trường và repository
- [ ] `scripts/configure.ps1 -Preset win-debug` thành công từ thư mục build sạch; build Debug và Release đều thành công với MinGW GCC 15.2.
- [ ] Cửa sổ SDL3 mở, đóng bằng nút X hoặc Esc, exit code 0.
- [ ] `ctest` chạy ít nhất 1 test thật và pass.
- [ ] Dependency pin theo tag/commit, có bảng license (`docs/operations/dependencies.md`).
- [ ] `.gitignore` loại trừ build/cache; không có secret.
- [ ] Ghi rõ Linux build đã test thật (Docker) hay mới chuẩn bị.

### Phase 2 — Core engine
- [ ] Fixed timestep 30 Hz; test với delta 5 giây cho ra số tick bị giới hạn đúng.
- [ ] ECS: handle stale (entity đã destroy) bị phát hiện; test add/remove/query.
- [ ] Event unsubscribe khi đang dispatch không crash (test).
- [ ] Resource handle invalid bị phát hiện; config sai trả lỗi rõ.
- [ ] Log có FPS, frame time và số tick.

### Phase 3 — Vulkan renderer (2.5D)
- [ ] Camera orthographic góc cố định, xoay 90°, zoom giới hạn.
- [ ] Mesh có depth đúng, texture và material PBR, đèn directional, shadow map, tone mapping.
- [ ] Cutaway hoặc làm mờ vật che nhân vật (thay cho "camera tránh tường").
- [ ] Resize và minimize không crash; 0 validation error trong smoke test 60 giây.
- [ ] Hiển thị draw calls, số tam giác, CPU/GPU frame time; screenshot do chương trình chụp.
- [ ] Chạy được trên cả GTX 1650 và Intel UHD.

### Phase 4 — Asset pipeline
- [ ] Load GLB test (mesh + material) và render được.
- [ ] Test bounds, index, UV, file lỗi; `pawbound asset validate` trả exit code đúng; license thiếu thì fail.
- [ ] Skeleton và animation: PASS chỉ khi có fixture rigged (có thể là rig placeholder sinh bằng code; ghi rõ).
- [ ] Blender batch chạy headless (nếu Blender đã cài ở `D:\DevTools`).

### Phase 5 — World generation
- [ ] `pawbound world generate --seed 20261106 --config data/world/demo.json --output out/world` tạo khu 512 m.
- [ ] Generate 2 lần cho cùng `logical_hash`; golden hash trong test.
- [ ] Validator bắt được lỗi trong fixture lỗi cố ý (overlap, mất kết nối đường, spawn trong vật cản).
- [ ] Client render terrain, đường, nhà, cây, props; streaming không tạo object trùng (test).
- [ ] Generate dưới 10 giây cho khu MVP; báo cáo thống kê.

### Phase 6 — Chó và di chuyển
- [ ] WASD theo hướng camera; walk/trot/run/sprint/jump; va chạm với nhà; không rơi xuyên terrain.
- [ ] Camera follow mượt, xoay 90° có nội suy; vật che được làm mờ.
- [ ] Animation idle/walk/run: PASS chỉ khi có asset hoặc rig thật, nếu không thì PARTIAL.
- [ ] Breed và appearance cấu hình qua data; test input mapping, movement math, state transition.

### Phase 7 — Sinh tồn, inventory, combat
- [ ] Chỉ số thay đổi theo simulation time (test với fixed time); ăn, uống, nghỉ hoạt động.
- [ ] Combat có cooldown, tốn thể lực, hit validation; test deterministic damage.
- [ ] Inventory không vượt giới hạn, không duplicate (test thao tác lặp).
- [ ] PvP zone load từ world config, có test; có chính sách gục và hồi sinh.

### Phase 8 — NPC và AI
- [ ] NPC đi giữa các waypoint/POI; A* có giới hạn số node và timeout.
- [ ] Tính cách người khác nhau cho phản ứng khác nhau; chó NPC có roam/sniff/eat/flee/fight.
- [ ] AI LOD; ngân sách AI ≤ 3 ms/tick cho 100 NPC active (đo).
- [ ] Spawn/despawn theo chunk đúng (test).

### Phase 9 — Thế giới động
- [ ] Ngày/đêm quan sát được; test wrap thời gian; thời tiết có blend.
- [ ] Lịch NPC và tài nguyên theo giờ; sự kiện có cooldown và anti-repeat.
- [ ] Logic không phụ thuộc render frame (test chạy headless).

### Phase 10 — Multiplayer
- [ ] Server headless (Windows + Linux); 2 client thấy nhau di chuyển.
- [ ] Packet giả set HP, teleport, tạo item đều không có tác dụng (test).
- [ ] Packet malformed hoặc quá cỡ bị reject; disconnect/reconnect không crash.
- [ ] Benchmark 2/8/16/20/30 theo ngưỡng ở `multiplayer-design.md` §7.

### Phase 11 — Backend
- [ ] Đăng ký, đăng nhập, refresh, revoke; tạo nhân vật; lưu và load.
- [ ] Matchmaking trả server hoặc lỗi rõ; ticket hết hạn hoặc dùng lại bị server từ chối.
- [ ] Migration chạy từ DB rỗng; test unit và integration; không có secret thật.

### Phase 12 — Xã hội
- [ ] Bạn bè, party, guild CRUD, permission matrix test (gồm test âm tính leo thang quyền).
- [ ] Tên guild unique dưới truy cập đồng thời (test race); chat có rate limit, block/mute.

### Phase 13 — Thanh toán (mock/sandbox)
- [ ] Order 100.000 VND đi hết luồng mock; webhook sai chữ ký, sai số tiền hoặc sai currency bị từ chối.
- [ ] Webhook lặp không tạo entitlement hoặc guild trùng; client không tự đánh dấu paid được.

### Phase 14 — Gameplay nâng cao
- [ ] Bị bắt có cảnh báo và cửa sổ thoát; giải cứu bởi đồng đội; thời gian chờ có giới hạn.
- [ ] Test exploit: relog, disconnect khi đang cứu, nhận thưởng hai lần.

### Phase 15 — Release
- [ ] Không còn bug blocker hoặc critical; benchmark thật; backup và restore đã test; artifact có checksum; license đầy đủ; security review.
