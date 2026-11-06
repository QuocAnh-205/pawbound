#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <set>

#include <glm/geometric.hpp>

#include "sim/npc.hpp"
#include "world/generator.hpp"
#include "world/world_config.hpp"

using namespace pb::sim;
namespace fs = std::filesystem;

namespace {
struct Fixture {
  pb::world::WorldData world;
  GameplayConfig gc;
  NpcConfig nc;
  std::unique_ptr<CollisionWorld> col;
  std::unique_ptr<NavGraph> nav;
  std::vector<ResourceNode> res;
  Fixture() {
    pb::ConfigDiagnostics d;
    world = pb::world::generate_world(pb::world::load_world_config(fs::path(PB_SOURCE_DIR) / "data/world/demo.json", d));
    gc = load_gameplay_config(fs::path(PB_SOURCE_DIR) / "data/gameplay/gameplay.json", d);
    nc = load_npc_config(fs::path(PB_SOURCE_DIR) / "data/gameplay/npcs.json", d);
    REQUIRE(d.ok());
    col = std::make_unique<CollisionWorld>(world);
    nav = std::make_unique<NavGraph>(world);
    res = make_resource_nodes(world, gc);
  }
  glm::vec3 spawn() const {
    for (const auto& c : world.chunks)
      for (const auto& p : c.pois)
        if (p.kind == pb::world::PoiKind::Spawn) return {pb::world::to_m(p.x), 0, pb::world::to_m(p.z)};
    return {};
  }
};
Fixture& fx() {
  static Fixture f;
  return f;
}
// NPC đứng một mình trước mặt người chơi (thế giới phẳng, không vật cản)
pb::world::WorldData flat() {
  pb::world::WorldData w;
  w.size_x = w.size_z = 20000;
  w.chunk_size = 6400;
  w.chunks_x = w.chunks_z = 4;
  w.terrain.cells_x = w.terrain.cells_z = 50;
  w.terrain.cell = 400;
  w.terrain.height_cm.assign(51 * 51, 0);
  w.chunks.resize(16);
  w.district_cell = 1600;
  w.dcells_x = w.dcells_z = 13;
  w.districts.assign(169, 1);
  return w;
}
}  // namespace

TEST_CASE("A*: đường ngắn nhất, không có đường, giới hạn mở rộng, cache") {
  // Lưới 5x5 cạnh 1 m, nút (x,z) = z*5+x; cắt cạnh để có đường vòng
  std::vector<glm::vec2> nodes;
  std::vector<std::pair<uint32_t, uint32_t>> edges;
  for (int z = 0; z < 5; ++z)
    for (int x = 0; x < 5; ++x) nodes.emplace_back(x, z);
  auto id = [](int x, int z) { return static_cast<uint32_t>(z * 5 + x); };
  for (int z = 0; z < 5; ++z)
    for (int x = 0; x < 5; ++x) {
      if (x < 4 && !(x == 1 && z < 4)) edges.emplace_back(id(x, z), id(x + 1, z));  // tường giữa cột 1-2 trừ hàng 4
      if (z < 4) edges.emplace_back(id(x, z), id(x, z + 1));
    }
  nodes.emplace_back(100, 100);  // nút cô lập (25)
  NavGraph g(nodes, edges);
  auto r = g.find_path(id(0, 0), id(4, 0));
  REQUIRE(r.status == PathResult::Status::Found);
  CHECK(r.length == doctest::Approx(4 + 4 + 4));  // vòng qua hàng 4
  CHECK(r.nodes.front() == id(0, 0));
  CHECK(r.nodes.back() == id(4, 0));
  CHECK(g.find_path(0, 25).status == PathResult::Status::NoPath);
  CHECK(g.find_path(0, 999).status == PathResult::Status::InvalidNode);
  CHECK(g.find_path(id(0, 0), id(4, 0), 3).status == PathResult::Status::Truncated);
  g.find_path_cached(0, 24);
  g.find_path_cached(0, 24);
  CHECK(g.cache_hits() == 1);
  CHECK(g.cache_misses() == 1);
  CHECK(g.nearest({0.2f, 0.1f}) == 0u);
  CHECK_FALSE(g.nearest({60, 60}, 5.0f).has_value());
}

