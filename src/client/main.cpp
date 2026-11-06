// PAWBOUND client — điểm vào.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <charconv>
#include <cmath>
#include <format>
#include <string>
#include <string_view>

#include "client/test_scene.hpp"
#include "client/dog_actor.hpp"
#include "client/game_session.hpp"
#include "client/net_game.hpp"
#include <fstream>
#include "client/qa_scenario.hpp"
#include "render/overlay.hpp"
#include "sim/gameplay.hpp"
#include "client/world_view.hpp"
#include "sim/collision.hpp"
#include "sim/input_mapping.hpp"
#include <set>
#include <unordered_map>
#include "world/world_io.hpp"
#include "core/application.hpp"
#include "core/client_config.hpp"
#include "core/log.hpp"
#include "core/version.hpp"
#include "geom/camera.hpp"
#include "platform/platform.hpp"
#include "render/renderer.hpp"

namespace {
struct Args {
  std::string config_path = "config/client.json";
  long long max_frames = -1;
  bool hidden = false;
  bool validation = false;
  bool no_render = false;  // chỉ mở cửa sổ (smoke test Phase 1)
  bool resize_test = false;  // tự đổi kích thước/thu nhỏ cửa sổ để kiểm thử swapchain
  std::string gpu;
  std::string screenshot;
  float time_of_day = 15.0f;
  bool debug = false;
  std::string world_dir;  // rỗng = test scene Phase 3
  std::string asset_root = ".";  // thư mục chứa assets/, data/, config/
  std::string breed = "street_mix";
  std::string scenario;  // "survival" = kịch bản QA tự động
  std::string weather, event;  // ép thời tiết/sự kiện lúc bắt đầu (QA)
  std::string connect, ticket;  // chế độ online: host:port + session ticket (chuỗi hoặc @file)
  float zoom = 0.0f;      // 0 = mặc định
  float at_x = -1, at_z = -1;  // vị trí bắt đầu (m), -1 = spawn
  bool ok = true;
};

bool parse_ll(std::string_view v, long long& out) {
  return std::from_chars(v.data(), v.data() + v.size(), out).ec == std::errc{};
}

Args parse_args(int argc, char** argv) {
  Args a;
#ifndef NDEBUG
  a.validation = true;  // Debug build: bật validation layer mặc định
#endif
  for (int i = 1; i < argc; ++i) {
    std::string_view s = argv[i];
    auto next = [&]() -> std::string_view { return i + 1 < argc ? std::string_view(argv[++i]) : std::string_view(); };
    if (s == "--config") a.config_path = next();
    else if (s == "--frames") a.ok &= parse_ll(next(), a.max_frames);
    else if (s == "--hidden") a.hidden = true;
    else if (s == "--validation") a.validation = true;
    else if (s == "--no-validation") a.validation = false;
    else if (s == "--no-render") a.no_render = true;
    else if (s == "--resize-test") a.resize_test = true;
    else if (s == "--gpu") a.gpu = next();
    else if (s == "--screenshot") a.screenshot = next();
    else if (s == "--world") a.world_dir = next();
    else if (s == "--asset-root") a.asset_root = next();
    else if (s == "--breed") a.breed = next();
    else if (s == "--scenario") a.scenario = next();
    else if (s == "--weather") a.weather = next();
    else if (s == "--connect") a.connect = next();
    else if (s == "--ticket") a.ticket = next();
    else if (s == "--event") a.event = next();
    else if (s == "--debug") a.debug = true;
    else if (s == "--at") {
      long long x = 0, z = 0;
      a.ok &= parse_ll(next(), x);
      a.ok &= parse_ll(next(), z);
      a.at_x = static_cast<float>(x);
      a.at_z = static_cast<float>(z);
    } else if (s == "--zoom") {
      long long z = 0;
      a.ok &= parse_ll(next(), z);
      a.zoom = static_cast<float>(z);
    }
    else if (s == "--time") {
      long long t = 15;
      a.ok &= parse_ll(next(), t);
      a.time_of_day = static_cast<float>(t);
    } else {
      pb::log::error("client", "tham số không hợp lệ: {}", s);
      a.ok = false;
    }
  }
  return a;
}

class ClientApp final : public pb::Application {
 public:
  ClientApp(Args args, pb::ClientConfig cfg)
      : pb::Application(pb::AppSettings{30.0, 5, 0.25, 5.0, "client"}), args_(std::move(args)), cfg_(std::move(cfg)) {}

