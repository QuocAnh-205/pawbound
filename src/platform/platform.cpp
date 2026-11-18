#include "platform/platform.hpp"

#include <SDL3/SDL.h>

#include "core/log.hpp"

namespace pb::platform {

std::unique_ptr<Platform> Platform::create(const WindowDesc& desc, std::string& error) {
#if defined(__ANDROID__)
  // Điện thoại: chỉ chơi NGANG (xoay theo cảm biến giữa hai chiều ngang), toàn màn hình, không bị khóa màn hình
  SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
  SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "0");
#endif
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
    error = std::string("SDL_Init thất bại: ") + SDL_GetError();
    return nullptr;
  }
  SDL_WindowFlags flags = 0;
  if (desc.resizable) flags |= SDL_WINDOW_RESIZABLE;
  if (desc.hidden) flags |= SDL_WINDOW_HIDDEN;
#if defined(__ANDROID__)
  flags |= SDL_WINDOW_FULLSCREEN;  // ẩn thanh trạng thái/điều hướng (immersive)
#endif
  if (desc.vulkan) flags |= SDL_WINDOW_VULKAN;

  std::unique_ptr<Platform> p(new Platform());
  p->window_ = SDL_CreateWindow(desc.title.c_str(), desc.width, desc.height, flags);
  if (!p->window_) {
    error = std::string("SDL_CreateWindow thất bại: ") + SDL_GetError();
    SDL_Quit();
    return nullptr;
  }
  log::info("platform", "SDL {}.{}.{} — cửa sổ {}x{} đã tạo", SDL_MAJOR_VERSION, SDL_MINOR_VERSION,
            SDL_MICRO_VERSION, desc.width, desc.height);
  return p;
}

Platform::~Platform() {
  if (gamepad_) SDL_CloseGamepad(static_cast<SDL_Gamepad*>(gamepad_));
  if (window_) SDL_DestroyWindow(window_);
  SDL_Quit();
  log::info("platform", "SDL đã tắt");
}

FrameEvents Platform::pump_events() {
  FrameEvents ev;
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    switch (e.type) {
      case SDL_EVENT_QUIT:
      case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        ev.quit_requested = true;
        break;
      case SDL_EVENT_KEY_DOWN:
        // Đang gõ chat: Esc chỉ hủy ô nhập (client xử lý), không thoát game
        if (e.key.key == SDLK_ESCAPE && !SDL_TextInputActive(window_)) ev.quit_requested = true;
        if (e.key.key == SDLK_BACKSPACE && SDL_TextInputActive(window_)) ev.backspaces++;
        if (!e.key.repeat) ev.keys_pressed.push_back(static_cast<int>(e.key.key));
        break;
      case SDL_EVENT_FINGER_DOWN:
      case SDL_EVENT_FINGER_MOTION:
      case SDL_EVENT_FINGER_UP:
      case SDL_EVENT_FINGER_CANCELED:
        ev.touches.push_back({e.type == SDL_EVENT_FINGER_DOWN   ? TouchEvent::Type::Down
                              : e.type == SDL_EVENT_FINGER_MOTION ? TouchEvent::Type::Move
                                                                  : TouchEvent::Type::Up,
                              static_cast<uint64_t>(e.tfinger.fingerID), e.tfinger.x, e.tfinger.y});
        break;
      case SDL_EVENT_DID_ENTER_BACKGROUND:
        ev.app_background = true;
        break;
      case SDL_EVENT_DID_ENTER_FOREGROUND:
        ev.app_foreground = true;
        break;
      case SDL_EVENT_TEXT_INPUT:
        if (e.text.text) ev.text += e.text.text;
        break;
      case SDL_EVENT_GAMEPAD_ADDED:
        if (!gamepad_) {
          gamepad_ = SDL_OpenGamepad(e.gdevice.which);
          if (gamepad_) log::info("platform", "gamepad: {}", SDL_GetGamepadName(static_cast<SDL_Gamepad*>(gamepad_)));
        }
        break;
      case SDL_EVENT_GAMEPAD_REMOVED:
        if (gamepad_ && SDL_GetGamepadID(static_cast<SDL_Gamepad*>(gamepad_)) == e.gdevice.which) {
          SDL_CloseGamepad(static_cast<SDL_Gamepad*>(gamepad_));
          gamepad_ = nullptr;
          log::info("platform", "gamepad đã ngắt");
        }
        break;
      case SDL_EVENT_MOUSE_WHEEL:
        ev.wheel += e.wheel.y;
        break;
      case SDL_EVENT_MOUSE_MOTION:
        ev.mouse_dx += e.motion.xrel;
        ev.mouse_dy += e.motion.yrel;
        break;
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e.button.button == SDL_BUTTON_LEFT) ev.mouse_left_pressed = true;
        if (e.button.button == SDL_BUTTON_RIGHT) ev.mouse_right_pressed = true;
        break;
      case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        ev.resized = true;
        break;
      case SDL_EVENT_WINDOW_MINIMIZED:
        minimized_ = true;
        break;
      case SDL_EVENT_WINDOW_RESTORED:
      case SDL_EVENT_WINDOW_MAXIMIZED:
        minimized_ = false;
        ev.resized = true;
        break;
      default:
        break;
    }
  }
  ev.minimized = minimized_;
  drawable_size(ev.width, ev.height);
  return ev;
}

uint64_t Platform::ticks_ns() const { return SDL_GetTicksNS(); }

bool Platform::key_down(int scancode) const {
  int n = 0;
  const bool* state = SDL_GetKeyboardState(&n);
  return state && scancode >= 0 && scancode < n && state[scancode];
}

bool Platform::mouse_down(int button) const {
  const SDL_MouseButtonFlags f = SDL_GetMouseState(nullptr, nullptr);
  return (f & SDL_BUTTON_MASK(button)) != 0;
}

GamepadSnapshot Platform::gamepad() const {
  GamepadSnapshot g;
  auto* pad = static_cast<SDL_Gamepad*>(gamepad_);
  if (!pad) return g;
  auto axis = [&](SDL_GamepadAxis a) { return static_cast<float>(SDL_GetGamepadAxis(pad, a)) / 32767.0f; };
  auto btn = [&](SDL_GamepadButton b) { return SDL_GetGamepadButton(pad, b); };
  g.connected = true;
  g.lx = axis(SDL_GAMEPAD_AXIS_LEFTX);
  g.ly = -axis(SDL_GAMEPAD_AXIS_LEFTY);
  g.lt = axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
  g.rt = axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
  g.a = btn(SDL_GAMEPAD_BUTTON_SOUTH);
  g.b = btn(SDL_GAMEPAD_BUTTON_EAST);
  g.x = btn(SDL_GAMEPAD_BUTTON_WEST);
  g.y = btn(SDL_GAMEPAD_BUTTON_NORTH);
  g.lb = btn(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
  g.rb = btn(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
  return g;
}

void Platform::drawable_size(int& w, int& h) const {
  w = h = 0;
  if (window_) SDL_GetWindowSizeInPixels(window_, &w, &h);
}

void Platform::set_text_input(bool on) {
  if (on) SDL_StartTextInput(window_);
  else SDL_StopTextInput(window_);
}
bool Platform::text_input_active() const { return SDL_TextInputActive(window_); }
void Platform::set_relative_mouse(bool on) {
  if (SDL_GetWindowRelativeMouseMode(window_) != on) SDL_SetWindowRelativeMouseMode(window_, on);
}
bool Platform::window_focused() const { return (SDL_GetWindowFlags(window_) & SDL_WINDOW_INPUT_FOCUS) != 0; }

}  // namespace pb::platform
