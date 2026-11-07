// Phase 14: bắt chó / giải cứu / companion / kỹ năng / nhiệm vụ — kể cả các test exploit.
#include <doctest/doctest.h>

#include <glm/geometric.hpp>
#include <nlohmann/json.hpp>

#include "sim/advanced.hpp"

using namespace pb;
using namespace pb::sim;

namespace {
CaptureConfig fast_cfg() {
  CaptureConfig c;
  c.active_hours = {22};
  c.spawn_chance_per_min = 6000;  // chắc chắn spawn
  c.min_session_s = 0;
  c.spawn_min_dist_m = 20;
  c.spawn_max_dist_m = 21;
  c.warn_distance_m = 18;
  c.min_warning_s = 3;
  c.give_up_s = 30;
  c.escape_distance_m = 40;
  c.thief_speed = 5;
  c.hold_max_s = 60;
  c.rescue_channel_s = 2;
  c.immunity_s = 100;
  return c;
}
struct World {
  std::vector<CaptureStatus> st;
  std::vector<glm::vec2> pos;
  std::vector<CapturePlayer> ps;
  explicit World(size_t n) : st(n), pos(n, glm::vec2(100, 100)) {
    for (size_t i = 0; i < n; ++i) ps.push_back({static_cast<uint32_t>(i + 1), pos[i], true, true, false, 1000, false, &st[i], &pos[i]});
  }
  void sync() {
    for (size_t i = 0; i < ps.size(); ++i) ps[i].pos = pos[i];
  }
};
const auto any_zone = [](glm::vec2) { return true; };
bool has(const std::vector<CaptureEvent>& ev, CaptureEvent::Type t, uint32_t player) {
  for (const auto& e : ev)
    if (e.type == t && e.player == player) return true;
  return false;
}
}  // namespace

TEST_CASE("capture: canh bao truoc, co cua so phan ung, roi moi bi bat; khong mat do, bi dua toi khu giu") {
  CaptureSystem cs(fast_cfg(), {10, 10}, {200, 200}, 7);
  World w(1);
  double t = 0;
  int64_t unix0 = 1'000'000;
  bool warned = false, captured = false;
  double warn_t = 0, cap_t = 0;
  for (int i = 0; i < 600 && !captured; ++i, t += 1.0 / 30) {
    w.sync();
    const auto ev = cs.tick(w.ps, t, unix0 + static_cast<int64_t>(t), 22.5f, 1.0f / 30, any_zone);
    if (has(ev, CaptureEvent::Type::Warning, 1)) warned = true, warn_t = t;
    if (has(ev, CaptureEvent::Type::Captured, 1)) captured = true, cap_t = t;
  }
  REQUIRE(warned);
  REQUIRE(captured);
  CHECK(cap_t - warn_t >= 3.0 - 1e-6);  // ít nhất min_warning_s giây phản ứng
  CHECK(w.st[0].state == CaptureState::Captured);
  CHECK(w.pos[0] == glm::vec2(10, 10));
  CHECK(w.st[0].hold_until > unix0);
}

TEST_CASE("capture: ban ngay / khu an toan / nguoi moi / dang mien nhiem -> khong bi nham") {
  auto c = fast_cfg();
  c.min_session_s = 300;
  CaptureSystem cs(c, {10, 10}, {200, 200}, 3);
  World w(4);
  w.ps[0].session_s = 10;           // người mới vào phiên
  w.ps[1].in_safe_zone = true;      // khu an toàn
  w.st[2].immune_until = 2'000'000; // đang miễn nhiễm
  w.ps[3].session_s = 1000;
  for (int i = 0; i < 90; ++i) cs.tick(w.ps, i / 30.0, 1'000'000, 12.0f, 1.0f / 30, any_zone);  // ban ngày
  CHECK(cs.thieves().empty());
  for (int i = 0; i < 90; ++i) cs.tick(w.ps, 10 + i / 30.0, 1'000'000, 22.0f, 1.0f / 30, any_zone);
  REQUIRE(cs.thieves().size() == 1);
  CHECK(cs.thieves()[0].target == 4);
}

