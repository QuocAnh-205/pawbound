#pragma once
// Interface dịch vụ để core/game không phụ thuộc implementation cụ thể (SDL, Vulkan, Jolt, miniaudio).
// Implementation thật nằm ở module tương ứng; server dùng bản headless hoặc không cần.
#include <cstdint>
#include <string_view>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace pb {

// Trạng thái input đã trừu tượng hóa theo hành động game (không phải phím vật lý).
struct InputState {
  glm::vec2 move{0.0f};  // x: phải(+)/trái(-), y: tiến(+)/lùi(-); độ dài ≤ 1
  bool sprint = false;
  bool walk = false;
  bool jump = false;
  bool interact = false;
  bool attack = false;
  bool dodge = false;
  bool sniff = false;
  bool rotate_camera_left = false;   // cạnh lên (pressed this frame)
  bool rotate_camera_right = false;
  float zoom_delta = 0.0f;
  bool toggle_debug = false;
};

class IInputService {
 public:
  virtual ~IInputService() = default;
  [[nodiscard]] virtual const InputState& state() const = 0;
};

class IAudioService {
 public:
  virtual ~IAudioService() = default;
  virtual void play_one_shot(std::string_view sound_id, const glm::vec3& position) = 0;
  virtual void set_listener(const glm::vec3& position) = 0;
};

// Renderer không có interface trừu tượng: client là nơi duy nhất dùng nó và server không render.
// Xem src/render/renderer.hpp.

class IPhysicsService {
 public:
  virtual ~IPhysicsService() = default;
  virtual void step(double dt) = 0;
  // Raycast trả khoảng cách tới va chạm đầu tiên hoặc < 0 nếu không trúng.
  [[nodiscard]] virtual float raycast(const glm::vec3& origin, const glm::vec3& dir, float max_dist) const = 0;
};

}  // namespace pb
