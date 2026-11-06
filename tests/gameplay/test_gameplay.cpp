#include <doctest/doctest.h>

#include <filesystem>

#include "sim/gameplay.hpp"
#include "world/generator.hpp"
#include "world/world_config.hpp"

using namespace pb::sim;
using pb::world::ZoneRule;

namespace {
const GameplayConfig& cfg() {
  static const GameplayConfig c = [] {
    pb::ConfigDiagnostics d;
    auto g = load_gameplay_config(std::filesystem::path(PB_SOURCE_DIR) / "data/gameplay/gameplay.json", d);
    for (const auto& e : d.errors) MESSAGE(e);
    REQUIRE(d.ok());
    return g;
  }();
  return c;
}
void sim(SurvivalState& s, const Activity& a, float seconds, float dt = 1.0f / 30.0f) {
  for (float t = 0; t < seconds; t += dt) tick_survival(s, cfg().survival, a, dt);
}
}  // namespace

TEST_CASE("survival: đói/khát giảm theo thời gian mô phỏng, độc lập kích thước bước") {
  SurvivalState a, b;
  sim(a, {}, 600.0f, 1.0f / 30.0f);
  sim(b, {}, 600.0f, 0.5f);
  CHECK(a.hunger == doctest::Approx(100.0f - 100.0f / 4800.0f * 600.0f).epsilon(0.01));
  CHECK(a.thirst == doctest::Approx(100.0f - 100.0f / 3600.0f * 600.0f).epsilon(0.01));
  CHECK(a.hunger == doctest::Approx(b.hunger).epsilon(0.005));  // 30 Hz vs 2 Hz cho cùng kết quả
  SurvivalState run;
  sim(run, {true, true, false, false}, 600.0f);
  CHECK(run.hunger < a.hunger);  // vận động mạnh đói nhanh hơn
}

TEST_CASE("survival: đói/khát cạn thì mất máu; no đủ thì hồi máu; HP 0 -> gục rồi chết, không xóa") {
  SurvivalState s;
  s.hp = 50;
  sim(s, {}, 10.0f);
  CHECK(s.hp > 50);
  s.hunger = 0;
  s.thirst = 0;
  const float before = s.hp;
  sim(s, {}, 60.0f);
  CHECK(s.hp == doctest::Approx(before - 60.0f * (1.0f / 30 + 1.0f / 20)).epsilon(0.02));
  s.hp = 1;
  sim(s, {}, 20.0f);
  CHECK(s.life == LifeState::Downed);
  sim(s, {}, 20.0f);
  CHECK(s.life == LifeState::Dead);  // chờ respawn — dữ liệu vẫn còn
}

TEST_CASE("survival: nước rút tốn thể lực, nghỉ hồi sau độ trễ, khát làm giảm thể lực tối đa, mệt làm chậm") {
  SurvivalState s;
  sim(s, {true, true, false, false}, 5.0f);
  CHECK(s.stamina == doctest::Approx(100 - 12 * 5).epsilon(0.02));
  CHECK(can_sprint(s, cfg().survival, true));
  s.stamina = 10;
  CHECK_FALSE(can_sprint(s, cfg().survival, false));  // không đủ để BẮT ĐẦU
  CHECK(can_sprint(s, cfg().survival, true));          // nhưng đang chạy thì tiếp tục
  sim(s, {}, 0.5f);
  CHECK(s.stamina == doctest::Approx(10));  // còn trong độ trễ hồi
  sim(s, {}, 2.0f);
  CHECK(s.stamina > 10);
  s.thirst = 10;
  s.stamina = 100;
  sim(s, {}, 0.1f);
  CHECK(s.stamina <= 70.01f);
  s.rest = 10;
  CHECK(speed_multiplier(s, cfg().survival) == doctest::Approx(0.8f));
}

TEST_CASE("nghỉ ngơi: hồi theo luật, nơi trú ẩn hồi nhanh gấp đôi, di chuyển làm gián đoạn") {
  SurvivalState a, b;
  a.rest = b.rest = 20;
  sim(a, {false, false, true, false}, 10.0f);
  sim(b, {false, false, true, true}, 10.0f);
  CHECK(a.rest == doctest::Approx(20 + 12).epsilon(0.02));
  CHECK(b.rest == doctest::Approx(20 + 24).epsilon(0.02));
  SurvivalState c;
  c.rest = 20;
  sim(c, {true, false, true, false}, 10.0f);  // đang đi -> không tính là nghỉ
  CHECK(c.rest < 20);
  CHECK_FALSE(c.resting);
}