TEST_CASE("capture: chay xa / danh kẻ trom -> thoat va duoc mien nhiem") {
  CaptureSystem cs(fast_cfg(), {10, 10}, {200, 200}, 11);
  World w(1);
  double t = 0;
  std::vector<CaptureEvent> ev;
  while (w.st[0].state != CaptureState::Threatened && t < 10) {
    ev = cs.tick(w.ps, t, 1'000'000, 22.0f, 1.0f / 30, any_zone);
    t += 1.0 / 30;
  }
  REQUIRE(w.st[0].state == CaptureState::Threatened);
  const uint32_t thief = cs.thieves()[0].id;
  CHECK(cs.hit_thief(thief, 100));
  ev = cs.tick(w.ps, t, 1'000'000, 22.0f, 1.0f / 30, any_zone);
  CHECK(has(ev, CaptureEvent::Type::ThiefDefeated, 1));
  CHECK(has(ev, CaptureEvent::Type::Escaped, 1));
  CHECK(w.st[0].state == CaptureState::Normal);
  CHECK(w.st[0].immune_until == 1'000'000 + 100);
  // Không bị nhắm lại trong thời gian miễn nhiễm (chống kill loop)
  for (int i = 0; i < 300; ++i) cs.tick(w.ps, t + i / 30.0, 1'000'050, 22.0f, 1.0f / 30, any_zone);
  for (const auto& th : cs.thieves()) CHECK(th.fleeing);
}

namespace {
void capture_player(CaptureSystem& cs, World& w, size_t idx, double& t, int64_t unix_now) {
  for (int i = 0; i < 2000 && w.st[idx].state != CaptureState::Captured; ++i, t += 1.0 / 30) {
    w.sync();
    cs.tick(w.ps, t, unix_now, 22.0f, 1.0f / 30, any_zone);
  }
  REQUIRE(w.st[idx].state == CaptureState::Captured);
}
}  // namespace

TEST_CASE("rescue: dong doi den khu giu, bam tuong tac, giu du lau -> duoc cuu; roi di giua chung -> gian doan") {
  auto c = fast_cfg();
  c.max_thieves = 1;
  CaptureSystem cs(c, {10, 10}, {200, 200}, 5);
  World w(2);
  w.st[1].immune_until = 9'999'999;  // người cứu không bị nhắm
  w.pos[1] = {300, 300};
  double t = 0;
  capture_player(cs, w, 0, t, 1'000'000);
  // Người cứu tới nơi, bấm tương tác
  w.pos[1] = {12, 10};
  w.sync();
  w.ps[1].interact_pressed = true;
  auto ev = cs.tick(w.ps, t, 1'000'010, 22.0f, 1.0f / 30, any_zone);
  CHECK(has(ev, CaptureEvent::Type::RescueStarted, 1));
  w.ps[1].interact_pressed = false;
  // Rời đi giữa chừng -> gián đoạn
  w.pos[1] = {60, 60};
  w.sync();
  ev = cs.tick(w.ps, t, 1'000'011, 22.0f, 1.0f / 30, any_zone);
  CHECK(has(ev, CaptureEvent::Type::RescueInterrupted, 1));
  CHECK(w.st[0].state == CaptureState::Captured);
  // Quay lại, ở đủ 2 s
  w.pos[1] = {12, 10};
  w.sync();
  w.ps[1].interact_pressed = true;
  cs.tick(w.ps, t, 1'000'012, 22.0f, 1.0f / 30, any_zone);
  w.ps[1].interact_pressed = false;
  bool rescued = false;
  for (int i = 0; i < 90 && !rescued; ++i) rescued = has(cs.tick(w.ps, t, 1'000'012, 22.0f, 1.0f / 30, any_zone), CaptureEvent::Type::Rescued, 1);
  CHECK(rescued);
  CHECK(w.st[0].state == CaptureState::Normal);
}