  uint32_t validation_errors() const { return val_errors_; }

 protected:
  bool on_init() override {
    std::string err;
    pb::platform::WindowDesc wd;
    wd.title = cfg_.window_title;
    wd.width = cfg_.window_width;
    wd.height = cfg_.window_height;
    wd.hidden = args_.hidden;
    wd.vulkan = !args_.no_render;
    platform_ = pb::platform::Platform::create(wd, err);
    if (!platform_) {
      pb::log::fatal("client", "{}", err);
      return false;
    }
    if (args_.no_render) return true;
    pb::render::RendererSettings rs;
    rs.validation = args_.validation;
    rs.vsync = cfg_.vsync;
    if (!args_.gpu.empty()) rs.gpu_preference = args_.gpu;
    renderer_ = pb::render::Renderer::create(platform_->native_window(), rs, err);
    if (!renderer_) {
      pb::log::fatal("client", "khởi tạo renderer thất bại: {}", err);
      return false;
    }
    if (!args_.world_dir.empty()) {
      auto lr = pb::world::load_world(args_.world_dir);
      if (!lr.ok()) {
        pb::log::fatal("client", "không nạp được world: {}", lr.error);
        return false;
      }
      pb::log::info("client", "world seed {} ({} nhà, {} props) — hash {}", lr.world.seed, lr.world.stats.buildings,
                    lr.world.stats.props, lr.stored_hash.substr(0, 16));
      world_hash_ = lr.stored_hash;
      world_ = std::make_unique<pb::client::WorldView>(*renderer_, std::move(lr.world), args_.world_dir);
      scene_.focus = world_->spawn_point();
      if (args_.at_x >= 0) scene_.focus = {args_.at_x, world_->data().height_at(args_.at_x, args_.at_z), args_.at_z};
      // Phase 6: chó người chơi
      const std::filesystem::path root(args_.asset_root);
      pb::ConfigDiagnostics cd;
      catalog_ = pb::sim::load_dog_catalog(root / "data/dogs/breeds.json", cd);
      for (const auto& e : cd.errors) pb::log::error("dog", "{}", e);
      if (!cd.ok()) return false;
      pb::ConfigDiagnostics bd;
      bindings_ = pb::sim::load_bindings(root / "config/input.json", bd);
      for (const auto& w : bd.warnings) pb::log::warn("input", "{}", w);
      collision_ = std::make_unique<pb::sim::CollisionWorld>(world_->data());
      {  // Vị trí xuất phát (spawn hoặc --at) không được nằm trong vật cản
        glm::vec2 p(scene_.focus.x, scene_.focus.z);
        collision_->resolve(p, 0.6f, world_->data().height_at(p.x, p.y), 0.3f);
        scene_.focus = {p.x, world_->data().height_at(p.x, p.y), p.y};
      }
      pb::ConfigDiagnostics gd;
      gameplay_ = pb::sim::load_gameplay_config(root / "data/gameplay/gameplay.json", gd);
      npc_cfg_ = pb::sim::load_npc_config(root / "data/gameplay/npcs.json", gd);
      world_cfg_ = pb::sim::load_world_sim_config(root / "data/gameplay/world_sim.json", gd);
      for (const auto& e : gd.errors) pb::log::error("game", "{}", e);
      if (!gd.ok()) return false;
      if (!args_.connect.empty()) {  // ---- ONLINE: server authoritative
        std::string ticket = args_.ticket;
        if (!ticket.empty() && ticket[0] == '@') {
          std::ifstream tf(ticket.substr(1));
          tf >> ticket;
        }
        const auto colon = args_.connect.rfind(':');
        const std::string host = args_.connect.substr(0, colon);
        const auto port = static_cast<uint16_t>(colon == std::string::npos ? pb::net::kDefaultPort : std::stoi(args_.connect.substr(colon + 1)));
        net_ = std::make_unique<pb::client::NetGame>(*renderer_, catalog_, root);
        std::string nerr;
        if (!net_->connect(host, port, ticket, world_hash_, nerr)) {
          pb::log::fatal("client", "kết nối thất bại: {}", nerr);
          return false;
        }
        camera_.set_target(scene_.focus, true);
        return true;
      }
      session_ = std::make_unique<pb::client::GameSession>(*renderer_, gameplay_, catalog_, world_->data(), *collision_, root,
                                                           args_.breed, scene_.focus, npc_cfg_, world_cfg_);
      session_->world_sim().clock().set_hour(time_of_day_);
      if (!args_.weather.empty()) {
        if (auto w = pb::sim::weather_from_string(args_.weather)) session_->world_sim().force_weather(*w, true);
        else pb::log::warn("client", "--weather không hợp lệ: {}", args_.weather);
      }
      if (!args_.event.empty()) session_->debug_event(pb::sim::event_from_string(args_.event));
      debug_ = args_.debug;
      pb::log::info("client", "va chạm: {} box, {} hình tròn", collision_->box_count(), collision_->circle_count());
    } else {
      scene_ = pb::client::build_test_scene(*renderer_, args_.asset_root);
    }
    camera_.set_target(scene_.focus, true);
    if (args_.zoom > 0) camera_.add_zoom(args_.zoom - camera_.zoom());
    pb::log::info("client", "renderer sẵn sàng trên {} — {}", renderer_->device_name(),
                  world_ ? "world procedural" : "test scene");
    return true;
  }

