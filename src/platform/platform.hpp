#pragma once
// Lớp mỏng bọc SDL3: khởi tạo, cửa sổ, vòng sự kiện. Không chứa logic gameplay.
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct SDL_Window;

namespace pb::platform {

struct WindowDesc {
  std::string title = "PAWBOUND";
  int width = 1280;
  int height = 720;
  bool hidden = false;     // dùng cho smoke test tự động
  bool resizable = true;
  bool vulkan = false;     // tạo cửa sổ có hỗ trợ Vulkan surface
};

struct GamepadSnapshot {
  bool connected = false;
  float lx = 0, ly = 0, lt = 0, rt = 0;  // ly dương = đẩy lên
  bool a = false, b = false, x = false, y = false, lb = false, rb = false;
};

struct FrameEvents {
  bool quit_requested = false;
  bool resized = false;
  bool minimized = false;
  int width = 0;
  int height = 0;
  std::vector<int> keys_pressed;  // SDL_Keycode của phím vừa nhấn (không tính auto-repeat)
  float wheel = 0.0f;             // cuộn chuột (dương = lên)
  bool mouse_left_pressed = false;
  bool mouse_right_pressed = false;
};

// RAII: khởi tạo SDL video khi tạo, SDL_Quit khi hủy.
class Platform {
 public:
  static std::unique_ptr<Platform> create(const WindowDesc& desc, std::string& error);
  ~Platform();
  Platform(const Platform&) = delete;
  Platform& operator=(const Platform&) = delete;

  // Đọc toàn bộ sự kiện đang chờ. Gọi một lần mỗi frame.
  FrameEvents pump_events();
  [[nodiscard]] SDL_Window* native_window() const { return window_; }
  [[nodiscard]] uint64_t ticks_ns() const;
  // Trạng thái phím hiện tại theo SDL_Scancode.
  [[nodiscard]] bool key_down(int scancode) const;
  [[nodiscard]] bool mouse_down(int button) const;
  [[nodiscard]] GamepadSnapshot gamepad() const;
  void drawable_size(int& w, int& h) const;

 private:
  Platform() = default;
  SDL_Window* window_ = nullptr;
  bool minimized_ = false;
  void* gamepad_ = nullptr;  // SDL_Gamepad*
};

}  // namespace pb::platform
