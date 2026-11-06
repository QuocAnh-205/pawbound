# Nhân vật chó, camera và di chuyển (Phase 6)

## Thành phần
| File | Vai trò |
|---|---|
| `src/sim/dog_motor.*` | Motor fixed-step 30 Hz: tăng/giảm tốc có giới hạn, quay hướng (600°/s), gait idle/walk/trot/run/sprint/air, nhảy + trọng lực, bám địa hình, chặn dốc > 40°, chia bước con để không xuyên vật mỏng khi chạy nhanh. **Dùng chung cho server** (Phase 10). |
| `src/sim/collision.*` | Va chạm 2.5D: hộp (nhà, ghế, thùng lớn), hình tròn (cây, đèn, trụ, bụi, đá), hồ (không lội qua được), biên thế giới. Vật thấp hơn `step_height` (0,3 m) thì bước qua; khi nhảy, vật thấp hơn chân thì bay qua. Lưới không gian 8 m. Xem ADR-006. |
| `src/sim/dog_archetype.*` + `data/dogs/breeds.json` | Baseline chỉ số + breed. **Giới hạn cân bằng:** `speed_mod` ∈ [0,95; 1,05], `size` ∈ [0,8; 1,2]; ngoài khoảng thì config bị từ chối. `DogAppearance` (màu lông, size_variant, vòng cổ) là cosmetic thuần: `movement_for()` không nhận appearance. |
| `src/sim/anim_graph.*` | State machine animation: idle / walk (cả trot) / run (cả sprint, air), crossfade 0,2 s, tốc độ phát = tốc độ thực / tốc độ clip (chân không trượt). |
| `src/sim/input_mapping.*` + `config/input.json` | Phím → hành động (tên phím SDL), nhiều phím mỗi hành động, cảnh báo xung đột; gamepad có deadzone (SDL3 gamepad). |
| `src/client/dog_actor.*` | Ghép mọi phần: nạp model **qua manifest**, GPU skinning, nội suy vị trí/yaw giữa hai tick sim, vòng va chạm debug. Nếu model hỏng/thiếu thì dùng chó hình khối **PLACEHOLDER**. |
| Renderer | GPU skinning: buffer đỉnh thứ hai (4 joint u16 + 4 weight), SSBO ma trận joint mỗi frame (tối đa 8192), pipeline skinned cho cả scene và shadow. |

## Camera (2.5D, ADR-002)
Ortho, pitch 50°, bám theo chó có làm mượt, Q/R xoay 90°, zoom 3,5–30 (mặc định 8). "Tránh tường" trong 2.5D được thay bằng **cutaway**: vật che giữa camera và nhân vật bị dither. Nhân vật không bao giờ bị cutaway.

## Model hiện có
| Breed | Model | Chất lượng |
|---|---|---|
| `street_mix`, `shiba` | Fox (Khronos) | **prototype**: là cáo, không phải chó |
| `shepherd` | chó hình khối sinh bằng code | **placeholder** |

## Chạy
```powershell
.\build\win-debug\bin\pawbound_client.exe --world out\world_demo [--breed shiba] [--debug] [--zoom 5]
```
F3 bật/tắt debug: tiêu đề cửa sổ hiện vị trí, vận tốc, gait, trạng thái chạm đất, clip/tốc độ phát, va chạm; dưới chân chó có vòng bán kính va chạm.
