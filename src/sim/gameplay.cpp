#include "sim/gameplay.hpp"

#include <algorithm>
#include <cmath>
#include <format>

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <nlohmann/json.hpp>

namespace pb::sim {

// ============================================================================ config
const ItemDef* GameplayConfig::item(const std::string& id) const {
  for (const auto& i : items)
    if (i.id == id) return &i;
  return nullptr;
}

namespace {
GameplayConfig from_json(const nlohmann::json& j, ConfigDiagnostics& d) {
  GameplayConfig g;
  ConfigReader r(j, "", d);
  auto s = r.child("survival");
  auto& sv = g.survival;
  s.read("max_hp", sv.max_hp, 1, 10000);
  s.read("max_hunger", sv.max_hunger, 1, 10000);
  s.read("max_thirst", sv.max_thirst, 1, 10000);
  s.read("max_stamina", sv.max_stamina, 1, 10000);
  s.read("max_rest", sv.max_rest, 1, 10000);
  s.read("hunger_full_to_empty_s", sv.hunger_full_to_empty_s, 60, 1e7);
  s.read("thirst_full_to_empty_s", sv.thirst_full_to_empty_s, 60, 1e7);
  s.read("rest_full_to_empty_s", sv.rest_full_to_empty_s, 60, 1e7);
  s.read("hp_regen_per_s", sv.hp_regen_per_s, 0, 100);
  s.read("starving_hp_loss_per_s", sv.starving_hp_loss_per_s, 0, 100);
  s.read("dehydrated_hp_loss_per_s", sv.dehydrated_hp_loss_per_s, 0, 100);
  s.read("low_threshold", sv.low_threshold, 0, 100);
  s.read("tired_threshold", sv.tired_threshold, 0, 100);
  s.read("tired_speed_mult", sv.tired_speed_mult, 0.1, 1);
  s.read("thirsty_stamina_max_mult", sv.thirsty_stamina_max_mult, 0.1, 1);
  s.read("stamina_regen_per_s", sv.stamina_regen_per_s, 0, 1000);
  s.read("stamina_regen_delay_s", sv.stamina_regen_delay_s, 0, 60);
  s.read("sprint_stamina_per_s", sv.sprint_stamina_per_s, 0, 1000);
  s.read("sprint_min_stamina", sv.sprint_min_stamina, 0, 100);
  s.read("run_stamina_per_s", sv.run_stamina_per_s, 0, 1000);
  s.read("jump_stamina", sv.jump_stamina, 0, 100);
  s.read("rest_recover_per_s", sv.rest_recover_per_s, 0, 100);
  s.read("rest_hp_bonus_per_s", sv.rest_hp_bonus_per_s, 0, 100);
  s.read("shelter_rest_mult", sv.shelter_rest_mult, 1, 10);
  s.read("downed_seconds", sv.downed_seconds, 0, 600);
  s.read("respawn_hp_fraction", sv.respawn_hp_fraction, 0.05, 1);
  s.read("respawn_drop_consumable_fraction", sv.respawn_drop_consumable_fraction, 0, 1);
  auto c = r.child("combat");
  auto& cb = g.combat;
  c.read("bite_damage", cb.bite_damage, 0, 1000);
  c.read("bite_cooldown_s", cb.bite_cooldown_s, 0.05, 60);
  c.read("bite_stamina", cb.bite_stamina, 0, 100);
  c.read("bite_range_m", cb.bite_range_m, 0.2, 10);
  c.read("bite_arc_deg", cb.bite_arc_deg, 1, 360);
  c.read("dodge_iframes_s", cb.dodge_iframes_s, 0, 5);
  c.read("dodge_cooldown_s", cb.dodge_cooldown_s, 0, 60);
  c.read("dodge_stamina", cb.dodge_stamina, 0, 100);
  c.read("stagger_hits", cb.stagger_hits, 1, 100);
  c.read("stagger_window_s", cb.stagger_window_s, 0.1, 60);
  c.read("stagger_s", cb.stagger_s, 0, 10);
  c.read("escape_distance_m", cb.escape_distance_m, 1, 100);
  c.read("escape_time_s", cb.escape_time_s, 0.1, 60);
  c.read("newbie_level", cb.newbie_level, 0, 100);
  c.read("npc_aggression_notoriety", cb.npc_aggression_notoriety, 0, 1000);
  c.read("notoriety_decay_per_s", cb.notoriety_decay_per_s, 0, 100);
  auto rs = r.child("resources");
  auto rule = [&](const char* k, ResourceRule& rr) {
    auto x = rs.child(k);
    x.read("capacity", rr.capacity, 1, 1000);
    x.read("amount", rr.amount, 0, 1000);
    x.read("regen_s", rr.regen_s, 1, 1e6);
  };
  rule("food", g.food);
  rule("water", g.water);
  rs.read("interact_range_m", g.interact_range_m, 0.5, 10);
  rs.read("sniff_radius_m", g.sniff_radius_m, 1, 200);
  r.child("inventory").read("slots", g.inventory_slots, 1, 64);
  if (!j.contains("items") || !j["items"].is_array()) {
    d.errors.emplace_back("cần mảng 'items'");
    return g;
  }
  size_t i = 0;
  for (const auto& ij : j["items"]) {
    ItemDef it;
    ConfigReader ir(ij, std::format("items[{}]", i++), d);
    ir.read("id", it.id);
    ir.read("name", it.name);
    ir.read("category", it.category);
    ir.read("max_stack", it.max_stack, 1, 999);
    ir.read("hunger", it.hunger, -100, 100);
    ir.read("thirst", it.thirst, -100, 100);
    ir.read("consumable", it.consumable);
    if (g.item(it.id)) d.errors.push_back("item trùng id: " + it.id);
    g.items.push_back(it);
  }
  return g;
}
}  // namespace

GameplayConfig parse_gameplay_config(std::string_view t, ConfigDiagnostics& d) { return from_json(parse_json_config(t, d), d); }
GameplayConfig load_gameplay_config(const std::filesystem::path& p, ConfigDiagnostics& d) {
  return from_json(load_json_config(p, d, true), d);
}

// ============================================================================ sinh tồn
float stamina_max(const SurvivalState& s, const SurvivalConfig& c) {
  return s.thirst < c.low_threshold ? c.max_stamina * c.thirsty_stamina_max_mult : c.max_stamina;
}
float speed_multiplier(const SurvivalState& s, const SurvivalConfig& c) {
  if (s.life != LifeState::Alive) return 0.0f;
  return s.rest < c.tired_threshold ? c.tired_speed_mult : 1.0f;
}
bool can_sprint(const SurvivalState& s, const SurvivalConfig& c, bool currently_sprinting) {
  if (s.life != LifeState::Alive) return false;
  return currently_sprinting ? s.stamina > 0.0f : s.stamina >= c.sprint_min_stamina;
}

bool spend_stamina(SurvivalState& s, const SurvivalConfig&, float amount) {
  if (s.life != LifeState::Alive || s.stamina < amount) return false;
  s.stamina -= amount;
  s.stamina_idle = 0;
  return true;
}

void apply_damage(SurvivalState& s, float amount) {
  if (s.life != LifeState::Alive || amount <= 0) return;
  s.hp = std::max(0.0f, s.hp - amount);
  if (s.hp <= 0.0f) {
    s.life = LifeState::Downed;
    s.downed_timer = 0;
    s.resting = false;
  }
}

void tick_survival(SurvivalState& s, const SurvivalConfig& c, const Activity& a, float dt) {
  if (s.life == LifeState::Downed) {
    s.downed_timer += dt;
    if (s.downed_timer >= c.downed_seconds) s.life = LifeState::Dead;  // chờ respawn()
    return;
  }
  if (s.life == LifeState::Dead) return;
  s.resting = a.resting && !a.moving;
  // Tiêu hao theo thời gian mô phỏng (vận động mạnh tiêu nhanh hơn)
  const float effort = a.sprinting ? 1.6f : a.moving ? 1.15f : 1.0f;
  s.hunger = std::max(0.0f, s.hunger - c.max_hunger / c.hunger_full_to_empty_s * effort * dt);
  s.thirst = std::max(0.0f, s.thirst - c.max_thirst / c.thirst_full_to_empty_s * effort * dt);
  if (s.resting) {
    const float mult = a.in_shelter ? c.shelter_rest_mult : 1.0f;
    s.rest = std::min(c.max_rest, s.rest + c.rest_recover_per_s * mult * dt);
  } else {
    s.rest = std::max(0.0f, s.rest - c.max_rest / c.rest_full_to_empty_s * dt);
  }
  // Thể lực
  if (a.sprinting && a.moving) {
    s.stamina = std::max(0.0f, s.stamina - c.sprint_stamina_per_s * dt);
    s.stamina_idle = 0;
  } else if (a.moving && c.run_stamina_per_s > 0) {
    s.stamina = std::max(0.0f, s.stamina - c.run_stamina_per_s * dt);
    s.stamina_idle = 0;
  } else {
    s.stamina_idle += dt;
    if (s.stamina_idle >= c.stamina_regen_delay_s)
      s.stamina += c.stamina_regen_per_s * (s.resting ? 1.5f : 1.0f) * dt;
  }
  s.stamina = std::min(s.stamina, stamina_max(s, c));
  // HP
  const bool fed = s.hunger > c.low_threshold && s.thirst > c.low_threshold;
  if (fed) s.hp = std::min(c.max_hp, s.hp + (c.hp_regen_per_s + (s.resting ? c.rest_hp_bonus_per_s : 0.0f)) * dt);
  float loss = 0;
  if (s.hunger <= 0) loss += c.starving_hp_loss_per_s * dt;
  if (s.thirst <= 0) loss += c.dehydrated_hp_loss_per_s * dt;
  if (loss > 0) apply_damage(s, loss);
}

// ============================================================================ inventory
int Inventory::find(const std::string& item) const {
  for (size_t i = 0; i < slots_.size(); ++i)
    if (slots_[i].item == item && slots_[i].count > 0) return static_cast<int>(i);
  return -1;
}

int Inventory::count_of(const std::string& item) const {
  int n = 0;
  for (const auto& s : slots_)
    if (s.item == item) n += s.count;
  return n;
}

int Inventory::add(const GameplayConfig& cfg, const std::string& item, int count) {
  const ItemDef* def = cfg.item(item);
  if (!def || count <= 0) return std::max(count, 0);
  // Gộp vào chồng sẵn có trước, rồi ô trống
  for (auto& s : slots_) {
    if (count == 0) break;
    if (s.item == item && s.count < def->max_stack) {
      const int take = std::min(count, def->max_stack - s.count);
      s.count += take;
      count -= take;
    }
  }
  for (auto& s : slots_) {
    if (count == 0) break;
    if (s.empty()) {
      const int take = std::min(count, def->max_stack);
      s = {item, take};
      count -= take;
    }
  }
  return count;
}

bool Inventory::remove(int slot, int count) {
  if (slot < 0 || static_cast<size_t>(slot) >= slots_.size() || count <= 0) return false;
  auto& s = slots_[static_cast<size_t>(slot)];
  if (s.count < count) return false;
  s.count -= count;
  if (s.count == 0) s.item.clear();
  return true;
}

bool Inventory::apply_once(uint64_t op_id) {
  if (applied_.contains(op_id)) return false;
  applied_.insert(op_id);
  applied_order_.push_back(op_id);
  if (applied_order_.size() > 1024) {  // giữ cửa sổ id gần đây
    applied_.erase(applied_order_.front());
    applied_order_.pop_front();
  }
  return true;
}

std::string Inventory::validate(const GameplayConfig& cfg) const {
  for (size_t i = 0; i < slots_.size(); ++i) {
    const auto& s = slots_[i];
    if (s.empty()) {
      if (s.count != 0 || !s.item.empty()) return std::format("ô {}: dữ liệu rỗng không nhất quán", i);
      continue;
    }
    const ItemDef* d = cfg.item(s.item);
    if (!d) return std::format("ô {}: item không tồn tại '{}'", i, s.item);
    if (s.count > d->max_stack) return std::format("ô {}: {} vượt max_stack {}", i, s.count, d->max_stack);
  }
  return {};
}

std::string Inventory::to_json() const {
  nlohmann::json a = nlohmann::json::array();
  for (const auto& s : slots_) a.push_back(s.empty() ? nlohmann::json(nullptr) : nlohmann::json{{"item", s.item}, {"count", s.count}});
  return nlohmann::json{{"version", 1}, {"slots", a}}.dump();
}

std::optional<Inventory> Inventory::from_json(const GameplayConfig& cfg, std::string_view text, std::string& error) {
  auto j = nlohmann::json::parse(text, nullptr, false);
  if (j.is_discarded() || !j.is_object() || j.value("version", 0) != 1 || !j.contains("slots") || !j["slots"].is_array()) {
    error = "inventory JSON không hợp lệ";
    return std::nullopt;
  }
  if (j["slots"].size() != static_cast<size_t>(cfg.inventory_slots)) {
    error = "số ô inventory không khớp cấu hình";
    return std::nullopt;
  }
  Inventory inv(cfg.inventory_slots);
  for (size_t i = 0; i < j["slots"].size(); ++i) {
    const auto& e = j["slots"][i];
    if (e.is_null()) continue;
    if (!e.is_object() || !e.contains("item") || !e["item"].is_string() || !e.contains("count") || !e["count"].is_number_integer()) {
      error = std::format("ô {} sai định dạng", i);
      return std::nullopt;
    }
    inv.slots_[i] = {e["item"].get<std::string>(), e["count"].get<int>()};
    if (inv.slots_[i].count <= 0) {
      error = std::format("ô {}: số lượng không hợp lệ", i);
      return std::nullopt;
    }
  }
  if (auto v = inv.validate(cfg); !v.empty()) {
    error = v;
    return std::nullopt;
  }
  return inv;
}

// ============================================================================ tài nguyên
std::vector<ResourceNode> make_resource_nodes(const world::WorldData& w, const GameplayConfig& cfg) {
  std::vector<ResourceNode> out;
  for (const auto& c : w.chunks)
    for (const auto& p : c.pois) {
      if (p.kind != world::PoiKind::Water && p.kind != world::PoiKind::Food) continue;
      const ResourceRule& r = p.kind == world::PoiKind::Water ? cfg.water : cfg.food;
      out.push_back({p.id, p.kind, {world::to_m(p.x), world::to_m(p.z)}, r.capacity, r.capacity, r.amount, r.regen_s, 0});
    }
  return out;
}

void tick_resources(std::vector<ResourceNode>& nodes, float dt) {
  for (auto& n : nodes) {
    if (n.remaining >= n.capacity) {
      n.regen_timer = 0;
      continue;
    }
    n.regen_timer += dt;
    while (n.regen_timer >= n.regen_s && n.remaining < n.capacity) {
      n.regen_timer -= n.regen_s;
      ++n.remaining;
    }
  }
}

const char* to_string(InteractResult r) {
  switch (r) {
    case InteractResult::Ok: return "ok";
    case InteractResult::OutOfRange: return "quá xa";
    case InteractResult::Depleted: return "đã hết, chờ hồi";
    case InteractResult::NotAlive: return "đang gục";
    case InteractResult::Full: return "đã no/đủ";
    case InteractResult::NoTarget: return "không có gì gần";
    case InteractResult::InventoryFull: return "túi đầy";
    case InteractResult::UnknownItem: return "vật phẩm không hợp lệ";
  }
  return "?";
}

InteractResult consume_at(ResourceNode& n, glm::vec2 pos, SurvivalState& s, const GameplayConfig& cfg) {
  if (s.life != LifeState::Alive) return InteractResult::NotAlive;
  if (glm::distance(pos, n.pos) > cfg.interact_range_m) return InteractResult::OutOfRange;
  if (n.remaining <= 0) return InteractResult::Depleted;
  float& stat = n.kind == world::PoiKind::Water ? s.thirst : s.hunger;
  const float max = n.kind == world::PoiKind::Water ? cfg.survival.max_thirst : cfg.survival.max_hunger;
  if (stat >= max - 1.0f) return InteractResult::Full;
  stat = std::min(max, stat + n.amount);
  --n.remaining;
  return InteractResult::Ok;
}

InteractResult forage_at(ResourceNode& n, glm::vec2 pos, Inventory& inv, const GameplayConfig& cfg) {
  if (glm::distance(pos, n.pos) > cfg.interact_range_m) return InteractResult::OutOfRange;
  if (n.kind != world::PoiKind::Food) return InteractResult::NoTarget;
  if (n.remaining <= 0) return InteractResult::Depleted;
  if (inv.add(cfg, "scrap_food", 1) != 0) return InteractResult::InventoryFull;
  --n.remaining;
  return InteractResult::Ok;
}

InteractResult use_item(Inventory& inv, int slot, SurvivalState& s, const GameplayConfig& cfg) {
  if (s.life != LifeState::Alive) return InteractResult::NotAlive;
  if (slot < 0 || static_cast<size_t>(slot) >= inv.slots().size() || inv.slots()[static_cast<size_t>(slot)].empty())
    return InteractResult::NoTarget;
  const ItemDef* d = cfg.item(inv.slots()[static_cast<size_t>(slot)].item);
  if (!d || !d->consumable) return InteractResult::UnknownItem;
  if (!inv.remove(slot, 1)) return InteractResult::NoTarget;
  s.hunger = std::clamp(s.hunger + d->hunger, 0.0f, cfg.survival.max_hunger);
  s.thirst = std::clamp(s.thirst + d->thirst, 0.0f, cfg.survival.max_thirst);
  return InteractResult::Ok;
}

ResourceNode* nearest_node(std::vector<ResourceNode>& nodes, glm::vec2 pos, float range, std::optional<world::PoiKind> kind) {
  ResourceNode* best = nullptr;
  float bd = range;
  for (auto& n : nodes) {
    if (kind && n.kind != *kind) continue;
    const float d = glm::distance(pos, n.pos);
    if (d <= bd) {
      bd = d;
      best = &n;
    }
  }
  return best;
}

// ============================================================================ chiến đấu
const char* to_string(BiteResult r) {
  switch (r) {
    case BiteResult::Hit: return "trúng";
    case BiteResult::Miss: return "trượt";
    case BiteResult::Dodged: return "bị né";
    case BiteResult::OnCooldown: return "đang hồi chiêu";
    case BiteResult::NoStamina: return "hết thể lực";
    case BiteResult::OutOfRange: return "ngoài tầm";
    case BiteResult::OutOfArc: return "không đối diện mục tiêu";
    case BiteResult::NotAllowed: return "vùng này cấm PvP";
    case BiteResult::Staggered: return "đang choáng";
    case BiteResult::TargetDown: return "mục tiêu đã gục";
  }
  return "?";
}

bool pvp_allowed(world::ZoneRule zone, const CombatState& a, const CombatState& t, const CombatConfig& c) {
  if (zone == world::ZoneRule::Safe) return false;
  if (zone == world::ZoneRule::Contested) return true;
  // Restricted: cả hai bật hung hăng, và người mới được bảo vệ.
  if (a.level < c.newbie_level || t.level < c.newbie_level) return false;
  return a.aggressive && t.aggressive;
}

float bite_damage(const CombatConfig& c, float strength_mod, float defense_mod) {
  // Chênh lệch chỉ số tối đa ±25% (GDD §13) — kẹp để người mới không bị áp đảo.
  const float s = std::clamp(strength_mod, -0.25f, 0.25f);
  const float d = std::clamp(defense_mod, -0.25f, 0.25f);
  return c.bite_damage * (1.0f + s) * (1.0f - d);
}

BiteOutcome request_bite(Combatant& a, Combatant& t, world::ZoneRule zone, const GameplayConfig& cfg, float now) {
  const auto& c = cfg.combat;
  BiteOutcome o;
  auto& ac = *a.combat;
  auto& tc = *t.combat;
  if (a.survival->life != LifeState::Alive) return o.result = BiteResult::TargetDown, o;
  if (ac.stagger > 0) return o.result = BiteResult::Staggered, o;
  if (ac.bite_cd > 0) return o.result = BiteResult::OnCooldown, o;
  if (t.survival->life != LifeState::Alive) return o.result = BiteResult::TargetDown, o;
  if (a.is_player && t.is_player && !pvp_allowed(zone, ac, tc, c)) return o.result = BiteResult::NotAllowed, o;
  if (a.survival->stamina < c.bite_stamina) return o.result = BiteResult::NoStamina, o;
  // Từ đây cú cắn được thực hiện (tốn thể lực + hồi chiêu dù trúng hay trượt)
  spend_stamina(*a.survival, cfg.survival, c.bite_stamina);
  ac.bite_cd = c.bite_cooldown_s;
  ac.since_last_combat = 0;
  const glm::vec2 d(t.pos.x - a.pos.x, t.pos.z - a.pos.z);
  const float dist = glm::length(d);
  if (dist > c.bite_range_m) return o.result = BiteResult::OutOfRange, o;
  const glm::vec2 fwd(std::sin(a.yaw), std::cos(a.yaw));
  if (dist > 1e-3f && glm::dot(fwd, d / dist) < std::cos(glm::radians(c.bite_arc_deg * 0.5f)))
    return o.result = BiteResult::OutOfArc, o;
  if (tc.iframes > 0) return o.result = BiteResult::Dodged, o;
  o.damage = bite_damage(c, 0.0f, 0.0f);
  apply_damage(*t.survival, o.damage);
  tc.since_last_combat = 0;
  if (!t.is_player && a.is_player) ac.notoriety += c.npc_aggression_notoriety;
  tc.recent_hits.push_back(now);
  std::erase_if(tc.recent_hits, [&](float h) { return now - h > c.stagger_window_s; });
  if (static_cast<int>(tc.recent_hits.size()) >= c.stagger_hits) {
    tc.stagger = c.stagger_s;
    tc.recent_hits.clear();
    o.target_staggered = true;
  }
  o.target_downed = t.survival->life != LifeState::Alive;
  o.result = BiteResult::Hit;
  return o;
}

bool request_dodge(Combatant& self, const GameplayConfig& cfg) {
  auto& c = *self.combat;
  if (self.survival->life != LifeState::Alive || c.dodge_cd > 0 || c.stagger > 0) return false;
  if (!spend_stamina(*self.survival, cfg.survival, cfg.combat.dodge_stamina)) return false;
  c.iframes = cfg.combat.dodge_iframes_s;
  c.dodge_cd = cfg.combat.dodge_cooldown_s;
  return true;
}

void tick_combat(CombatState& c, const CombatConfig& cfg, float dt) {
  c.bite_cd = std::max(0.0f, c.bite_cd - dt);
  c.dodge_cd = std::max(0.0f, c.dodge_cd - dt);
  c.iframes = std::max(0.0f, c.iframes - dt);
  c.stagger = std::max(0.0f, c.stagger - dt);
  c.notoriety = std::max(0.0f, c.notoriety - cfg.notoriety_decay_per_s * dt);
  c.since_last_combat += dt;
}

// ============================================================================ hồi sinh
RespawnOutcome respawn(SurvivalState& s, Inventory& inv, glm::vec2 died_at, const world::WorldData& w, const GameplayConfig& cfg) {
  RespawnOutcome o;
  float best = 1e30f;
  bool found = false;
  for (const auto& c : w.chunks)
    for (const auto& p : c.pois) {
      if (p.kind != world::PoiKind::Shelter && p.kind != world::PoiKind::Spawn) continue;
      const glm::vec2 pp(world::to_m(p.x), world::to_m(p.z));
      const float d = glm::distance(pp, died_at);
      if (d < best) {
        best = d;
        o.pos = pp;
        found = true;
      }
    }
  if (!found) o.pos = {world::to_m(w.size_x) * 0.5f, world::to_m(w.size_z) * 0.5f};
  // Rơi tối đa X% đồ TIÊU HAO (làm tròn xuống, deterministic: bớt từ ô cuối lên). Cosmetic giữ nguyên.
  for (int i = static_cast<int>(inv.slots().size()) - 1; i >= 0; --i) {
    const auto& st = inv.slots()[static_cast<size_t>(i)];
    if (st.empty()) continue;
    const ItemDef* d = cfg.item(st.item);
    if (!d || !d->consumable) continue;
    const int drop = static_cast<int>(std::floor(static_cast<float>(st.count) * cfg.survival.respawn_drop_consumable_fraction));
    if (drop > 0 && inv.remove(i, drop)) o.dropped_items += drop;
  }
  s.life = LifeState::Alive;
  s.downed_timer = 0;
  s.hp = cfg.survival.max_hp * cfg.survival.respawn_hp_fraction;
  s.hunger = std::max(s.hunger, cfg.survival.low_threshold + 10);
  s.thirst = std::max(s.thirst, cfg.survival.low_threshold + 10);
  s.stamina = stamina_max(s, cfg.survival);
  return o;
}

HudModel make_hud(const SurvivalState& s, const Inventory& inv, const GameplayConfig& cfg, world::ZoneRule zone) {
  const auto& c = cfg.survival;
  HudModel h;
  h.hp = s.hp / c.max_hp;
  h.hunger = s.hunger / c.max_hunger;
  h.thirst = s.thirst / c.max_thirst;
  h.stamina = s.stamina / c.max_stamina;
  h.rest = s.rest / c.max_rest;
  h.low_hunger = s.hunger < c.low_threshold;
  h.low_thirst = s.thirst < c.low_threshold;
  h.tired = s.rest < c.tired_threshold;
  h.downed = s.life != LifeState::Alive;
  h.downed_left = std::max(0.0f, c.downed_seconds - s.downed_timer);
  h.resting = s.resting;
  h.slots = inv.slots();
  h.zone = world::to_string(zone);
  return h;
}

}  // namespace pb::sim