  void on_frame_begin() override {
    auto ev = platform_->pump_events();
    if (ev.quit_requested) request_quit(0);
    if (ev.resized && renderer_) renderer_->notify_resized();
    minimized_ = ev.minimized || ev.width == 0 || ev.height == 0;
    width_ = ev.width;
    height_ = ev.height;
    aspect_ = ev.height > 0 ? static_cast<float>(ev.width) / static_cast<float>(ev.height) : 1.0f;
    for (int k : ev.keys_pressed) pressed_.insert(SDL_GetKeyName(static_cast<SDL_Keycode>(k)));
    if (ev.mouse_left_pressed) pressed_.insert("__mouse_left");
    for (int k : ev.keys_pressed) {
      if (!session_ && k == SDLK_Q) camera_.rotate_step(-1);
      if (!session_ && k == SDLK_E) camera_.rotate_step(+1);
      if (k == SDLK_F12 && renderer_) renderer_->request_screenshot("screenshots/manual.png");
      if (k == SDLK_T) {
        time_of_day_ = std::fmod(time_of_day_ + 1.0f, 24.0f);
        if (session_) session_->world_sim().clock().set_hour(session_->world_sim().clock().hour() + 1.0);
      }
      if (k == SDLK_Y && session_) session_->debug_next_weather();
      if (k == SDLK_U && session_) session_->debug_event(std::nullopt);
    }
    if (ev.wheel != 0.0f) camera_.add_zoom(-ev.wheel * 1.5f);
    if (args_.resize_test) {
      SDL_Window* w = platform_->native_window();
      switch (frame_count()) {
        case 20: SDL_SetWindowSize(w, 800, 600); break;
        case 40: SDL_SetWindowSize(w, 1600, 900); break;
        case 60: SDL_MinimizeWindow(w); break;
        case 80: SDL_RestoreWindow(w); break;
        case 100: SDL_SetWindowSize(w, 640, 360); break;
        default: break;
      }
    }
    if (args_.max_frames >= 0 && static_cast<long long>(frame_count()) >= args_.max_frames) {
      if (!args_.screenshot.empty() && renderer_ && !shot_taken_) {
        renderer_->request_screenshot(args_.screenshot);  // chụp frame cuối
        shot_taken_ = true;
      } else {
        request_quit(0);
      }
    }
  }

  pb::InputState read_input() {
    auto key_down = [&](const std::string& name) {
      auto it = scancodes_.find(name);
      if (it == scancodes_.end()) it = scancodes_.emplace(name, static_cast<int>(SDL_GetScancodeFromName(name.c_str()))).first;
      return it->second != SDL_SCANCODE_UNKNOWN && platform_->key_down(it->second);
    };
    auto was_pressed = [&](const std::string& name) { return pressed_.contains(name); };
    const auto gp = platform_->gamepad();
    pb::sim::GamepadState pad{gp.connected, gp.lx, gp.ly, gp.a, gp.b, gp.x, gp.y, gp.lb, gp.rb, gp.rt, gp.lt};
    auto in = pb::sim::map_input(bindings_, key_down, was_pressed, pad);
    session_pressed_.assign(pressed_.begin(), pressed_.end());
    pressed_.clear();  // cạnh lên chỉ dùng một lần
    if (args_.resize_test) {  // smoke test: tự chạy, thỉnh thoảng nước rút và nhảy
      in.move = {0.35f, 1.0f};
      in.sprint = (tick_count() / 90) % 2 == 1;
      in.jump = tick_count() % 75 == 0;
    }
    return in;
  }

