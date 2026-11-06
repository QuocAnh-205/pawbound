# Core Engine (`pb_core`)

| File | Nội dung |
|---|---|
| `application.hpp/.cpp` | Vòng đời: `on_init → [on_frame_begin → on_fixed_update×N → on_update → on_render(alpha)] → on_shutdown`. Bắt exception, luôn shutdown sau init thành công. `now_seconds()` override được để test. Log FPS, frame time và tick/s định kỳ. |
| `time.hpp` | `FixedTimestep` (mặc định 30 Hz, tối đa 5 tick/frame, cắt delta > 0,25 giây, ghi lại thời gian bị bỏ), `FrameStats`. |
| `ecs.hpp` | ECS sparse-set. `Entity{index, generation}`; generation tăng khi destroy nên handle cũ bị phát hiện. `each<A,B>` duyệt ngược trên pool nhỏ nhất; cho phép destroy trong lúc duyệt. |
| `event.hpp` | `EventChannel<E>`: kênh có kiểu (không có global bus). `Subscription` là RAII, an toàn khi channel bị hủy trước (weak_ptr). Có `publish` ngay và `enqueue/flush`. |
| `resource.hpp` | `ResourceRegistry<T>` + `Handle<T>{index, generation}`. Trạng thái Loading/Loaded/Failed; release tăng generation. |
| `config_reader.hpp/.cpp` | Đọc JSON có comment; thiếu key thì dùng mặc định và cảnh báo; sai kiểu hoặc ngoài khoảng thì báo lỗi; hỗ trợ object con, range. |
| `job.hpp/.cpp` | `ThreadPool` + `std::future`. **Quy tắc:** job không sửa ECS hay state gameplay; kết quả được áp trên main/sim thread. |
| `services.hpp` | Interface `IInputService`, `IRenderService`, `IPhysicsService`, `IAudioService`, `InputState` (input theo hành động). |
| `log.hpp/.cpp` | Logger (Phase 1). |

## Trade-off ECS
Sparse-set thay vì archetype: add/remove O(1), code ngắn và dễ debug. Query nhiều component phải kiểm tra `contains` cho từng pool khác, nhưng với vài nghìn entity mỗi server thì đủ nhanh. Nếu profiling ở Phase 15 cho thấy nghẽn, có thể thêm group/cache cho query nóng.
