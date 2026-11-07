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

// Chạm màn hình (điện thoại). Tọa độ chuẩn hóa 0..1 theo cửa sổ.
struct TouchEvent {
  enum class Type : uint8_t { Down, Move, Up } type = Type::Down;
  uint64_t finger = 0;
  float x = 0, y = 0;
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
  float mouse_dx = 0, mouse_dy = 0;  // chuyển động chuột (pixel) trong frame — dùng để xoay camera 3D
  std::string text;     // UTF-8 gõ được trong frame (chỉ khi đang bật text input)
  int backspaces = 0;   // số lần Backspace (kể cả giữ phím lặp)
  std::vector<TouchEvent> touches;
  bool app_background = false;  // Android: app bị đưa xuống nền (dừng render)
  bool app_foreground = false;
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
  // Bật/tắt nhập văn bản (IME, bàn phím ảo trên điện thoại)
  void set_text_input(bool on);
  // Khóa/ẩn con trỏ để xoay camera bằng chuột (chế độ 3D)
  void set_relative_mouse(bool on);
  [[nodiscard]] bool window_focused() const;
  [[nodiscard]] bool text_input_active() const;

 private:
  Platform() = default;
  SDL_Window* window_ = nullptr;
  bool minimized_ = false;
  void* gamepad_ = nullptr;  // SDL_Gamepad*
};

}  // namespace pb::platform