TEST_CASE("nav thế giới demo: tìm được đường giữa các nút xa nhau") {
  auto& f = fx();
  const auto a = f.nav->nearest({40, 40}), b = f.nav->nearest({470, 470});
  REQUIRE(a);
  REQUIRE(b);
  const auto r = f.nav->find_path(*a, *b, 20000);
  CHECK(r.status == PathResult::Status::Found);
  CHECK(r.length > 500.0f);
}

TEST_CASE("tầm nhìn: nhà chắn line-of-sight") {
  auto w = flat();
  CollisionWorld col(w);
  col.add_box({{10, 9}, {12, 11}, 10.0f});
  CHECK(col.segment_blocked({5, 10}, {20, 10}, 1.2f));
  CHECK_FALSE(col.segment_blocked({5, 5}, {20, 5}, 1.2f));
  col.add_box({{14, 4}, {15, 6}, 0.5f});  // ghế thấp không che tầm mắt 1,2 m
  CHECK_FALSE(col.segment_blocked({5, 5}, {20, 5}, 1.2f));
}

namespace {
// Một NPC đặt tay trước mặt người chơi trong thế giới phẳng
struct Solo {
  pb::world::WorldData w = flat();
  GameplayConfig gc;
  NpcConfig nc;
  std::unique_ptr<CollisionWorld> col;
  std::unique_ptr<NavGraph> nav;
  std::unique_ptr<NpcManager> mgr;
  SurvivalState psv;
  CombatState pcb;
  PlayerView pv;
  explicit Solo(NpcKind kind, Personality pers, glm::vec2 npc_pos = {100, 105}) {
    pb::ConfigDiagnostics d;
    gc = load_gameplay_config(fs::path(PB_SOURCE_DIR) / "data/gameplay/gameplay.json", d);
    nc = load_npc_config(fs::path(PB_SOURCE_DIR) / "data/gameplay/npcs.json", d);
    nc.humans_per_chunk.fill(0);
    nc.dogs_per_chunk.fill(0);
    col = std::make_unique<CollisionWorld>(w);
    std::vector<glm::vec2> nodes;
    std::vector<std::pair<uint32_t, uint32_t>> edges;
    for (int i = 0; i < 20; ++i) {
      nodes.emplace_back(60 + i * 5, 100);
      if (i) edges.emplace_back(i - 1, i);
    }
    nav = std::make_unique<NavGraph>(nodes, edges);
    mgr = std::make_unique<NpcManager>(w, *col, *nav, gc, nc, nullptr);
    pv = {1, {100, 0, 100}, 0, &psv, &pcb};
    mgr->update(0.0f, std::span(&pv, 1), {});  // kích hoạt chunk (không spawn gì)
    Npc n;
    n.id = 7;
    n.kind = kind;
    n.pers = pers;
    n.state = kind == NpcKind::Human ? NpcState::Wait : NpcState::Roam;
    n.home_chunk = w.chunk_index(1, 1);
    n.motor.pos = {npc_pos.x, 0, npc_pos.y};
    n.motor.yaw = 3.14159f;  // nhìn -Z, về phía người chơi
    if (kind == NpcKind::Human) {
      n.stats.walk_speed = 1.3f;
      n.stats.run_speed = 3.6f;
      n.stats.trot_speed = 2.0f;
      n.stats.sprint_speed = 4.0f;
    }
    mgr->npcs_mut().push_back(n);
  }
  std::vector<AiEvent> run(float seconds) {
    std::vector<AiEvent> all;
    for (int i = 0; i < static_cast<int>(seconds * 30); ++i) {
      auto ev = mgr->update(1.0f / 30, std::span(&pv, 1), {});
      all.insert(all.end(), ev.begin(), ev.end());
    }
    return all;
  }
  Npc& npc() { return mgr->npcs_mut().front(); }
  float dist() { return glm::distance(glm::vec2(npc().motor.pos.x, npc().motor.pos.z), glm::vec2(pv.pos.x, pv.pos.z)); }
};
bool has(const std::vector<AiEvent>& v, AiEvent::Type t) {
  for (const auto& e : v)
    if (e.type == t) return true;
  return false;
}
}  // namespace

TEST_CASE("người sợ hãi: bỏ chạy khi chó lại gần") {
  Solo s(NpcKind::Human, Personality::Fearful, {100, 103});
  s.run(0.2f);
  CHECK(s.npc().state == NpcState::Flee);
  const float d0 = s.dist();
  s.run(2.0f);
  CHECK(s.dist() > d0 + 3.0f);
}

