#pragma once
// Điều khiển cảm ứng cho điện thoại: joystick ảo nửa trái, nút bên phải, chụm 2 ngón để zoom.
// Xuất ra "phím ảo" (tên phím SDL như bàn phím) -> tái dùng nguyên logic input/bindings của bản PC.
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <glm/vec2.hpp>

#include "platform/platform.hpp"
#include "render/overlay.hpp"

namespace pb::client {

struct TouchButton {
  std::string label, key;  // key: tên phím SDL tương ứng (vd "J", "Left Shift")
  float cx, cy, r;         // tâm (0..1 theo bề ngang/dọc), bán kính theo chiều cao màn hình
  bool hold = false;       // giữ (nước rút, đánh hơi) hay nhấn một lần
};

class TouchControls {
 public:
  TouchControls();
  // Xử lý sự kiện chạm của frame. aspect = rộng/cao.
  void update(const std::vector<platform::TouchEvent>& events, float aspect);
  [[nodiscard]] bool active() const { return used_; }  // đã từng chạm -> hiển thị điều khiển
  [[nodiscard]] glm::vec2 move() const { return move_; }  // x phải, y lên; độ dài ≤ 1
  [[nodiscard]] bool held(const std::string& key) const { return held_.contains(key); }
  // Phím vừa nhấn trong frame (lấy xong thì xóa)
  std::vector<std::string> take_pressed();
  [[nodiscard]] float take_zoom();  // >0 phóng to
  glm::vec2 take_look();            // vuốt 1 ngón ở vùng trống: xoay camera (đơn vị: phần chiều rộng màn hình)
  void draw(render::Overlay& o, int w, int h) const;
  [[nodiscard]] const std::vector<TouchButton>& buttons() const { return buttons_; }

 private:
  struct Finger {
    glm::vec2 start, pos;
    int button = -1;  // chỉ số nút; -2 = joystick; -1 = tự do (zoom)
  };
  int hit(glm::vec2 p, float aspect) const;
  std::vector<TouchButton> buttons_;
  std::map<uint64_t, Finger> fingers_;
  std::set<std::string> held_;
  std::vector<std::string> pressed_;
  glm::vec2 move_{0.0f};
  float zoom_ = 0, pinch_dist_ = 0;
  glm::vec2 look_{0.0f};
  std::map<uint64_t, glm::vec2> last_free_;
  bool used_ = false;
  float aspect_ = 16.0f / 9.0f;
};

}  // namespace pb::client
