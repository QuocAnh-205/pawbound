# NPC, điều hướng và AI (Phase 8)

Mã: `src/sim/nav.*`, `src/sim/npc.*` (logic thuần, chạy được trên server); client: `src/client/npc_view.*`, `model_library.*`. Dữ liệu: `data/gameplay/npcs.json`.

## Điều hướng
- **Waypoint graph**, không phải navmesh: các nút nav do world generator tạo (tim đường chia đoạn ≤ 20 m, lối đi công viên, lối nối POI đã kiểm tra không xuyên nhà/hồ). NPC bám tim đường; va chạm thật do motor + `CollisionWorld` xử lý.
- A* (heuristic Euclid) giới hạn `max_expanded` = 3000 nút. Kết quả: Found / NoPath / Truncated / InvalidNode. Không bao giờ treo.
- Cache LRU 512 cặp (start, goal). Ngân sách **8 yêu cầu tìm đường/tick**, phần vượt hoãn sang tick sau.
- Kẹt (đi < 0,5 m trong 3 s) hoặc quá 60 s chưa tới đích thì bỏ đích và chọn đích khác, không lặp vô hạn.

## NPC
| Loại | Tính cách (tỉ lệ từ config) | Hành vi chính |
|---|---|---|
| Người | thân thiện 30% | thấy chó không tai tiếng thì lại gần và **cho xương** (sự kiện FeedPlayer, hồi chiêu 120 s); chó có tai tiếng ≥ 10 thì tránh |
| | sợ hãi 30% | bỏ chạy khi chó tới gần (5 m, khi cảnh giác 9 m) |
| | hung dữ 15% | dọa đuổi (Shout); tai tiếng cao thì gọi giúp (CallHelp → người xung quanh cảnh giác); bị cắn thì tự vệ, **đá** (−6 HP, 1,5 s) |
| | trung lập 25% | đi bộ giữa các điểm, dừng chân |
| Chó NPC | hung dữ 30% | gầm gừ 1,5 s → cắn người chơi bằng đúng `request_bite`; HP < 30% thì bỏ chạy |
| | thân thiện 30% | đi theo người chơi tối đa 30 s nếu người chơi không có tai tiếng |
| | trung lập 40% | lang thang, đánh hơi; bị cắn thì bỏ chạy |
Mọi chó NPC có nhu cầu đói/khát/nghỉ (nhanh gấp 6 lần người chơi): **tự tìm nguồn ăn/uống thật** trong world (dùng chung `ResourceNode` với người chơi).

Bị tấn công: HP giảm thì tự phát hiện, hoặc server gọi `on_attacked()`.

## Nhận biết
Tầm nhìn (người 14 m, góc 140°; chó 12 m, góc 200°) với line-of-sight (nhà chắn, ghế/cây không chắn); < 1,5 m luôn nhận biết. Nghe: `Noise` (tiếng cắn, bán kính 15 m) trong tầm nghe (18/22 m).

## Spawn / despawn / LOD
- Chỉ chunk trong bán kính 1 quanh mỗi người chơi mới có NPC. Mật độ theo district. Tối đa 120 NPC.
- Spawn **deterministic** theo (seed, chunk): rời đi rồi quay lại thì đúng tập NPC cũ (cùng id).
- AI LOD: NPC cách người chơi > 30 m chỉ tick 1/6 số tick (gộp dt).

## Hiển thị (client)
Người: hình khối **placeholder** màu áo theo tính cách (xanh lá = thân thiện, xanh dương = sợ, đỏ = hung dữ, xám = trung lập). Chó NPC: model chó placeholder skinned, animation riêng từng con. F3: chấm trạng thái trên đầu (đỏ = tấn công/dọa, xanh lá = thân thiện/theo, vàng = chạy, xanh dương = ăn/uống). HUD hiện số NPC và thời gian AI/tick.
