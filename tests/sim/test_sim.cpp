#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <set>

#include <glm/geometric.hpp>

#include "sim/anim_graph.hpp"
#include "sim/collision.hpp"
#include "sim/dog_archetype.hpp"
#include "sim/dog_motor.hpp"
#include "sim/input_mapping.hpp"
#include "world/world_types.hpp"

using namespace pb::sim;

namespace {
// Thế giới phẳng 100×100 m, không có vật cản (test tự thêm hình).
pb::world::WorldData flat_world() {
  pb::world::WorldData w;
  w.size_x = w.size_z = 10000;
  w.terrain.cells_x = w.terrain.cells_z = 25;
  w.terrain.cell = 400;
  w.terrain.height_cm.assign(26 * 26, 0);
  return w;
}
MovementStats stats() { return MovementStats{}; }
void run(MotorState& st, const MotorInput& in, const CollisionWorld& cw, float seconds) {
  for (int i = 0; i < static_cast<int>(seconds * 30); ++i) step_motor(st, in, stats(), cw, 1.0f / 30.0f);
}
}  // namespace

TEST_CASE("motor: tăng tốc tới tốc độ chạy, sprint nhanh hơn, dừng khi thả phím") {
  auto w = flat_world();
  CollisionWorld cw(w);
  MotorState st;
  st.pos = {50, 0, 50};
  MotorInput in;
  in.move = {0, 1};
  run(st, in, cw, 1.0f);
  CHECK(glm::length(st.vel) == doctest::Approx(stats().run_speed).epsilon(0.01));
  CHECK(st.gait == Gait::Run);
  CHECK(std::abs(st.yaw) < 0.01f);  // nhìn +Z
  in.sprint = true;
  run(st, in, cw, 1.0f);
  CHECK(glm::length(st.vel) == doctest::Approx(stats().sprint_speed).epsilon(0.01));
  CHECK(st.gait == Gait::Sprint);
  in = {};
  run(st, in, cw, 1.0f);
  CHECK(glm::length(st.vel) < 0.01f);
  CHECK(st.gait == Gait::Idle);
}

TEST_CASE("motor: gia tốc bị giới hạn (không đạt tốc độ tối đa ngay tick đầu)") {
  auto w = flat_world();
  CollisionWorld cw(w);
  MotorState st;
  st.pos = {50, 0, 50};
  MotorInput in;
  in.move = {1, 0};
  step_motor(st, in, stats(), cw, 1.0f / 30.0f);
  CHECK(glm::length(st.vel) == doctest::Approx(stats().accel / 30.0f).epsilon(0.01));
}

TEST_CASE("motor: walk/trot/run theo độ lớn cần analog và Ctrl") {
  const auto s = stats();
  MotorInput in;
  in.move = {0, 0.3f};
  CHECK(target_speed(in, s) == doctest::Approx(s.walk_speed));
  in.move = {0, 0.7f};
  CHECK(target_speed(in, s) == doctest::Approx(s.trot_speed));
  in.move = {0, 1.0f};
  CHECK(target_speed(in, s) == doctest::Approx(s.run_speed));
  in.walk = true;
  CHECK(target_speed(in, s) == doctest::Approx(s.walk_speed));
  in = {};
  in.move = {0, 0.05f};
  CHECK(target_speed(in, s) == 0.0f);
  CHECK(gait_for_speed(0.0f, s) == Gait::Idle);
  CHECK(gait_for_speed(s.walk_speed, s) == Gait::Walk);
  CHECK(gait_for_speed(s.trot_speed, s) == Gait::Trot);
}

TEST_CASE("va chạm: không xuyên tường khi chạy nước rút, trượt dọc tường") {
  auto w = flat_world();
  CollisionWorld cw(w);
  cw.add_box({{40, 55}, {60, 56}, 10.0f});  // tường mỏng 1 m chắn phía trước
  MotorState st;
  st.pos = {50, 0, 50};
  MotorInput in;
  in.move = {0, 1};
  in.sprint = true;
  run(st, in, cw, 3.0f);
  CHECK(st.pos.z < 55.0f - stats().radius + 0.01f);
  CHECK(st.blocked);
  // Đi chéo: trượt dọc tường (x tăng) chứ không đứng yên
  const float x0 = st.pos.x;
  in.move = glm::normalize(glm::vec2(1, 1));
  run(st, in, cw, 1.0f);
  CHECK(st.pos.x > x0 + 3.0f);
  CHECK(st.pos.z < 55.0f);
}

