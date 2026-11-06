#include "sim/world_sim.hpp"

#include <algorithm>
#include <cmath>
#include <format>

#include <glm/geometric.hpp>
#include <nlohmann/json.hpp>

#include "world/rng.hpp"

namespace pb::sim {

// ============================================================================ chuỗi
const char* to_string(Weather w) {
  static const char* n[] = {"clear", "cloudy", "rain", "fog", "storm"};
  return n[static_cast<size_t>(w)];
}
std::optional<Weather> weather_from_string(const std::string& s) {
  for (int i = 0; i < static_cast<int>(Weather::Count); ++i)
    if (s == to_string(static_cast<Weather>(i))) return static_cast<Weather>(i);
  return std::nullopt;
}
const char* to_string(EventType t) {
  static const char* n[] = {"lost_puppy", "food_hotspot", "dog_conflict", "rescue_signal", "area_closure"};
  return n[static_cast<size_t>(t)];
}
std::optional<EventType> event_from_string(const std::string& s) {
  for (int i = 0; i < static_cast<int>(EventType::Count); ++i)
    if (s == to_string(static_cast<EventType>(i))) return static_cast<EventType>(i);
  return std::nullopt;
}

// ============================================================================ đồng hồ
void WorldClock::advance(double real_dt) {
  if (real_dt <= 0 || day_len_ <= 0) return;
  hour_ += real_dt * 24.0 / day_len_;
  while (hour_ >= 24.0) {
    hour_ -= 24.0;
    ++day_;
  }
}
void WorldClock::set_hour(double h) {
  h = std::fmod(h, 24.0);
  hour_ = h < 0 ? h + 24.0 : h;
}

// ============================================================================ config
WorldSimConfig load_world_sim_config(const std::filesystem::path& path, ConfigDiagnostics& d) {
  WorldSimConfig c;
  c.pedestrian_by_hour.fill(1.0f);
  auto j = load_json_config(path, d, true);
  ConfigReader r(j, "", d);
  auto cl = r.child("clock");
  cl.read("day_length_real_s", c.day_length_real_s, 10, 1e7);
  cl.read("start_hour", c.start_hour, 0, 23.999);
  if (j.contains("pedestrian_density_by_hour") && j["pedestrian_density_by_hour"].is_array() &&
      j["pedestrian_density_by_hour"].size() == 24) {
    for (size_t i = 0; i < 24; ++i) c.pedestrian_by_hour[i] = j["pedestrian_density_by_hour"][i].get<float>();
  } else {
    d.errors.emplace_back("pedestrian_density_by_hour phải là mảng 24 số");
  }
  if (j.contains("meal_hours") && j["meal_hours"].is_array())
    for (const auto& m : j["meal_hours"])
      if (m.is_array() && m.size() == 2) c.meal_hours.emplace_back(m[0].get<float>(), m[1].get<float>());
  r.read("meal_food_regen_mult", c.meal_food_regen_mult, 1, 100);
  auto w = r.child("weather");
  int salt = 7;
  w.read("seed_salt", salt, 0, 1000000);
  c.weather_salt = static_cast<uint64_t>(salt);
  w.read("transition_s", c.transition_s, 1, 3600);
  auto ws = w.child("states");
  for (int i = 0; i < static_cast<int>(Weather::Count); ++i) {
    const auto wt = static_cast<Weather>(i);
    auto s = ws.child(to_string(wt));
    auto& def = c.weather[static_cast<size_t>(i)];
    s.read("min_s", def.min_s, 1, 1e6);
    s.read("max_s", def.max_s, 1, 1e6);
    s.read("visibility", def.visibility, 0.05, 2);
    s.read("scent", def.scent, 0, 3);
    s.read("move", def.move, 0.3, 1.5);
    s.read("pedestrians", def.pedestrians, 0, 2);
    if (def.max_s < def.min_s) d.errors.push_back(std::format("weather.{}: max_s < min_s", to_string(wt)));
    const auto& raw = s.raw();
    if (raw.contains("next") && raw["next"].is_object())
      for (const auto& [k, v] : raw["next"].items()) {
        auto nw = weather_from_string(k);
        if (!nw) d.errors.push_back(std::format("weather.{}.next: '{}' không hợp lệ", to_string(wt), k));
        else def.next[*nw] = v.get<int>();
      }
    if (def.next.empty()) d.errors.push_back(std::format("weather.{}: thiếu 'next'", to_string(wt)));
  }
  auto ev = r.child("events");
  ev.read("check_interval_s", c.event_check_s, 1, 3600);
  ev.read("max_active", c.max_events, 0, 32);
  ev.read("spawn_min_m", c.event_min_m, 0, 1000);
  ev.read("spawn_max_m", c.event_max_m, 1, 2000);
  auto defs = ev.child("defs");
  for (int i = 0; i < static_cast<int>(EventType::Count); ++i) {
    const auto et = static_cast<EventType>(i);
    auto e = defs.child(to_string(et));
    auto& def = c.events[static_cast<size_t>(i)];
    e.read("weight", def.weight, 0, 1000);
    e.read("cooldown_s", def.cooldown_s, 0, 1e7);
    e.read("duration_s", def.duration_s, 1, 1e7);
    const auto& raw = e.raw();
    if (raw.contains("districts") && raw["districts"].is_array())
      for (const auto& dn : raw["districts"]) {
        const std::string s = dn.get<std::string>();
        for (int k = 0; k < world::kDistrictCount; ++k)
          if (s == world::to_string(static_cast<world::District>(k))) def.districts.push_back(static_cast<world::District>(k));
      }
    if (raw.contains("hours") && raw["hours"].is_array() && raw["hours"].size() == 2) {
      def.hour_from = raw["hours"][0].get<float>();
      def.hour_to = raw["hours"][1].get<float>();
    }
  }
  return c;
}

// ============================================================================ ánh sáng
Lighting lighting_for(double hour, const WeatherEffects& w) {
  Lighting l;
  const float t = static_cast<float>((hour - 6.0) / 12.0 * 3.14159265);  // 6h mọc, 18h lặn
  const float elev = std::sin(t);
  const float day = std::clamp(elev * 3.0f, 0.0f, 1.0f);
  const float overcast = std::clamp(w.cloud * 0.6f + w.storm * 0.3f, 0.0f, 0.85f);
  l.night = elev <= 0.0f;
  if (!l.night) {
    l.sun_dir = glm::normalize(glm::vec3(std::cos(t) * 0.8f, std::max(elev, 0.05f), 0.45f));
    l.sun_color = glm::mix(glm::vec3(1.0f, 0.55f, 0.3f), glm::vec3(1.0f, 0.96f, 0.9f), day);
    l.sun_intensity = (3.2f * day + 0.4f) * (1.0f - overcast);
  } else {
    l.sun_dir = glm::normalize(glm::vec3(-0.3f, 0.7f, 0.4f));  // mặt trăng
    l.sun_color = glm::vec3(0.55f, 0.65f, 1.0f);
    l.sun_intensity = 0.35f * (1.0f - overcast);
  }
  const glm::vec3 clear_sky = glm::mix(glm::vec3(0.08f, 0.1f, 0.2f), glm::vec3(0.55f, 0.65f, 0.8f), day);
  const glm::vec3 grey_sky = glm::mix(glm::vec3(0.07f, 0.075f, 0.09f), glm::vec3(0.5f, 0.52f, 0.56f), day);
  l.sky = glm::mix(clear_sky, grey_sky, std::clamp(w.cloud + w.fog * 0.5f, 0.0f, 1.0f));
  l.ambient = (0.25f + 0.45f * day) * (1.0f + 0.25f * w.cloud);  // trời âm u: ánh sáng khuếch tán đều hơn
  l.fog_density = 0.0015f + 0.03f * w.fog + 0.006f * w.rain;
  l.street_lights = std::clamp(1.0f - day + overcast * 0.6f, 0.0f, 1.0f);
  return l;
}

// ============================================================================ WorldSim
namespace {
uint32_t rnd(uint64_t& s) {
  s = world::splitmix64(s);
  return static_cast<uint32_t>(s >> 32);
}
double frnd(uint64_t& s) { return static_cast<double>(rnd(s)) / 4294967296.0; }
}  // namespace

WorldSim::WorldSim(const WorldSimConfig& cfg, const world::WorldData& world, uint64_t seed)
    : cfg_(cfg), world_(world), clock_(cfg.day_length_real_s, cfg.start_hour),
      rng_(world::sub_seed(seed, "world_sim", cfg.weather_salt)), next_check_(cfg.event_check_s) {
  const auto& def = cfg_.weather[static_cast<size_t>(weather_)];
  weather_left_ = def.min_s + frnd(rng_) * (def.max_s - def.min_s);
}

void WorldSim::pick_next_weather() {
  const auto& def = cfg_.weather[static_cast<size_t>(weather_)];
  int total = 0;
  for (const auto& [w, wt] : def.next) total += std::max(wt, 0);
  Weather next = weather_;
  if (total > 0) {
    int r = static_cast<int>(rnd(rng_) % static_cast<uint32_t>(total));
    for (const auto& [w, wt] : def.next) {  // std::map: thứ tự cố định -> deterministic
      r -= std::max(wt, 0);
      if (r < 0) {
        next = w;
        break;
      }
    }
  }
  prev_ = weather_;
  weather_ = next;
  blend_t_ = 0;
  const auto& nd = cfg_.weather[static_cast<size_t>(weather_)];
  weather_left_ = nd.min_s + frnd(rng_) * (nd.max_s - nd.min_s);
}

WeatherEffects WorldSim::effects() const {
  auto one = [](Weather w, const WeatherDef& d) {
    WeatherEffects e;
    e.visibility = d.visibility;
    e.scent = d.scent;
    e.move = d.move;
    e.pedestrians = d.pedestrians;
    e.rain = w == Weather::Rain ? 0.6f : w == Weather::Storm ? 1.0f : 0.0f;
    e.fog = w == Weather::Fog ? 1.0f : 0.0f;
    e.cloud = w == Weather::Cloudy ? 0.6f : w == Weather::Rain ? 0.85f : w == Weather::Storm ? 1.0f : w == Weather::Fog ? 0.5f : 0.0f;
    e.storm = w == Weather::Storm ? 1.0f : 0.0f;
    return e;
  };
  const auto a = one(prev_, cfg_.weather[static_cast<size_t>(prev_)]);
  const auto b = one(weather_, cfg_.weather[static_cast<size_t>(weather_)]);
  const float t = static_cast<float>(std::clamp(blend_t_ / std::max(1.0f, cfg_.transition_s), 0.0, 1.0));
  auto mix = [t](float x, float y) { return x + (y - x) * t; };
  return {mix(a.visibility, b.visibility), mix(a.scent, b.scent), mix(a.move, b.move), mix(a.pedestrians, b.pedestrians),
          mix(a.rain, b.rain), mix(a.fog, b.fog), mix(a.cloud, b.cloud), mix(a.storm, b.storm)};
}

float WorldSim::pedestrian_density() const {
  const double h = clock_.hour();
  const auto i = static_cast<size_t>(h) % 24, j = (i + 1) % 24;
  const float f = static_cast<float>(h - std::floor(h));
  const float by_hour = cfg_.pedestrian_by_hour[i] + (cfg_.pedestrian_by_hour[j] - cfg_.pedestrian_by_hour[i]) * f;
  return by_hour * effects().pedestrians;
}

float WorldSim::food_regen_mult() const {
  const double h = clock_.hour();
  for (const auto& [a, b] : cfg_.meal_hours)
    if (h >= a && h < b) return cfg_.meal_food_regen_mult;
  return 1.0f;
}

std::optional<glm::vec2> WorldSim::find_event_location(EventType t, std::span<const glm::vec3> players) {
  if (players.empty() || world_.nav_nodes.empty()) return std::nullopt;
  const auto& def = cfg_.events[static_cast<size_t>(t)];
  for (int attempt = 0; attempt < 12; ++attempt) {
    const glm::vec3 p = players[rnd(rng_) % players.size()];
    const double a = frnd(rng_) * 6.283185307, d = cfg_.event_min_m + frnd(rng_) * (cfg_.event_max_m - cfg_.event_min_m);
    const glm::vec2 want(p.x + static_cast<float>(std::cos(a) * d), p.z + static_cast<float>(std::sin(a) * d));
    // Bám vào nút nav gần nhất (luôn là chỗ đi được, không trong nhà/hồ)
    float bd = 1e9f;
    glm::vec2 best{};
    for (const auto& n : world_.nav_nodes) {
      const glm::vec2 q(world::to_m(n.x), world::to_m(n.z));
      const float dd = glm::distance(q, want);
      if (dd < bd) {
        bd = dd;
        best = q;
      }
    }
    if (bd > 30.0f) continue;
    const auto dist = static_cast<world::cm>(best.x * 100), distz = static_cast<world::cm>(best.y * 100);
    const auto district = world_.district_at(dist, distz);
    if (!def.districts.empty() && std::find(def.districts.begin(), def.districts.end(), district) == def.districts.end()) continue;
    if (world_.in_pond(dist, distz, 200)) continue;
    bool near_player = false;
    for (const auto& pl : players) near_player |= glm::distance(glm::vec2(pl.x, pl.z), best) < cfg_.event_min_m * 0.5f;
    if (near_player) continue;  // không xuất hiện ngay sát người chơi
    for (const auto& e : events_)
      if (glm::distance(e.pos, best) < 20.0f) near_player = true;
    if (near_player) continue;
    return best;
  }
  return std::nullopt;
}

std::optional<ActiveEvent> WorldSim::start_event(EventType t, glm::vec2 pos, std::vector<ResourceNode>& resources) {
  ActiveEvent e;
  e.id = next_id_++;
  e.type = t;
  e.pos = pos;
  e.started = time_;
  e.ends = time_ + cfg_.events[static_cast<size_t>(t)].duration_s;
  if (t == EventType::FoodHotspot) {
    e.resource_node = kEventNodeTag | e.id;
    resources.push_back({e.resource_node, world::PoiKind::Food, pos, 5, 5, 40, 1e9f, 0});
  }
  cooldown_until_[static_cast<size_t>(t)] = time_ + cfg_.events[static_cast<size_t>(t)].cooldown_s;
  last_event_ = static_cast<int>(t);
  events_.push_back(e);
  return e;
}

WorldSim::Changes WorldSim::tick(double dt, std::span<const glm::vec3> players, std::vector<ResourceNode>& resources) {
  Changes ch;
  time_ += dt;
  clock_.advance(dt);
  // Thời tiết
  blend_t_ += dt;
  weather_left_ -= dt;
  if (weather_left_ <= 0) {
    pick_next_weather();
    ch.weather_changed = true;
  }
  // Tài nguyên: thức ăn hồi nhanh vào giờ ăn
  const float fm = food_regen_mult();
  for (auto& n : resources) {
    if (n.remaining >= n.capacity) {
      n.regen_timer = 0;
      continue;
    }
    n.regen_timer += static_cast<float>(dt) * (n.kind == world::PoiKind::Food ? fm : 1.0f);
    while (n.regen_timer >= n.regen_s && n.remaining < n.capacity) {
      n.regen_timer -= n.regen_s;
      ++n.remaining;
    }
  }
  // Kết thúc sự kiện
  for (auto it = events_.begin(); it != events_.end();) {
    if (it->completed || time_ >= it->ends) {
      ch.ended.push_back(*it);
      if (it->resource_node) std::erase_if(resources, [&](const ResourceNode& r) { return r.id == it->resource_node; });
      it = events_.erase(it);
    } else {
      ++it;
    }
  }
  // Sinh sự kiện mới
  if (time_ >= next_check_) {
    next_check_ = time_ + cfg_.event_check_s;
    if (static_cast<int>(events_.size()) < cfg_.max_events) {
      const double h = clock_.hour();
      std::vector<EventType> eligible;
      int total = 0;
      for (int i = 0; i < static_cast<int>(EventType::Count); ++i) {
        const auto& d = cfg_.events[static_cast<size_t>(i)];
        if (d.weight <= 0 || time_ < cooldown_until_[static_cast<size_t>(i)] || h < d.hour_from || h >= d.hour_to) continue;
        if (i == last_event_) continue;  // chống lặp cùng loại liên tiếp
        bool active = false;
        for (const auto& e : events_) active |= static_cast<int>(e.type) == i;
        if (active) continue;
        eligible.push_back(static_cast<EventType>(i));
        total += d.weight;
      }
      if (total > 0) {
        int r = static_cast<int>(rnd(rng_) % static_cast<uint32_t>(total));
        EventType pick = eligible.front();
        for (auto t : eligible) {
          r -= cfg_.events[static_cast<size_t>(t)].weight;
          if (r < 0) {
            pick = t;
            break;
          }
        }
        if (auto pos = find_event_location(pick, players))
          if (auto e = start_event(pick, *pos, resources)) ch.started.push_back(*e);
      }
    }
  }
  return ch;
}

void WorldSim::force_weather(Weather w, bool instant) {
  prev_ = instant ? w : weather_;
  weather_ = w;
  blend_t_ = instant ? 1e9 : 0;
  const auto& d = cfg_.weather[static_cast<size_t>(w)];
  weather_left_ = d.max_s;
}

std::optional<ActiveEvent> WorldSim::force_event(EventType t, glm::vec2 near, std::vector<ResourceNode>& resources) {
  const glm::vec3 p(near.x, 0, near.y);
  auto pos = find_event_location(t, std::span(&p, 1));
  if (!pos) return std::nullopt;
  return start_event(t, *pos, resources);
}

bool WorldSim::complete_event(uint32_t id) {
  for (auto& e : events_)
    if (e.id == id && !e.completed) {
      e.completed = true;
      return true;
    }
  return false;
}

// ============================================================================ lưu / nạp
WorldSimSnapshot WorldSim::snapshot(const std::vector<ResourceNode>& resources) const {
  WorldSimSnapshot s;
  s.day = clock_.day();
  s.hour = clock_.hour();
  s.sim_time = time_;
  s.weather = weather_;
  s.prev_weather = prev_;
  s.weather_left = weather_left_;
  s.blend_t = blend_t_;
  s.rng = rng_;
  s.next_event_check = next_check_;
  s.next_event_id = next_id_;
  s.cooldown_until = cooldown_until_;
  s.last_event = last_event_;
  s.events = events_;
  for (const auto& r : resources)
    if (r.remaining != r.capacity && !is_event_node(r.id)) s.resources.emplace_back(r.id, r.remaining);
  return s;
}

void WorldSim::restore(const WorldSimSnapshot& s, std::vector<ResourceNode>& resources) {
  clock_.restore(s.day, s.hour);
  time_ = s.sim_time;
  weather_ = s.weather;
  prev_ = s.prev_weather;
  weather_left_ = s.weather_left;
  blend_t_ = s.blend_t;
  rng_ = s.rng;
  next_check_ = s.next_event_check;
  next_id_ = s.next_event_id;
  cooldown_until_ = s.cooldown_until;
  last_event_ = s.last_event;
  events_ = s.events;
  for (const auto& [id, rem] : s.resources)
    for (auto& r : resources)
      if (r.id == id) r.remaining = std::clamp(rem, 0, r.capacity);
  for (const auto& e : events_)  // node tạm của sự kiện điểm thức ăn
    if (e.resource_node) resources.push_back({e.resource_node, world::PoiKind::Food, e.pos, 5, 5, 40, 1e9f, 0});
}

std::string WorldSim::to_json(const WorldSimSnapshot& s) {
  nlohmann::json j;
  j["version"] = s.version;
  j["day"] = s.day;
  j["hour"] = s.hour;
  j["sim_time"] = s.sim_time;
  j["weather"] = to_string(s.weather);
  j["prev_weather"] = to_string(s.prev_weather);
  j["weather_left"] = s.weather_left;
  j["blend_t"] = std::min(s.blend_t, 1e9);
  j["rng"] = std::to_string(s.rng);  // chuỗi: JSON double không giữ đủ 64 bit
  j["next_event_check"] = s.next_event_check;
  j["next_event_id"] = s.next_event_id;
  j["cooldown_until"] = s.cooldown_until;
  j["last_event"] = s.last_event;
  auto ev = nlohmann::json::array();
  for (const auto& e : s.events)
    ev.push_back({{"id", e.id}, {"type", to_string(e.type)}, {"x", e.pos.x}, {"z", e.pos.y}, {"started", e.started},
                  {"ends", e.ends}, {"completed", e.completed}, {"node", e.resource_node}});
  j["events"] = ev;
  auto rs = nlohmann::json::array();
  for (const auto& [id, rem] : s.resources) rs.push_back({id, rem});
  j["resources"] = rs;
  return j.dump(2);
}

std::optional<WorldSimSnapshot> WorldSim::from_json(std::string_view text, std::string& error) {
  auto j = nlohmann::json::parse(text, nullptr, false);
  if (j.is_discarded() || !j.is_object() || j.value("version", 0) != 1) {
    error = "world state JSON không hợp lệ hoặc sai version";
    return std::nullopt;
  }
  try {
    WorldSimSnapshot s;
    s.day = j.at("day").get<int64_t>();
    s.hour = j.at("hour").get<double>();
    s.sim_time = j.at("sim_time").get<double>();
    auto w = weather_from_string(j.at("weather").get<std::string>());
    auto pw = weather_from_string(j.at("prev_weather").get<std::string>());
    if (!w || !pw) throw std::runtime_error("weather không hợp lệ");
    s.weather = *w;
    s.prev_weather = *pw;
    s.weather_left = j.at("weather_left").get<double>();
    s.blend_t = j.at("blend_t").get<double>();
    s.rng = std::stoull(j.at("rng").get<std::string>());
    s.next_event_check = j.at("next_event_check").get<double>();
    s.next_event_id = j.at("next_event_id").get<uint32_t>();
    s.cooldown_until = j.at("cooldown_until").get<decltype(s.cooldown_until)>();
    s.last_event = j.at("last_event").get<int>();
    for (const auto& e : j.at("events")) {
      auto t = event_from_string(e.at("type").get<std::string>());
      if (!t) throw std::runtime_error("event type không hợp lệ");
      s.events.push_back({e.at("id").get<uint32_t>(), *t, {e.at("x").get<float>(), e.at("z").get<float>()},
                          e.at("started").get<double>(), e.at("ends").get<double>(), e.at("completed").get<bool>(),
                          e.at("node").get<uint32_t>()});
    }
    for (const auto& r : j.at("resources")) s.resources.emplace_back(r.at(0).get<uint32_t>(), r.at(1).get<int>());
    if (s.hour < 0 || s.hour >= 24) throw std::runtime_error("hour ngoài khoảng");
    return s;
  } catch (const std::exception& e) {
    error = std::string("world state: ") + e.what();
    return std::nullopt;
  }
}

}  // namespace pb::sim
