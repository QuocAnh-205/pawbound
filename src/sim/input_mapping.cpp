#include "sim/input_mapping.hpp"

#include <cmath>

#include <glm/geometric.hpp>

namespace pb::sim {

const char* to_string(Action a) {
  static const char* n[] = {"move_forward", "move_back", "move_left", "move_right", "sprint", "walk", "jump",
                            "interact", "attack", "dodge", "sniff", "camera_left", "camera_right", "toggle_debug"};
  const auto i = static_cast<size_t>(a);
  return i < std::size(n) ? n[i] : "?";
}

InputBindings InputBindings::defaults() {
  InputBindings b;
  b.keys = {{Action::MoveForward, {"W", "Up"}},     {Action::MoveBack, {"S", "Down"}},
            {Action::MoveLeft, {"A", "Left"}},      {Action::MoveRight, {"D", "Right"}},
            {Action::Sprint, {"Left Shift"}},       {Action::Walk, {"Left Ctrl"}},
            {Action::Jump, {"Space"}},              {Action::Interact, {"E"}},
            {Action::Attack, {"J"}},                {Action::Dodge, {"K"}},
            {Action::Sniff, {"F"}},                 {Action::CameraLeft, {"Q"}},
            {Action::CameraRight, {"R"}},           {Action::ToggleDebug, {"F3"}}};
  return b;
}

InputBindings load_bindings(const std::filesystem::path& path, ConfigDiagnostics& diag) {
  InputBindings b = InputBindings::defaults();
  auto j = load_json_config(path, diag, /*required=*/false);
  if (!diag.ok() || !j.contains("bindings")) return b;
  ConfigReader r(j, "", diag);
  if (r.has("gamepad_deadzone")) {
    double dz = b.gamepad_deadzone;
    r.read("gamepad_deadzone", dz, 0.0, 0.9);
    b.gamepad_deadzone = static_cast<float>(dz);
  }
  auto br = r.child("bindings");
  for (int i = 0; i < static_cast<int>(Action::Count); ++i) {
    const auto a = static_cast<Action>(i);
    if (!br.has(to_string(a))) continue;
    std::vector<std::string> keys;
    br.read(to_string(a), keys);
    if (!keys.empty()) b.keys[a] = keys;
  }
  // Phát hiện một phím gán cho hai hành động di chuyển/nhảy (xung đột).
  std::map<std::string, Action> owner;
  for (const auto& [a, ks] : b.keys)
    for (const auto& k : ks) {
      if (auto it = owner.find(k); it != owner.end() && it->second != a)
        diag.warnings.push_back(std::string("phím '") + k + "' gán cho cả " + to_string(it->second) + " và " + to_string(a));
      owner[k] = a;
    }
  return b;
}

InputState map_input(const InputBindings& b, const std::function<bool(const std::string&)>& key_down,
                     const std::function<bool(const std::string&)>& pressed, const GamepadState& pad) {
  auto down = [&](Action a) {
    auto it = b.keys.find(a);
    if (it == b.keys.end()) return false;
    for (const auto& k : it->second)
      if (key_down(k)) return true;
    return false;
  };
  auto hit = [&](Action a) {
    auto it = b.keys.find(a);
    if (it == b.keys.end()) return false;
    for (const auto& k : it->second)
      if (pressed(k)) return true;
    return false;
  };
  InputState s;
  s.move = {static_cast<float>(down(Action::MoveRight)) - static_cast<float>(down(Action::MoveLeft)),
            static_cast<float>(down(Action::MoveForward)) - static_cast<float>(down(Action::MoveBack))};
  if (glm::dot(s.move, s.move) > 1.0f) s.move = glm::normalize(s.move);
  s.sprint = down(Action::Sprint);
  s.walk = down(Action::Walk);
  s.jump = hit(Action::Jump);
  s.interact = hit(Action::Interact);
  s.attack = hit(Action::Attack);
  s.dodge = hit(Action::Dodge);
  s.sniff = down(Action::Sniff);
  s.rotate_camera_left = hit(Action::CameraLeft);
  s.rotate_camera_right = hit(Action::CameraRight);
  s.toggle_debug = hit(Action::ToggleDebug);
  if (pad.connected) {
    glm::vec2 stick(pad.lx, pad.ly);
    const float m = glm::length(stick);
    if (m > b.gamepad_deadzone) {
      // Rescale ngoài deadzone về 0..1, ưu tiên gamepad nếu bàn phím không dùng.
      const glm::vec2 v = stick / m * std::min(1.0f, (m - b.gamepad_deadzone) / (1.0f - b.gamepad_deadzone));
      if (glm::dot(s.move, s.move) < 1e-6f) s.move = v;
    }
    s.sprint |= pad.rt > 0.5f;
    s.jump |= pad.a;
    s.attack |= pad.x;
    s.dodge |= pad.b;
    s.interact |= pad.y;
    s.rotate_camera_left |= pad.lb;
    s.rotate_camera_right |= pad.rb;
  }
  return s;
}

}  // namespace pb::sim