TEST_CASE("va chạm: hồ không đi qua được; biên thế giới chặn") {
  auto w = flat_world();
  CollisionWorld cw(w);
  cw.add_circle({{50, 60}, 5.0f, 1e30f});
  MotorState st;
  st.pos = {50, 0, 50};
  MotorInput in;
  in.move = {0, 1};
  run(st, in, cw, 4.0f);
  CHECK(glm::distance(glm::vec2(st.pos.x, st.pos.z), glm::vec2(50, 60)) >= 5.0f + stats().radius - 0.01f);
  st.pos = {2, 0, 2};
  in.move = {-1, -1};
  run(st, in, cw, 2.0f);
  CHECK(st.pos.x >= stats().radius - 1e-3f);
  CHECK(st.pos.z >= stats().radius - 1e-3f);
}

TEST_CASE("nhảy: lên rồi rơi về mặt đất; nhảy qua vật thấp, không qua vật cao") {
  auto w = flat_world();
  CollisionWorld cw(w);
  MotorState st;
  st.pos = {50, 0, 50};
  MotorInput in;
  in.jump = true;
  step_motor(st, in, stats(), cw, 1.0f / 30.0f);
  CHECK_FALSE(st.grounded);
  CHECK(st.gait == Gait::Air);
  in.jump = false;
  float peak = 0;
  for (int i = 0; i < 60; ++i) {
    step_motor(st, in, stats(), cw, 1.0f / 30.0f);
    peak = std::max(peak, st.pos.y);
  }
  CHECK(st.grounded);
  CHECK(st.pos.y == doctest::Approx(0.0f));
  CHECK(peak == doctest::Approx(0.625f).epsilon(0.1));  // v²/2g = 25/40

  // Vật thấp 0,5 m (ghế): bị chặn khi đi bộ, nhảy qua được
  CollisionWorld cw2(w);
  cw2.add_box({{40, 53}, {60, 54}, 0.5f});
  MotorState a;
  a.pos = {50, 0, 50};
  MotorInput go;
  go.move = {0, 1};
  run(a, go, cw2, 2.0f);
  CHECK(a.pos.z < 53.0f);
  // Chạy lấy đà rồi nhảy khi cách vật cản ~1 m
  auto run_and_jump = [&](const CollisionWorld& world) {
    MotorState m;
    m.pos = {50, 0, 44};
    MotorInput mi;
    mi.move = {0, 1};
    bool jumped = false;
    for (int i = 0; i < 90; ++i) {
      mi.jump = !jumped && m.pos.z >= 51.8f;
      jumped |= mi.jump;
      step_motor(m, mi, stats(), world, 1.0f / 30.0f);
    }
    return m;
  };
  CHECK(run_and_jump(cw2).pos.z > 54.0f);
  // Tường cao 3 m: nhảy cũng không qua
  CollisionWorld cw3(w);
  cw3.add_box({{40, 53}, {60, 54}, 3.0f});
  CHECK(run_and_jump(cw3).pos.z < 53.0f);
}

TEST_CASE("dốc: không leo được dốc quá đứng") {
  auto w = flat_world();
  // Vách đất cao 5 m ở x >= 60 (dốc > 40°)
  for (int z = 0; z <= 25; ++z)
    for (int x = 15; x <= 25; ++x) w.terrain.height_cm[static_cast<size_t>(z * 26 + x)] = 500;
  CollisionWorld cw(w);
  MotorState st;
  st.pos = {50, 0, 50};
  MotorInput in;
  in.move = {1, 0};
  run(st, in, cw, 3.0f);
  CHECK(st.pos.y < 1.5f);
}

