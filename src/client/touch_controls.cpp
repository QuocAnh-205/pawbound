#include "client/touch_controls.hpp"

#include <algorithm>
#include <cmath>

#include <glm/geometric.hpp>

namespace pb::client {

TouchControls::TouchControls() {
  // Bố cục ngang (landscape). Nhãn ASCII vì HUD dùng font ASCII.
  buttons_ = {
      {"CAN", "J", 0.90f, 0.80f, 0.090f},     {"NHAY", "Space", 0.78f, 0.88f, 0.070f},
      {"CHAY", "Left Shift", 0.93f, 0.57f, 0.070f, true}, {"E", "E", 0.79f, 0.67f, 0.065f},
      {"NE", "K", 0.67f, 0.88f, 0.060f},      {"NGUI", "F", 0.66f, 0.72f, 0.060f, true},
      {"NHAT", "G", 0.69f, 0.57f, 0.055f},    {"NGHI", "Z", 0.81f, 0.47f, 0.055f},
      {"<", "Q", 0.58f, 0.09f, 0.050f},       {">", "R", 0.66f, 0.09f, 0.050f},
      {"CHAT", "Return", 0.93f, 0.09f, 0.055f},
  };
  for (int i = 0; i < 6; ++i)
    buttons_.push_back({std::to_string(i + 1), std::to_string(i + 1), 0.30f + 0.045f * static_cast<float>(i), 0.09f, 0.035f});
}

int TouchControls::hit(glm::vec2 p, float aspect) const {
  for (size_t i = 0; i < buttons_.size(); ++i) {
    const auto& b = buttons_[i];
    // khoảng cách theo đơn vị chiều cao màn hình (x nhân aspect) -> nút tròn thật trên màn hình
    const glm::vec2 d{(p.x - b.cx) * aspect, p.y - b.cy};
    if (glm::length(d) <= b.r * 1.15f) return static_cast<int>(i);  // vùng chạm rộng hơn hình 15%
  }
  return -1;
}

void TouchControls::update(const std::vector<platform::TouchEvent>& events, float aspect) {
  aspect_ = aspect;
  for (const auto& e : events) {
    const glm::vec2 p{e.x, e.y};
    using T = platform::TouchEvent::Type;
    if (e.type == T::Down) {
      used_ = true;
      Finger f{p, p, hit(p, aspect)};
      if (f.button < 0 && p.x < 0.45f && std::none_of(fingers_.begin(), fingers_.end(), [](const auto& kv) { return kv.second.button == -2; }))
        f.button = -2;  // joystick: chạm đầu tiên ở nửa trái (vị trí đặt ngón là tâm)
      if (f.button >= 0) {
        const auto& b = buttons_[static_cast<size_t>(f.button)];
        if (b.hold) held_.insert(b.key);
        else pressed_.push_back(b.key);
      }
      fingers_[e.finger] = f;
    } else if (e.type == T::Move) {
      if (auto it = fingers_.find(e.finger); it != fingers_.end()) it->second.pos = p;
    } else {
      if (auto it = fingers_.find(e.finger); it != fingers_.end()) {
        if (it->second.button >= 0) held_.erase(buttons_[static_cast<size_t>(it->second.button)].key);
        fingers_.erase(it);
      }
    }
  }
  // Joystick: bán kính 12% chiều cao; vùng chết 15%
  move_ = glm::vec2(0.0f);
  std::vector<glm::vec2> free;
  for (const auto& [id, f] : fingers_) {
    if (f.button == -2) {
      glm::vec2 d{(f.pos.x - f.start.x) * aspect, -(f.pos.y - f.start.y)};
      const float len = glm::length(d) / 0.12f;
      if (len > 0.15f) move_ = glm::normalize(d) * std::min(1.0f, len);
    } else if (f.button == -1) {
      free.push_back({f.pos.x * aspect, f.pos.y});
    }
  }
  // Chụm 2 ngón (không phải joystick/nút) -> zoom
  if (free.size() == 2) {
    const float d = glm::distance(free[0], free[1]);
    if (pinch_dist_ > 0) zoom_ += (d - pinch_dist_) * 25.0f;
    pinch_dist_ = d;
  } else {
    pinch_dist_ = 0;
  }
}

std::vector<std::string> TouchControls::take_pressed() {
  std::vector<std::string> out;
  out.swap(pressed_);
  return out;
}

float TouchControls::take_zoom() {
  const float z = zoom_;
  zoom_ = 0;
  return z;
}

void TouchControls::draw(render::Overlay& o, int w, int h) const {
  if (!used_) return;
  const float W = static_cast<float>(w), H = static_cast<float>(h);
  for (const auto& b : buttons_) {
    const float r = b.r * H, x = b.cx * W, y = b.cy * H;
    const bool on = held_.contains(b.key);
    o.rect(x - r, y - r, 2 * r, 2 * r, on ? glm::vec4(1, 0.8f, 0.3f, 0.45f) : glm::vec4(0, 0, 0, 0.35f));
    const float s = std::max(1.2f, r / 22.0f);
    o.text(x - render::Overlay::text_width(b.label, s) * 0.5f, y - 3.5f * s, b.label, {1, 1, 1, 0.9f}, s);
  }
  for (const auto& [id, f] : fingers_)
    if (f.button == -2) {  // vẽ joystick tại chỗ đặt ngón
      const float R = 0.12f * H;
      o.rect(f.start.x * W - R, f.start.y * H - R, 2 * R, 2 * R, {1, 1, 1, 0.12f});
      o.rect(f.pos.x * W - R * 0.35f, f.pos.y * H - R * 0.35f, R * 0.7f, R * 0.7f, {1, 1, 1, 0.35f});
    }
}

}  // namespace pb::client