  void on_fixed_update(double dt) override {
    if (net_) {
      const auto in = read_input();
      if (in.rotate_camera_left) camera_.rotate_step(-1);
      if (in.rotate_camera_right) camera_.rotate_step(+1);
      if (in.toggle_debug) debug_ = !debug_;
      net_->fixed_update(in, camera_, static_cast<float>(dt));
      if (!net_->connected() && args_.max_frames < 0) request_quit(6);
      return;
    }
    if (!session_) return;
    const auto in = read_input();
    if (in.rotate_camera_left) camera_.rotate_step(-1);
    if (in.rotate_camera_right) camera_.rotate_step(+1);
    if (in.toggle_debug) debug_ = !debug_;
    if (args_.scenario == "survival") {
      auto in2 = in;
      if (scenario_.step(*session_, in2, session_pressed_)) {
        pb::log::info("qa", "kịch bản survival: {}", scenario_.passed() ? "PASS" : "FAIL");
        scenario_exit_ = scenario_.passed() ? 0 : 5;
        request_quit(scenario_exit_);
      }
      session_->fixed_update(in2, session_pressed_, camera_, static_cast<float>(dt));
      return;
    }
    session_->fixed_update(in, session_pressed_, camera_, static_cast<float>(dt));
  }

  void on_update(double dt) override {
    if (net_) {
      net_->update_visual(static_cast<float>(dt));
      scene_.focus = net_->focus();
      camera_.set_target(scene_.focus);
      world_->update(scene_.focus);
    } else if (session_) {
      session_->update_visual(static_cast<float>(dt));
      scene_.focus = session_->player().position(static_cast<float>(timestep().alpha()));
      camera_.set_target(scene_.focus);
      world_->update(scene_.focus);
    } else if (world_) {  // không có chó (không xảy ra trong luồng bình thường): camera tự do
      glm::vec2 in(0.0f);
      if (platform_->key_down(SDL_SCANCODE_W)) in.y += 1;
      if (platform_->key_down(SDL_SCANCODE_S)) in.y -= 1;
      if (platform_->key_down(SDL_SCANCODE_D)) in.x += 1;
      if (platform_->key_down(SDL_SCANCODE_A)) in.x -= 1;
      const float speed = platform_->key_down(SDL_SCANCODE_LSHIFT) ? 40.0f : 12.0f;
      if (args_.resize_test) in = {0.6f, 1.0f};  // smoke test: tự đi để kiểm tra streaming
      if (glm::dot(in, in) > 0) {
        in = glm::normalize(in);
        scene_.focus += (camera_.ground_forward() * in.y + camera_.ground_right() * in.x) * speed * static_cast<float>(dt);
        const auto& wd = world_->data();
        scene_.focus.x = std::clamp(scene_.focus.x, 0.0f, pb::world::to_m(wd.size_x));
        scene_.focus.z = std::clamp(scene_.focus.z, 0.0f, pb::world::to_m(wd.size_z));
        scene_.focus.y = wd.height_at(scene_.focus.x, scene_.focus.z);
      }
      camera_.set_target(scene_.focus);
      if (int n = world_->update(scene_.focus); n > 0)
        pb::log::debug("world", "nạp {} chunk, đang giữ {}", n, world_->loaded_chunks());
    }
    camera_.update(static_cast<float>(dt));
  }