TEST_CASE("exploit: nguoi cuu rot mang giua chung; hai nguoi cung cuu; relog khi bi bat; het han khi offline") {
  auto c = fast_cfg();
  c.max_thieves = 1;
  CaptureSystem cs(c, {10, 10}, {200, 200}, 9);
  World w(3);
  w.st[1].immune_until = w.st[2].immune_until = 9'999'999;
  w.pos[1] = w.pos[2] = {300, 300};
  double t = 0;
  capture_player(cs, w, 0, t, 1'000'000);
  const uint32_t cap_id = w.st[0].capture_id;
  // Hai người cùng bấm cứu một tick -> chỉ MỘT rescuer
  w.pos[1] = {12, 10};
  w.pos[2] = {8, 10};
  w.sync();
  w.ps[1].interact_pressed = w.ps[2].interact_pressed = true;
  auto ev = cs.tick(w.ps, t, 1'000'001, 22.0f, 1.0f / 30, any_zone);
  int started = 0;
  for (const auto& e : ev) started += e.type == CaptureEvent::Type::RescueStarted;
  CHECK(started == 1);
  w.ps[1].interact_pressed = w.ps[2].interact_pressed = false;
  // Rescuer rớt mạng -> gián đoạn, không được thả
  const uint32_t rescuer = w.st[0].rescuer;
  w.ps[rescuer - 1].online = false;
  ev = cs.tick(w.ps, t, 1'000'002, 22.0f, 1.0f / 30, any_zone);
  CHECK(has(ev, CaptureEvent::Type::RescueInterrupted, 1));
  CHECK(w.st[0].state == CaptureState::Captured);
  // Relog: lưu -> nạp lại ở chỗ khác -> vẫn bị giữ, bị đưa về khu giữ
  nlohmann::json saved;
  to_json(saved, w.st[0]);
  CaptureStatus reloaded;
  from_json_safe(saved, reloaded);
  glm::vec2 where{500, 500};
  CapturePlayer cp{1, where, true, true, false, 0, false, &reloaded, &where};
  cs.restore(cp, 1'000'010);
  CHECK(reloaded.state == CaptureState::Captured);
  CHECK(reloaded.capture_id == cap_id);
  CHECK(where == glm::vec2(10, 10));
  // Hết hạn trong lúc offline -> được thả ở chỗ an toàn khi vào lại (không bị khóa lâu)
  glm::vec2 where2{10, 10};
  CapturePlayer cp2{1, where2, true, true, false, 0, false, &reloaded, &where2};
  cs.restore(cp2, reloaded.hold_until + 1);
  CHECK(reloaded.state == CaptureState::Normal);
  CHECK(where2 == glm::vec2(200, 200));
  CHECK(reloaded.immune_until > reloaded.hold_until);
}

TEST_CASE("capture: thoi gian giu co tran, tu duoc tha") {
  CaptureSystem cs(fast_cfg(), {10, 10}, {200, 200}, 13);
  World w(1);
  double t = 0;
  capture_player(cs, w, 0, t, 1'000'000);
  const auto ev = cs.tick(w.ps, t, w.st[0].hold_until, 22.0f, 1.0f / 30, any_zone);
  CHECK(has(ev, CaptureEvent::Type::Released, 1));
  CHECK(w.pos[0] == glm::vec2(200, 200));
}

TEST_CASE("config: hold_max_s co tran cung; spawn phai ngoai tam canh bao") {
  ConfigDiagnostics d;
  parse_advanced_config(R"({"capture":{"hold_max_s":86400}})", d);
  CHECK_FALSE(d.ok());
  ConfigDiagnostics d2;
  parse_advanced_config(R"({"capture":{"spawn_min_dist_m":10,"warn_distance_m":18}})", d2);
  CHECK_FALSE(d2.ok());
  ConfigDiagnostics d3;
  const auto c = load_advanced_config(PB_SOURCE_DIR "/data/gameplay/advanced.json", d3);
  CHECK(d3.ok());
  CHECK(c.capture.hold_max_s <= 1200);  // mục tiêu 10–20 phút
  CHECK(c.quests.size() >= 3);
}