TEST_CASE("người thân thiện: lại gần và cho ăn khi chó không có tai tiếng; tránh khi tai tiếng cao") {
  Solo s(NpcKind::Human, Personality::Friendly, {100, 106});
  const auto ev = s.run(8.0f);
  CHECK(has(ev, AiEvent::Type::FeedPlayer));
  Solo bad(NpcKind::Human, Personality::Friendly, {100, 104});
  bad.pcb.notoriety = 50;
  bad.run(0.2f);
  CHECK(bad.npc().state == NpcState::Flee);
  const auto ev2 = bad.run(4.0f);
  CHECK_FALSE(has(ev2, AiEvent::Type::FeedPlayer));
  CHECK(bad.dist() > 10.0f);
}

TEST_CASE("người hung dữ: dọa, và tự vệ (đá) khi bị cắn") {
  Solo s(NpcKind::Human, Personality::Hostile, {100, 104});
  auto ev = s.run(0.5f);
  CHECK(has(ev, AiEvent::Type::Shout));
  s.mgr->on_attacked(7, 1);
  CHECK(s.npc().state == NpcState::Defend);
  ev = s.run(3.0f);
  CHECK(has(ev, AiEvent::Type::KickPlayer));
  CHECK(s.psv.hp < 100);
}

TEST_CASE("chó hung dữ: gầm gừ rồi tấn công; bỏ chạy khi máu thấp") {
  Solo s(NpcKind::Dog, Personality::Aggressive, {100, 103});
  auto ev = s.run(0.3f);
  CHECK(has(ev, AiEvent::Type::Growl));
  CHECK(s.npc().state == NpcState::Growl);
  ev = s.run(4.0f);
  CHECK(has(ev, AiEvent::Type::BitePlayer));
  CHECK(s.psv.hp < 100);
  s.npc().sv.hp = 10;
  s.run(0.2f);
  CHECK(s.npc().state == NpcState::Flee);
}

TEST_CASE("chó thân thiện đi theo; chó trung lập bị cắn thì bỏ chạy") {
  Solo f(NpcKind::Dog, Personality::Friendly, {100, 105});
  f.run(0.3f);
  CHECK(f.npc().state == NpcState::Follow);
  f.pv.pos = {100, 0, 92};  // người chơi đi xa ~11 m (trong ngưỡng 20 m)
  f.run(6.0f);
  CHECK(f.dist() < 4.0f);
  Solo n(NpcKind::Dog, Personality::Neutral, {100, 103});
  n.npc().sv.hp = 80;  // bị cắn (HP giảm) -> tự phát hiện
  n.run(0.2f);
  CHECK(n.npc().state == NpcState::Flee);
}

TEST_CASE("chó đói tìm tới nguồn thức ăn trong thế giới demo và ăn") {
  auto& f = fx();
  auto res = f.res;
  NpcConfig nc = f.nc;
  nc.humans_per_chunk.fill(0);
  nc.dogs_per_chunk.fill(0);
  NpcManager mgr(f.world, *f.col, *f.nav, f.gc, nc, &res);
  // Chọn một nguồn thức ăn, đặt chó cách ~15 m trên nút nav gần đó
  const ResourceNode* food = nullptr;
  for (const auto& r : res)
    if (r.kind == pb::world::PoiKind::Food) food = &r;
  REQUIRE(food);
  SurvivalState psv;
  CombatState pcb;
  PlayerView pv{1, {food->pos.x + 30, 0, food->pos.y + 30}, 0, &psv, &pcb};
  mgr.update(0, std::span(&pv, 1), {});
  const auto start = f.nav->nearest(food->pos + glm::vec2(12, 0));
  REQUIRE(start);
  Npc dog;
  dog.id = 99;
  dog.kind = NpcKind::Dog;
  dog.pers = Personality::Neutral;
  dog.state = NpcState::Roam;
  dog.home_chunk = *mgr.active_chunks().begin();
  for (int c : mgr.active_chunks()) dog.home_chunk = c;
  const glm::vec2 sp = f.nav->pos(*start);
  dog.home_chunk = f.world.chunk_index(static_cast<int>(sp.x / 64), static_cast<int>(sp.y / 64));
  dog.motor.pos = {sp.x, f.world.height_at(sp.x, sp.y), sp.y};
  dog.sv.hunger = 20;
  dog.sv.thirst = 100;
  pv.pos = {sp.x + 40, 0, sp.y + 40};  // người chơi ở xa, không gây xao nhãng
  mgr.npcs_mut().push_back(dog);
  bool ate = false;
  for (int i = 0; i < 30 * 40 && !ate; ++i) {
    mgr.update(1.0f / 30, std::span(&pv, 1), {});
    const Npc* n = mgr.find(99);
    REQUIRE(n);
    ate = n->sv.hunger > 40;
  }
  CHECK(ate);
}