  void on_render(double) override {
    if (!renderer_) {
      SDL_Delay(1);
      return;
    }
    if (minimized_) {
      SDL_Delay(16);
      return;
    }
    pb::render::SceneParams sp;
    sp.view = camera_.view();
    sp.proj = camera_.proj(aspect_);
    sp.camera_back_dir = camera_.back_dir();
    sp.focus = scene_.focus;
    sp.cutaway_radius = 4.0f;
    if (world_) {
      draw_items_.clear();
      sp.point_lights.clear();
      world_->collect(draw_items_, sp.point_lights);
      if (session_) session_->append_draws(static_cast<float>(timestep().alpha()), debug_, draw_items_);
      if (net_) {
        net_->append_draws(debug_, draw_items_);
        pb::sim::WeatherEffects fx;
        const auto w = static_cast<pb::sim::Weather>(net_->weather());
        fx.cloud = w == pb::sim::Weather::Clear ? 0.0f : w == pb::sim::Weather::Fog ? 0.5f : 0.8f;
        fx.rain = w == pb::sim::Weather::Rain ? 0.6f : w == pb::sim::Weather::Storm ? 1.0f : 0.0f;
        fx.fog = w == pb::sim::Weather::Fog ? 1.0f : 0.0f;
        const auto l = pb::sim::lighting_for(net_->hour(), fx);
        sp.sun_dir = l.sun_dir;
        sp.sun_color = l.sun_color;
        sp.sun_intensity = l.sun_intensity;
        sp.sky_color = l.sky;
        sp.ambient_intensity = l.ambient;
        sp.clear_color = glm::vec4(l.sky, 1.0f);
        sp.fog_color = l.sky;
        sp.fog_density = l.fog_density;
        for (auto& pl : sp.point_lights) pl.intensity *= l.street_lights;
      } else if (session_) {
        apply_lighting(sp, session_->world_sim());
        sp.exposure += session_->lightning() * 1.5f;
      } else {
        apply_time_of_day(sp, time_of_day_);
      }
      overlay_.clear();
      if (session_) session_->draw_hud(overlay_, width_, height_, time_of_day_);
      if (net_) net_->draw_hud(overlay_, width_, height_);
      renderer_->draw_frame(sp, draw_items_, &overlay_);
    } else {
      sp.point_lights = scene_.lights;
      apply_time_of_day(sp, time_of_day_);
      renderer_->draw_frame(sp, scene_.items);
    }
    if (frame_count() % 30 == 0 && session_ && debug_) {
      SDL_SetWindowTitle(platform_->native_window(), std::format("[F3 DEBUG] {} | draws {} | CPU {:.2f}ms GPU {:.2f}ms",
                                                                 session_->player().debug_text(), renderer_->stats().draw_calls,
                                                                 renderer_->stats().cpu_ms, renderer_->stats().gpu_ms)
                                                         .c_str());
    } else if (frame_count() % 30 == 0) {
      const auto& st = renderer_->stats();
      SDL_SetWindowTitle(platform_->native_window(),
                         std::format("{} — {} | draws {} (+{} shadow) culled {} tris {} | CPU {:.2f}ms GPU {:.2f}ms | "
                                     "{:02.0f}h | WASD đi, Q/E xoay, cuộn zoom, T đổi giờ, F12 chụp",
                                     cfg_.window_title, renderer_->device_name(), st.draw_calls, st.shadow_draw_calls,
                                     st.culled, st.triangles, st.cpu_ms, st.gpu_ms, time_of_day_)
                             .c_str());
    }
  }

  void on_stats(const pb::FrameStats::Report& r) override {
    pb::Application::on_stats(r);
    if (renderer_) {
      const auto& st = renderer_->stats();
      pb::log::info("render", "draws={} shadow_draws={} culled={} tris={} cpu={:.2f}ms gpu={:.2f}ms val_err={} val_warn={}",
                    st.draw_calls, st.shadow_draw_calls, st.culled, st.triangles, st.cpu_ms, st.gpu_ms,
                    st.validation_errors, st.validation_warnings);
    }
  }

  void on_shutdown() override {
    if (renderer_) {
      renderer_->wait_idle();
      val_errors_ = renderer_->stats().validation_errors;
      pb::log::info("client", "validation: {} lỗi, {} cảnh báo", renderer_->stats().validation_errors,
                    renderer_->stats().validation_warnings);
    }
    if (session_) pb::log::info("game", "kết thúc: {} | {}", session_->player().debug_text(), session_->summary());
    if (net_) pb::log::info("net", "kết thúc: {}", net_->status());
    net_.reset();
    session_.reset();
    world_.reset();
    renderer_.reset();  // renderer phải hủy trước cửa sổ
    platform_.reset();
    pb::log::info("client", "thoát sạch sau {} frame", frame_count());
  }