TEST_CASE("companion: di theo chu, canh bao ke trom, khong spam canh bao") {
  CompanionConfig c;
  Companion comp{1, {0, 0}};
  std::vector<Thief> th{{1, 1, {20, 0}, 40, 0, false}};
  auto a = tick_companion(comp, {5, 0}, c, 0, 0.1f, th, {});
  REQUIRE(a);
  CHECK(a->find("nguoi la") != std::string::npos);
  CHECK_FALSE(tick_companion(comp, {5, 0}, c, 1, 0.1f, th, {}));  // cooldown
  for (int i = 0; i < 100; ++i) tick_companion(comp, {5, 0}, c, 2, 0.1f, {}, {});
  CHECK(glm::distance(comp.pos, glm::vec2(5, 0)) <= c.follow_distance_m + 0.01f);
}

TEST_CASE("skills: level co tran, bonus toc do <= 5%, strength KHONG ap len nguoi choi; du lieu xau bi loc") {
  SkillConfig c;
  SkillSet s;
  s.add_xp(Skill::Speed, 1e9f, c);
  s.add_xp(Skill::Strength, 1e9f, c);
  CHECK(s.level(Skill::Speed, c) == 5);
  CHECK(speed_bonus(s, c) == doctest::Approx(1.05f));
  CHECK(strength_bonus(s, c, true) == 1.0f);
  CHECK(strength_bonus(s, c, false) > 1.0f);
  SkillSet bad;
  skills_from_json(nlohmann::json{{"speed", -50}, {"strength", 1e30}, {"scent", "x"}}, bad, c);
  CHECK(bad.level(Skill::Speed, c) == 0);
  CHECK(bad.level(Skill::Strength, c) == 5);
  // State hỏng kiểu dữ liệu không được làm sập server
  CaptureStatus cs;
  from_json_safe(nlohmann::json{{"state", "captured"}, {"holdUntil", "x"}, {"id", -1}}, cs);
  CHECK(cs.state == CaptureState::Normal);
  QuestBook qb;
  quests_from_json(nlohmann::json{{"q1", {{"p", "nhieu"}, {"c", 5}}}}, qb, {{"q1", "t", QuestObjective::Forage, 3, 10, 100, true}});
  CHECK(qb.q["q1"].progress == 0);
  CHECK_FALSE(qb.q["q1"].claimed);
}

TEST_CASE("quest: hoan thanh mot lan moi ky, qua han thi lam lai, khong nhan thuong hai lan") {
  std::vector<QuestDef> defs{{"q1", "t", QuestObjective::Forage, 3, 10, 100, true}};
  QuestBook b;
  const int64_t day = 20'000 * 86400LL;
  CHECK(b.on_event(defs, QuestObjective::Forage, 1, day + 10).empty());
  CHECK(b.on_event(defs, QuestObjective::Forage, 1, day + 500).empty());  // quá hạn 100 s: làm lại (tiến độ = 1)
  CHECK(b.on_event(defs, QuestObjective::Forage, 1, day + 510).empty());
  CHECK(b.on_event(defs, QuestObjective::Forage, 1, day + 520).size() == 1);
  CHECK(b.on_event(defs, QuestObjective::Forage, 5, day + 530).empty());  // cùng ngày: không hoàn thành lại
  // Lưu/nạp không làm mất trạng thái "đã xong hôm nay"
  QuestBook b2;
  quests_from_json(quests_to_json(b), b2, defs);
  CHECK(b2.on_event(defs, QuestObjective::Forage, 5, day + 600).empty());
  CHECK(b2.on_event(defs, QuestObjective::Forage, 5, day + 86400).size() == 1);  // ngày mới
}