TEST_CASE("breed: chỉ số giới hạn, ngoại hình không ảnh hưởng chỉ số; file thật hợp lệ") {
  pb::ConfigDiagnostics d;
  auto cat = load_dog_catalog(std::filesystem::path(PB_SOURCE_DIR) / "data/dogs/breeds.json", d);
  for (const auto& e : d.errors) MESSAGE(e);
  REQUIRE(d.ok());
  REQUIRE(cat.breeds.size() >= 2);
  float lo = 1e9f, hi = 0;
  for (const auto& b : cat.breeds) {
    const auto m = movement_for(cat, b);
    lo = std::min(lo, m.sprint_speed);
    hi = std::max(hi, m.sprint_speed);
  }
  CHECK(hi / lo <= 1.05f / 0.95f + 1e-4f);  // chênh lệch tối đa ~10.5%

  pb::ConfigDiagnostics d2;
  parse_dog_catalog(R"({"models":{"m":{"asset_id":"x","scale":1,"walk_clip_speed":1,"run_clip_speed":5,
      "clips":{"idle":"i","walk":"w","run":"r"}}}, "breeds":[{"id":"cheat","display_name":"x","model":"m",
      "speed_mod":2.0,"size":1.0}]})",
                    d2);
  CHECK_FALSE(d2.ok());  // speed_mod 2.0 bị từ chối
  pb::ConfigDiagnostics d3;
  parse_dog_catalog(R"({"models":{}, "breeds":[{"id":"a","display_name":"x","model":"khong_co","speed_mod":1,"size":1}]})", d3);
  CHECK_FALSE(d3.ok());
}

TEST_CASE("anim graph: chọn clip theo gait, crossfade, tốc độ phát theo tốc độ chạy") {
  ModelBinding mb;
  mb.walk_clip_speed = 1.5f;
  mb.run_clip_speed = 6.0f;
  AnimGraph g(0.2f);
  g.update(Gait::Idle, 0, mb, 0.1f);
  CHECK(g.current().clip == "idle");
  g.update(Gait::Walk, 1.5f, mb, 0.05f);
  CHECK(g.current().clip == "walk");
  CHECK(g.previous().clip == "idle");
  CHECK(g.blend() < 1.0f);
  CHECK(g.current().rate == doctest::Approx(1.0f));
  g.update(Gait::Trot, 3.0f, mb, 0.3f);  // trot dùng clip walk, phát nhanh hơn
  CHECK(g.current().clip == "walk");
  CHECK(g.current().rate == doctest::Approx(2.0f));
  CHECK(g.blend() == doctest::Approx(1.0f));
  g.update(Gait::Sprint, 9.0f, mb, 0.1f);
  CHECK(g.current().clip == "run");
  CHECK(g.current().rate == doctest::Approx(1.5f));
}

TEST_CASE("input: bàn phím, phím vừa nhấn, gamepad deadzone, bindings từ file") {
  auto b = InputBindings::defaults();
  std::set<std::string> held{"W", "D", "Left Shift"};
  std::set<std::string> pressed{"Space"};
  GamepadState pad;
  auto s = map_input(
      b, [&](const std::string& k) { return held.contains(k); }, [&](const std::string& k) { return pressed.contains(k); }, pad);
  CHECK(s.move.y == doctest::Approx(0.7071f).epsilon(1e-3));
  CHECK(s.move.x == doctest::Approx(0.7071f).epsilon(1e-3));
  CHECK(s.sprint);
  CHECK(s.jump);
  CHECK_FALSE(s.attack);
  // Gamepad: trong deadzone -> 0
  held.clear();
  pressed.clear();
  pad.connected = true;
  pad.lx = 0.1f;
  auto none = [](const std::string&) { return false; };
  s = map_input(b, none, none, pad);
  CHECK(glm::length(s.move) == 0.0f);
  pad.lx = 1.0f;
  pad.rt = 1.0f;
  s = map_input(b, none, none, pad);
  CHECK(s.move.x == doctest::Approx(1.0f));
  CHECK(s.sprint);
  pb::ConfigDiagnostics d;
  auto fb = load_bindings(std::filesystem::path(PB_SOURCE_DIR) / "config/input.json", d);
  CHECK(d.ok());
  CHECK(d.warnings.empty());
  CHECK(fb.keys[Action::Jump] == std::vector<std::string>{"Space"});
}