 private:
  // Ánh sáng từ WorldSim (Phase 9): giờ + thời tiết.
  static void apply_lighting(pb::render::SceneParams& sp, const pb::sim::WorldSim& ws) {
    const auto l = pb::sim::lighting_for(ws.clock().hour(), ws.effects());
    sp.sun_dir = l.sun_dir;
    sp.sun_color = l.sun_color;
    sp.sun_intensity = l.sun_intensity;
    sp.sky_color = l.sky;
    sp.ambient_intensity = l.ambient;
    sp.clear_color = glm::vec4(l.sky, 1.0f);
    sp.fog_color = l.sky;
    sp.fog_density = l.fog_density;
    for (auto& p : sp.point_lights) p.intensity *= l.street_lights;
  }
  // Ánh sáng theo giờ cho test scene Phase 3 (không có WorldSim).
  static void apply_time_of_day(pb::render::SceneParams& sp, float hour) {
    const float t = (hour - 6.0f) / 12.0f * 3.14159265f;  // 6h mọc, 18h lặn
    const float elev = std::sin(t);
    const glm::vec3 sun = glm::normalize(glm::vec3(std::cos(t) * 0.8f, std::max(elev, 0.05f), 0.45f));
    const float day = std::clamp(elev * 3.0f, 0.0f, 1.0f);
    if (elev > 0.0f) {
      sp.sun_dir = sun;
      sp.sun_color = glm::mix(glm::vec3(1.0f, 0.55f, 0.3f), glm::vec3(1.0f, 0.96f, 0.9f), day);
      sp.sun_intensity = 3.2f * day + 0.4f;
    } else {  // trăng
      sp.sun_dir = glm::normalize(glm::vec3(-0.3f, 0.7f, 0.4f));
      sp.sun_color = glm::vec3(0.55f, 0.65f, 1.0f);
      sp.sun_intensity = 0.35f;
    }
    sp.sky_color = glm::mix(glm::vec3(0.08f, 0.1f, 0.2f), glm::vec3(0.55f, 0.65f, 0.8f), day);
    sp.ambient_intensity = 0.25f + 0.45f * day;
    sp.clear_color = glm::vec4(sp.sky_color, 1.0f);
    sp.fog_color = sp.sky_color;
    sp.fog_density = 0.0015f;
    for (auto& l : sp.point_lights) l.intensity *= (1.0f - day);  // đèn đường chỉ sáng khi tối
  }

  Args args_;
  pb::ClientConfig cfg_;
  std::unique_ptr<pb::platform::Platform> platform_;
  std::unique_ptr<pb::render::Renderer> renderer_;
  pb::geom::Camera25D camera_;
  pb::client::TestScene scene_;
  std::unique_ptr<pb::client::WorldView> world_;
  pb::sim::DogCatalog catalog_;
  pb::sim::InputBindings bindings_;
  std::unique_ptr<pb::sim::CollisionWorld> collision_;
  pb::sim::GameplayConfig gameplay_;
  pb::sim::NpcConfig npc_cfg_;
  pb::sim::WorldSimConfig world_cfg_;
  std::unique_ptr<pb::client::GameSession> session_;
  std::unique_ptr<pb::client::NetGame> net_;
  std::string world_hash_;
  std::vector<std::string> session_pressed_;
  pb::client::SurvivalScenario scenario_;
  int scenario_exit_ = 0;
  pb::render::Overlay overlay_;
  int width_ = 1280, height_ = 720;
  std::set<std::string> pressed_;
  std::unordered_map<std::string, int> scancodes_;
  bool debug_ = false;
  std::vector<pb::render::DrawItem> draw_items_;
  float aspect_ = 16.0f / 9.0f;
  bool minimized_ = false;
  bool shot_taken_ = false;
  float time_of_day_ = 15.0f;
  uint32_t val_errors_ = 0;

 public:
  void set_time(float h) { time_of_day_ = h; }
};
}  // namespace

int main(int argc, char** argv) {
  Args args = parse_args(argc, argv);
  if (!args.ok) {
    pb::log::error("client", "cách dùng: pawbound_client [--config f] [--frames N] [--hidden] [--validation|--no-validation]"
                             " [--no-render] [--gpu discrete|integrated|tên] [--screenshot file.png] [--time 0-23]");
    return 2;
  }
  auto cfg = pb::load_client_config(args.config_path);
  for (const auto& w : cfg.warnings) pb::log::warn("config", "{}", w);
  if (!cfg.ok()) {
    for (const auto& e : cfg.errors) pb::log::error("config", "{}", e);
    return 3;
  }
  pb::log::set_level(*pb::log::parse_level(cfg.config.log_level));
  if (!cfg.config.log_file.empty() && !pb::log::open_file(cfg.config.log_file))
    pb::log::warn("client", "không mở được file log {}", cfg.config.log_file);
  pb::log::info("client", "PAWBOUND client v{} khởi động", pb::kVersionString);

  const float tod = args.time_of_day;
  ClientApp app(std::move(args), cfg.config);
  app.set_time(tod);
  int code = app.run();
  // Smoke test: có lỗi validation -> exit code 4 để CTest bắt được.
  if (code == 0 && app.validation_errors() > 0) code = 4;
  pb::log::close_file();
  return code;
}
