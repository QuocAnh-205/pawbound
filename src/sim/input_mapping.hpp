#pragma once
// Ánh xạ phím/nút -> hành động game (InputState). Logic thuần: nhận hàm truy vấn trạng thái phím,
// nên test được không cần SDL. Bindings đọc từ config/input.json (tên phím theo SDL).
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "core/config_reader.hpp"
#include "core/services.hpp"

namespace pb::sim {

enum class Action { MoveForward, MoveBack, MoveLeft, MoveRight, Sprint, Walk, Jump, Interact, Attack, Dodge, Sniff,
                    CameraLeft, CameraRight, ToggleDebug, Count };
const char* to_string(Action a);

struct InputBindings {
  std::map<Action, std::vector<std::string>> keys;  // tên phím SDL, vd "W", "Left Shift", "Space"
  float gamepad_deadzone = 0.2f;
  static InputBindings defaults();
};

InputBindings load_bindings(const std::filesystem::path& path, ConfigDiagnostics& diag);

struct GamepadState {
  bool connected = false;
  float lx = 0, ly = 0;  // -1..1, ly dương = đẩy lên
  bool a = false, b = false, x = false, y = false, lb = false, rb = false;
  float rt = 0, lt = 0;
};

// key_down(tên phím) -> đang giữ. pressed(tên phím) -> vừa nhấn trong frame này.
InputState map_input(const InputBindings& b, const std::function<bool(const std::string&)>& key_down,
                     const std::function<bool(const std::string&)>& pressed, const GamepadState& pad);

}  // namespace pb::sim
