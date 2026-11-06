#include <doctest/doctest.h>

#include <filesystem>
#include <set>

#include <glm/geometric.hpp>

#include "sim/npc.hpp"
#include "sim/world_sim.hpp"
#include "world/generator.hpp"
#include "world/world_config.hpp"

using namespace pb::sim;
namespace fs = std::filesystem;

namespace {
struct Env {
  pb::world::WorldData world;
  GameplayConfig gc;
  WorldSimConfig wc;
  std::vector<ResourceNode> res;
  Env() {
    pb::ConfigDiagnostics d;
    world = pb::world::generate_world(pb::world::load_world_config(fs::path(PB_SOURCE_DIR) / "data/world/demo.json", d));
    gc = load_gameplay_config(fs::path(PB_SOURCE_DIR) / "data/gameplay/gameplay.json", d);
    wc = load_world_sim_config(fs::path(PB_SOURCE_DIR) / "data/gameplay/world_sim.json", d);
    for (const auto& e : d.errors) MESSAGE(e);
    REQUIRE(d.ok());
    res = make_resource_nodes(world, gc);
  }
  glm::vec3 center() const { return {256, 0, 256}; }
};
Env& env() {
  static Env e;
  return e;
}
}  // namespace

TEST_CASE("worldsim: đồng hồ — tỉ lệ thời gian, qua nửa đêm tăng ngày, đặt giờ") {
  WorldClock c(2400, 23.5);
  c.advance(50);  // 50 s thật = 0,5 giờ game
  CHECK(c.hour() == doctest::Approx(0.0).epsilon(1e-9));
  CHECK(c.day() == 1);
  c.advance(2400 * 3 + 100);
  CHECK(c.day() == 4);
  CHECK(c.hour() == doctest::Approx(1.0));
  c.set_hour(-1);
  CHECK(c.hour() == doctest::Approx(23));
  c.set_hour(49);
  CHECK(c.hour() == doctest::Approx(1));
  c.advance(-5);  // dt âm bị bỏ qua
  CHECK(c.hour() == doctest::Approx(1));
}

TEST_CASE("worldsim: ánh sáng ngày/đêm và ảnh hưởng thời tiết") {
  WeatherEffects clear;
  const auto noon = lighting_for(12, clear);
  const auto midnight = lighting_for(0, clear);
  CHECK_FALSE(noon.night);
  CHECK(midnight.night);
  CHECK(noon.sun_dir.y > 0.9f);
  CHECK(noon.sun_intensity > 3.0f);
  CHECK(midnight.sun_intensity < 0.5f);
  CHECK(noon.street_lights < 0.1f);
  CHECK(midnight.street_lights > 0.9f);
  WeatherEffects storm;
  storm.cloud = 1;
  storm.storm = 1;
  storm.rain = 1;
  const auto stormy = lighting_for(12, storm);
  CHECK(stormy.sun_intensity < noon.sun_intensity * 0.5f);
  WeatherEffects fog;
  fog.fog = 1;
  CHECK(lighting_for(12, fog).fog_density > noon.fog_density * 10);
}

TEST_CASE("worldsim: thời tiết chuyển dần (không nhảy đột ngột) và deterministic theo seed") {
  auto& e = env();
  WorldSim a(e.wc, e.world, 42), b(e.wc, e.world, 42), c(e.wc, e.world, 43);
  auto ra = e.res, rb = e.res, rc = e.res;
  const glm::vec3 p = e.center();
  std::vector<std::string> seq_a, seq_b, seq_c;
  float max_step = 0;
  WeatherEffects prev = a.effects();
  const double dt = 1.0 / 30;
  for (int i = 0; i < 30 * 60 * 60; ++i) {  // 1 giờ thật
    if (a.tick(dt, std::span(&p, 1), ra).weather_changed) seq_a.emplace_back(to_string(a.weather()));
    if (b.tick(dt, std::span(&p, 1), rb).weather_changed) seq_b.emplace_back(to_string(b.weather()));
    if (c.tick(dt, std::span(&p, 1), rc).weather_changed) seq_c.emplace_back(to_string(c.weather()));
    const auto cur = a.effects();
    max_step = std::max({max_step, std::abs(cur.visibility - prev.visibility), std::abs(cur.rain - prev.rain),
                         std::abs(cur.fog - prev.fog), std::abs(cur.cloud - prev.cloud)});
    prev = cur;
  }
  CHECK(seq_a.size() >= 3);
  CHECK(seq_a == seq_b);
  CHECK(seq_a != seq_c);
  // transition 90 s: mỗi tick 1/30 s chỉ đổi tối đa ~1/2700 * biên độ (≤ 1)
  CHECK(max_step < 0.001f);
}