TEST_CASE("tài nguyên: dùng trong tầm, cạn rồi hồi theo thời gian; khoảng cách do server kiểm tra") {
  ResourceNode n{1, pb::world::PoiKind::Water, {10, 10}, 2, 2, 30, 60, 0};
  SurvivalState s;
  s.thirst = 20;
  CHECK(consume_at(n, {30, 30}, s, cfg()) == InteractResult::OutOfRange);
  CHECK(consume_at(n, {10.5f, 10}, s, cfg()) == InteractResult::Ok);
  CHECK(s.thirst == doctest::Approx(50));
  CHECK(consume_at(n, {10.5f, 10}, s, cfg()) == InteractResult::Ok);
  s.thirst = 0;
  CHECK(consume_at(n, {10.5f, 10}, s, cfg()) == InteractResult::Depleted);
  std::vector<ResourceNode> v{n};
  tick_resources(v, 59.0f);
  CHECK(v[0].remaining == 0);
  tick_resources(v, 2.0f);
  CHECK(v[0].remaining == 1);
  tick_resources(v, 1000.0f);
  CHECK(v[0].remaining == 2);  // không vượt capacity
  s.thirst = 100;
  CHECK(consume_at(v[0], {10, 10}, s, cfg()) == InteractResult::Full);  // không phí tài nguyên khi đã đủ
  CHECK(v[0].remaining == 2);
}

TEST_CASE("inventory: giới hạn stack/ô, không nhân bản qua thao tác lặp, serialize + validate") {
  Inventory inv(cfg().inventory_slots);
  CHECK(inv.add(cfg(), "scrap_food", 5) == 0);  // 3 + 2 (max_stack 3)
  CHECK(inv.count_of("scrap_food") == 5);
  CHECK(inv.add(cfg(), "khong_ton_tai", 1) == 1);
  CHECK(inv.add(cfg(), "meat_bone", 100) == 92);  // còn 4 ô x 2 = 8
  CHECK(inv.add(cfg(), "water_bottle", 1) == 1);   // túi đầy
  CHECK(inv.validate(cfg()).empty());
  // Thao tác lặp cùng id (gói tin gửi lại) chỉ áp dụng một lần
  int added = 0;
  for (int i = 0; i < 5; ++i)
    if (inv.apply_once(42)) added++;
  CHECK(added == 1);
  CHECK_FALSE(inv.remove(0, 99));
  CHECK(inv.remove(0, 3));
  CHECK(inv.slots()[0].empty());
  // Serialize vòng
  std::string err;
  auto back = Inventory::from_json(cfg(), inv.to_json(), err);
  REQUIRE(back);
  CHECK(back->count_of("meat_bone") == 8);
  // Dữ liệu giả mạo bị từ chối
  CHECK_FALSE(Inventory::from_json(cfg(), R"({"version":1,"slots":[{"item":"scrap_food","count":999},null,null,null,null,null]})", err));
  CHECK_FALSE(Inventory::from_json(cfg(), R"({"version":1,"slots":[{"item":"gold","count":1},null,null,null,null,null]})", err));
  CHECK_FALSE(Inventory::from_json(cfg(), R"({"version":1,"slots":[{"item":"scrap_food","count":-1},null,null,null,null,null]})", err));
  CHECK_FALSE(Inventory::from_json(cfg(), R"({"version":1,"slots":[null]})", err));
}

TEST_CASE("dùng item: ăn từ túi; nhặt đồ ăn từ nguồn") {
  Inventory inv(cfg().inventory_slots);
  ResourceNode food{2, pb::world::PoiKind::Food, {0, 0}, 3, 3, 35, 240, 0};
  CHECK(forage_at(food, {1, 0}, inv, cfg()) == InteractResult::Ok);
  CHECK(food.remaining == 2);
  SurvivalState s;
  s.hunger = 10;
  CHECK(use_item(inv, inv.find("scrap_food"), s, cfg()) == InteractResult::Ok);
  CHECK(s.hunger == doctest::Approx(35));
  CHECK(inv.count_of("scrap_food") == 0);
  CHECK(use_item(inv, 0, s, cfg()) == InteractResult::NoTarget);
  inv.add(cfg(), "old_collar", 1);
  CHECK(use_item(inv, inv.find("old_collar"), s, cfg()) == InteractResult::UnknownItem);  // cosmetic không ăn được
}