TEST_CASE("spawn/despawn theo chunk: deterministic, không trùng id, gỡ khi đi xa") {
  auto& f = fx();
  NpcManager mgr(f.world, *f.col, *f.nav, f.gc, f.nc, nullptr);
  SurvivalState psv;
  CombatState pcb;
  PlayerView pv{1, f.spawn(), 0, &psv, &pcb};
  mgr.update(1.0f / 30, std::span(&pv, 1), {});
  const auto first = mgr.npcs();
  CHECK(!first.empty());
  CHECK(mgr.active_chunks().size() <= 9);
  std::set<uint32_t> ids;
  for (const auto& n : first) {
    CHECK(ids.insert(n.id).second);
    CHECK(mgr.active_chunks().contains(n.home_chunk));
  }
  // Đi sang góc đối diện: NPC cũ bị gỡ
  pv.pos = {500 - f.spawn().x, 0, 500 - f.spawn().z};
  mgr.update(1.0f / 30, std::span(&pv, 1), {});
  for (const auto& n : mgr.npcs()) CHECK(mgr.active_chunks().contains(n.home_chunk));
  // Quay lại: cùng tập id (spawn deterministic theo seed + chunk)
  pv.pos = f.spawn();
  mgr.update(1.0f / 30, std::span(&pv, 1), {});
  std::set<uint32_t> again;
  for (const auto& n : mgr.npcs()) again.insert(n.id);
  CHECK(again == ids);
}

TEST_CASE("kịch bản AI 60 giây ở trung tâm (seed cố định): NPC di chuyển, không kẹt trong nhà, ngân sách AI đo được") {
  auto& f = fx();
  auto res = f.res;
  NpcConfig nc = f.nc;
  nc.active_radius_chunks = 2;  // nhiều NPC hơn để đo chi phí
  NpcManager mgr(f.world, *f.col, *f.nav, f.gc, nc, &res);
  SurvivalState psv;
  CombatState pcb;
  PlayerView pv{1, {388, 0, 165}, 0, &psv, &pcb};  // khu downtown của seed demo
  mgr.update(0, std::span(&pv, 1), {});
  std::unordered_map<uint32_t, glm::vec3> start;
  for (const auto& n : mgr.npcs()) start[n.id] = n.motor.pos;
  double total_ms = 0;
  int ticks = 0;
  for (int i = 0; i < 30 * 60; ++i) {
    mgr.update(1.0f / 30, std::span(&pv, 1), {});
    total_ms += mgr.stats().last_ms;
    ++ticks;
  }
  int moved = 0, inside = 0;
  for (const auto& n : mgr.npcs()) {
    if (start.contains(n.id) && glm::distance(start[n.id], n.motor.pos) > 3.0f) ++moved;
    if (f.col->overlaps({n.motor.pos.x, n.motor.pos.z}, 0.05f, n.motor.pos.y, 0.3f)) ++inside;
  }
  MESSAGE("NPC hoạt động: " << mgr.npcs().size() << ", đã di chuyển > 3 m: " << moved << ", AI trung bình "
                            << total_ms / ticks << " ms/tick, max " << mgr.stats().max_ms << " ms, path req "
                            << mgr.stats().path_requests << " (hoãn " << mgr.stats().path_deferred << ", lỗi "
                            << mgr.stats().path_failed << "), cache hit " << f.nav->cache_hits());
  CHECK(mgr.npcs().size() >= 20);
  CHECK(moved >= static_cast<int>(mgr.npcs().size()) / 2);
  CHECK(inside == 0);
  CHECK(total_ms / ticks < 5.0);  // Debug build; Release đo riêng trong báo cáo
}