TEST_CASE("worldsim: lịch — người đi bộ thưa về đêm, thức ăn hồi nhanh giờ ăn") {
  auto& e = env();
  WorldSim s(e.wc, e.world, 1);
  s.force_weather(Weather::Clear, true);
  s.clock().set_hour(12.0);
  const float day = s.pedestrian_density();
  CHECK(s.food_regen_mult() == doctest::Approx(3.0f));
  s.clock().set_hour(3.0);
  const float night = s.pedestrian_density();
  CHECK(night < day * 0.2f);
  CHECK(s.food_regen_mult() == doctest::Approx(1.0f));
  s.force_weather(Weather::Storm, true);
  s.clock().set_hour(12.0);
  CHECK(s.pedestrian_density() < day * 0.5f);
  // Hồi thức ăn: giờ ăn x3
  auto r1 = e.res, r2 = e.res;
  ResourceNode* f1 = nullptr;
  for (auto& n : r1)
    if (n.kind == pb::world::PoiKind::Food) {
      n.remaining = 0;
      f1 = &n;
      break;
    }
  for (auto& n : r2)
    if (n.id == f1->id) n.remaining = 0;
  WorldSim meal(e.wc, e.world, 1), off(e.wc, e.world, 1);
  meal.clock().set_hour(12.0);
  off.clock().set_hour(15.0);
  const glm::vec3 far(5000, 0, 5000);
  for (int i = 0; i < 30 * 90; ++i) {  // 90 s
    meal.tick(1.0 / 30, std::span(&far, 0), r1);
    off.tick(1.0 / 30, std::span(&far, 0), r2);
  }
  int m = 0, o = 0;
  for (const auto& n : r1)
    if (n.id == f1->id) m = n.remaining;
  for (const auto& n : r2)
    if (n.id == f1->id) o = n.remaining;
  CHECK(m == 1);  // 90 s × 3 = 270 s > 240 s
  CHECK(o == 0);
}

TEST_CASE("worldsim: NPC — mật độ người đi bộ thay đổi theo giờ, cùng ngưỡng thì cùng người") {
  auto& e = env();
  pb::ConfigDiagnostics d;
  auto nc = load_npc_config(fs::path(PB_SOURCE_DIR) / "data/gameplay/npcs.json", d);
  CollisionWorld col(e.world);
  NavGraph nav(e.world);
  NpcManager mgr(e.world, col, nav, e.gc, nc, nullptr);
  SurvivalState sv;
  CombatState cb;
  PlayerView pv{1, {388, 0, 165}, 0, &sv, &cb};
  auto humans = [&] {
    std::set<uint32_t> ids;
    for (const auto& n : mgr.npcs())
      if (n.kind == NpcKind::Human) ids.insert(n.id);
    return ids;
  };
  mgr.set_human_density(1.0f);
  mgr.update(1.0f / 30, std::span(&pv, 1), {});
  const auto full = humans();
  mgr.set_human_density(0.1f);
  for (int i = 0; i < 31; ++i) mgr.update(1.0f / 30, std::span(&pv, 1), {});
  const auto night = humans();
  CHECK(night.size() < full.size() / 2);
  for (uint32_t id : night) CHECK(full.contains(id));  // người còn lại là tập con
  mgr.set_human_density(1.0f);
  for (int i = 0; i < 31; ++i) mgr.update(1.0f / 30, std::span(&pv, 1), {});
  CHECK(humans() == full);  // sáng ra: đúng những người cũ quay lại
}