TEST_CASE("chiến đấu: tầm, hướng, hồi chiêu, thể lực, né, choáng, sát thương deterministic") {
  SurvivalState as, ts;
  CombatState ac, tc;
  Combatant a{{0, 0, 0}, 0.0f, false, &as, &ac};  // nhìn +Z
  Combatant t{{0, 0, 1.0f}, 3.14159f, false, &ts, &tc};
  auto bite = [&](float now) { return request_bite(a, t, ZoneRule::Contested, cfg(), now); };
  auto r = bite(0);
  CHECK(r.result == BiteResult::Hit);
  CHECK(r.damage == doctest::Approx(12));
  CHECK(ts.hp == doctest::Approx(88));
  CHECK(as.stamina == doctest::Approx(90));
  CHECK(bite(0.1f).result == BiteResult::OnCooldown);
  tick_combat(ac, cfg().combat, 0.8f);
  t.pos = {0, 0, 5};
  CHECK(bite(1.0f).result == BiteResult::OutOfRange);
  tick_combat(ac, cfg().combat, 0.8f);
  t.pos = {0, 0, -1};  // phía sau lưng
  CHECK(bite(2.0f).result == BiteResult::OutOfArc);
  // Né: iframes làm cú cắn trượt
  t.pos = {0, 0, 1};
  tick_combat(ac, cfg().combat, 0.8f);
  CHECK(request_dodge(t, cfg()));
  CHECK_FALSE(request_dodge(t, cfg()));  // hồi chiêu né
  CHECK(bite(3.0f).result == BiteResult::Dodged);
  // Choáng: 3 cú trúng trong 2 giây
  CombatState tc2;
  SurvivalState ts2;
  Combatant t2{{0, 0, 1}, 0, false, &ts2, &tc2};
  int hits = 0;
  bool stag = false;
  for (int i = 0; i < 3; ++i) {
    ac.bite_cd = 0;
    as.stamina = 100;
    auto o = request_bite(a, t2, ZoneRule::Contested, cfg(), 10.0f + 0.5f * static_cast<float>(i));
    hits += o.result == BiteResult::Hit;
    stag |= o.target_staggered;
  }
  CHECK(hits == 3);
  CHECK(stag);
  CHECK(tc2.stagger > 0);
  // Hết thể lực
  ac.bite_cd = 0;
  as.stamina = 5;
  CHECK(request_bite(a, t2, ZoneRule::Contested, cfg(), 20.0f).result == BiteResult::NoStamina);
  // Sát thương kẹp chênh lệch ±25%
  CHECK(bite_damage(cfg().combat, 5.0f, 0.0f) == doctest::Approx(12 * 1.25f));
}

TEST_CASE("vùng PvP: safe cấm, restricted cần cả hai hung hăng + không phải người mới, contested tự do; NPC không áp luật PvP") {
  const auto& c = cfg().combat;
  CombatState a, b;
  a.level = b.level = 10;
  CHECK_FALSE(pvp_allowed(ZoneRule::Safe, a, b, c));
  CHECK_FALSE(pvp_allowed(ZoneRule::Restricted, a, b, c));
  a.aggressive = b.aggressive = true;
  CHECK(pvp_allowed(ZoneRule::Restricted, a, b, c));
  b.level = 2;
  CHECK_FALSE(pvp_allowed(ZoneRule::Restricted, a, b, c));
  CHECK(pvp_allowed(ZoneRule::Contested, a, b, c));

  SurvivalState as, ts;
  CombatState ac, tc;
  Combatant p1{{0, 0, 0}, 0, true, &as, &ac}, p2{{0, 0, 1}, 0, true, &ts, &tc};
  CHECK(request_bite(p1, p2, ZoneRule::Safe, cfg(), 0).result == BiteResult::NotAllowed);
  CHECK(as.stamina == doctest::Approx(100));  // bị từ chối thì không tốn thể lực
  Combatant npc{{0, 0, 1}, 0, false, &ts, &tc};
  CHECK(request_bite(p1, npc, ZoneRule::Safe, cfg(), 0).result == BiteResult::Hit);
  CHECK(ac.notoriety == doctest::Approx(c.npc_aggression_notoriety));  // hậu quả nhẹ: tăng tai tiếng
  tick_combat(ac, c, 100.0f);
  CHECK(ac.notoriety < c.npc_aggression_notoriety);
}

TEST_CASE("hồi sinh: tại nơi trú ẩn/spawn gần nhất, HP một phần, rơi tối đa 50% đồ tiêu hao, giữ cosmetic") {
  pb::ConfigDiagnostics d;
  auto wc = pb::world::load_world_config(std::filesystem::path(PB_SOURCE_DIR) / "data/world/demo.json", d);
  const auto w = pb::world::generate_world(wc);
  SurvivalState s;
  s.hp = 0;
  s.life = LifeState::Dead;
  Inventory inv(cfg().inventory_slots);
  inv.add(cfg(), "scrap_food", 3);
  inv.add(cfg(), "old_collar", 1);
  inv.add(cfg(), "meat_bone", 1);
  auto o = respawn(s, inv, {10, 10}, w, cfg());
  CHECK(s.life == LifeState::Alive);
  CHECK(s.hp == doctest::Approx(50));
  CHECK(inv.count_of("old_collar") == 1);
  CHECK(inv.count_of("scrap_food") == 2);  // floor(3*0.5)=1 bị rơi
  CHECK(inv.count_of("meat_bone") == 1);   // floor(1*0.5)=0
  CHECK(o.dropped_items == 1);
  CHECK(o.pos.x > 0);
}