TEST_CASE("worldsim: sự kiện — xuất hiện, hợp lệ vị trí, kết thúc đúng hạn, cooldown và chống lặp") {
  auto& e = env();
  WorldSim s(e.wc, e.world, 7);
  s.clock().set_hour(10.0);
  auto res = e.res;
  const size_t base_nodes = res.size();
  const glm::vec3 p(388, 0, 165);
  std::vector<ActiveEvent> started, ended;
  std::vector<int> order;
  for (int i = 0; i < 30 * 60 * 20; ++i) {  // 20 phút
    auto ch = s.tick(1.0 / 30, std::span(&p, 1), res);
    for (auto& x : ch.started) {
      started.push_back(x);
      order.push_back(static_cast<int>(x.type));
    }
    for (auto& x : ch.ended) ended.push_back(x);
    CHECK(static_cast<int>(s.events().size()) <= e.wc.max_events);
  }
  REQUIRE(started.size() >= 3);
  for (size_t i = 1; i < order.size(); ++i) CHECK(order[i] != order[i - 1]);  // không lặp cùng loại liên tiếp
  for (const auto& x : started) {
    const float d = glm::distance(x.pos, glm::vec2(p.x, p.z));
    CHECK(d >= e.wc.event_min_m * 0.5f);
    CHECK(d <= e.wc.event_max_m + 30.0f);
    CHECK_FALSE(e.world.in_pond(static_cast<pb::world::cm>(x.pos.x * 100), static_cast<pb::world::cm>(x.pos.y * 100), 100));
  }
  for (const auto& x : ended) CHECK(x.ends - x.started == doctest::Approx(e.wc.events[static_cast<size_t>(x.type)].duration_s));
  // Cooldown: cùng loại không bắt đầu lại trước khi hết cooldown
  for (size_t i = 0; i < started.size(); ++i)
    for (size_t j = i + 1; j < started.size(); ++j)
      if (started[i].type == started[j].type)
        CHECK(started[j].started - started[i].started >= e.wc.events[static_cast<size_t>(started[i].type)].cooldown_s - 1e-6);
  // Node thức ăn tạm của food_hotspot được gỡ khi sự kiện kết thúc
  size_t extra = 0;
  for (const auto& r : res) extra += is_event_node(r.id) ? 1 : 0;
  size_t active_hotspots = 0;
  for (const auto& ev : s.events()) active_hotspots += ev.type == EventType::FoodHotspot;
  CHECK(extra == active_hotspots);
  CHECK(res.size() == base_nodes + active_hotspots);
}

TEST_CASE("worldsim: ép sự kiện để test + hoàn thành sự kiện kết thúc sớm") {
  auto& e = env();
  WorldSim s(e.wc, e.world, 3);
  auto res = e.res;
  auto ev = s.force_event(EventType::FoodHotspot, {300, 200}, res);
  REQUIRE(ev);
  CHECK(res.size() == e.res.size() + 1);
  CHECK(s.complete_event(ev->id));
  CHECK_FALSE(s.complete_event(ev->id));
  const glm::vec3 p(300, 0, 200);
  auto ch = s.tick(1.0 / 30, std::span(&p, 1), res);
  REQUIRE(ch.ended.size() == 1);
  CHECK(ch.ended[0].completed);
  CHECK(res.size() == e.res.size());
}

TEST_CASE("worldsim: lưu/nạp trạng thái — tiếp tục y hệt sau khi nạp; JSON hỏng bị từ chối") {
  auto& e = env();
  WorldSim a(e.wc, e.world, 99);
  auto ra = e.res;
  const glm::vec3 p(388, 0, 165);
  for (int i = 0; i < 30 * 300; ++i) a.tick(1.0 / 30, std::span(&p, 1), ra);
  ra[0].remaining = 0;
  const std::string saved = WorldSim::to_json(a.snapshot(ra));
  std::string err;
  auto snap = WorldSim::from_json(saved, err);
  REQUIRE_MESSAGE(snap, err);
  WorldSim b(e.wc, e.world, 12345);  // seed khác: trạng thái lấy từ file
  auto rb = e.res;
  b.restore(*snap, rb);
  CHECK(b.clock().hour() == doctest::Approx(a.clock().hour()));
  CHECK(b.weather() == a.weather());
  CHECK(rb[0].remaining == 0);
  CHECK(rb.size() == ra.size());
  for (int i = 0; i < 30 * 600; ++i) {
    a.tick(1.0 / 30, std::span(&p, 1), ra);
    b.tick(1.0 / 30, std::span(&p, 1), rb);
  }
  CHECK(a.weather() == b.weather());
  CHECK(a.events().size() == b.events().size());
  CHECK(a.clock().hour() == doctest::Approx(b.clock().hour()));
  CHECK_FALSE(WorldSim::from_json("{}", err));
  CHECK_FALSE(WorldSim::from_json(R"({"version":1,"day":0})", err));
  CHECK_FALSE(WorldSim::from_json("không phải json", err));
}

TEST_CASE("worldsim: logic không phụ thuộc kích thước bước (30 Hz vs 10 Hz)") {
  auto& e = env();
  WorldSim a(e.wc, e.world, 5), b(e.wc, e.world, 5);
  auto ra = e.res, rb = e.res;
  const glm::vec3 p(388, 0, 165);
  for (int i = 0; i < 30 * 600; ++i) a.tick(1.0 / 30, std::span(&p, 1), ra);
  for (int i = 0; i < 10 * 600; ++i) b.tick(1.0 / 10, std::span(&p, 1), rb);
  CHECK(a.clock().hour() == doctest::Approx(b.clock().hour()).epsilon(1e-6));
  CHECK(a.weather() == b.weather());
}
