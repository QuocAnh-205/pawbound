#pragma once
// Thế giới động (Phase 9): đồng hồ, ánh sáng theo giờ, thời tiết, lịch hoạt động, sự kiện động, lưu trạng thái.
// Logic thuần, deterministic theo seed; KHÔNG phụ thuộc render frame (chỉ nhận dt mô phỏng).
// Dedicated server là nguồn sự thật (Phase 10 đồng bộ giờ/thời tiết/sự kiện xuống client).
#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "core/config_reader.hpp"
#include "sim/gameplay.hpp"
#include "world/world_types.hpp"

namespace pb::sim {

// ============================================================================ đồng hồ
class WorldClock {
 public:
  WorldClock(double day_length_real_s = 2400, double start_hour = 8) : day_len_(day_length_real_s), hour_(start_hour) {}
  void advance(double real_dt);
  void set_hour(double h);
  [[nodiscard]] double hour() const { return hour_; }  // [0, 24)
  [[nodiscard]] int64_t day() const { return day_; }
  [[nodiscard]] double day_length() const { return day_len_; }
  [[nodiscard]] double total_hours() const { return static_cast<double>(day_) * 24.0 + hour_; }
  void restore(int64_t day, double hour) {
    day_ = day;
    set_hour(hour);
  }

 private:
  double day_len_;
  double hour_;
  int64_t day_ = 0;
};

// ============================================================================ thời tiết
enum class Weather : uint8_t { Clear, Cloudy, Rain, Fog, Storm, Count };
const char* to_string(Weather w);
std::optional<Weather> weather_from_string(const std::string& s);

struct WeatherDef {
  float min_s = 300, max_s = 900;
  std::map<Weather, int> next;
  float visibility = 1, scent = 1, move = 1, pedestrians = 1;
};

// Hệ số hiệu lực của thời tiết hiện tại (đã blend khi chuyển trạng thái).
struct WeatherEffects {
  float visibility = 1, scent = 1, move = 1, pedestrians = 1;
  float rain = 0;   // 0..1 cường độ mưa (hiển thị)
  float fog = 0;    // 0..1
  float cloud = 0;  // 0..1
  float storm = 0;  // 0..1 (chớp)
};

// ============================================================================ sự kiện động
enum class EventType : uint8_t { LostPuppy, FoodHotspot, DogConflict, RescueSignal, AreaClosure, Count };
const char* to_string(EventType t);
std::optional<EventType> event_from_string(const std::string& s);

struct EventDef {
  int weight = 1;
  float cooldown_s = 600, duration_s = 300;
  std::vector<world::District> districts;
  float hour_from = 0, hour_to = 24;
};

// Node tài nguyên tạm do sự kiện tạo: 4 bit cao = 0xF (POI thật dùng loại 1..3, xem world generator).
inline constexpr uint32_t kEventNodeTag = 0xF0000000u;
constexpr bool is_event_node(uint32_t id) { return (id >> 28) == 0xFu; }

struct ActiveEvent {
  uint32_t id = 0;
  EventType type = EventType::FoodHotspot;
  glm::vec2 pos{0.0f};
  double started = 0;  // giây mô phỏng
  double ends = 0;
  bool completed = false;
  uint32_t resource_node = 0;  // FoodHotspot: id node tạm (0 = không)
};

struct WorldSimConfig {
  double day_length_real_s = 2400, start_hour = 8;
  std::array<float, 24> pedestrian_by_hour{};
  std::vector<std::pair<float, float>> meal_hours;
  float meal_food_regen_mult = 3;
  uint64_t weather_salt = 7;
  float transition_s = 90;
  std::array<WeatherDef, static_cast<size_t>(Weather::Count)> weather;
  float event_check_s = 45;
  int max_events = 2;
  float event_min_m = 25, event_max_m = 90;
  std::array<EventDef, static_cast<size_t>(EventType::Count)> events;
};
WorldSimConfig load_world_sim_config(const std::filesystem::path& path, ConfigDiagnostics& diag);

// Ánh sáng theo giờ + thời tiết (dữ liệu thuần; client chuyển thành SceneParams).
struct Lighting {
  glm::vec3 sun_dir{0, 1, 0};
  glm::vec3 sun_color{1.0f};
  float sun_intensity = 3;
  glm::vec3 sky{0.55f, 0.65f, 0.8f};
  float ambient = 0.6f;
  float fog_density = 0.0015f;
  float street_lights = 0;  // 0..1
  bool night = false;
};
Lighting lighting_for(double hour, const WeatherEffects& w);

struct WorldSimSnapshot {
  int version = 1;
  int64_t day = 0;
  double hour = 8, sim_time = 0;
  Weather weather = Weather::Clear, prev_weather = Weather::Clear;
  double weather_left = 0, blend_t = 0;
  uint64_t rng = 0;
  double next_event_check = 0;
  uint32_t next_event_id = 1;
  std::array<double, static_cast<size_t>(EventType::Count)> cooldown_until{};
  int last_event = -1;
  std::vector<ActiveEvent> events;
  std::vector<std::pair<uint32_t, int>> resources;  // (node id, remaining)
};

class WorldSim {
 public:
  WorldSim(const WorldSimConfig& cfg, const world::WorldData& world, uint64_t seed);

  // Một tick mô phỏng (giây thật). Trả các sự kiện vừa bắt đầu/kết thúc (để thông báo).
  struct Changes {
    std::vector<ActiveEvent> started, ended;
    bool weather_changed = false;
  };
  Changes tick(double dt, std::span<const glm::vec3> player_positions, std::vector<ResourceNode>& resources);

  [[nodiscard]] const WorldClock& clock() const { return clock_; }
  [[nodiscard]] WorldClock& clock() { return clock_; }
  [[nodiscard]] Weather weather() const { return weather_; }
  [[nodiscard]] WeatherEffects effects() const;
  [[nodiscard]] float pedestrian_density() const;  // giờ × thời tiết
  [[nodiscard]] float food_regen_mult() const;
  [[nodiscard]] const std::vector<ActiveEvent>& events() const { return events_; }
  [[nodiscard]] double sim_time() const { return time_; }

  // Debug/QA: ép thời tiết / sự kiện.
  void force_weather(Weather w, bool instant);
  std::optional<ActiveEvent> force_event(EventType t, glm::vec2 near, std::vector<ResourceNode>& resources);
  bool complete_event(uint32_t id);

  [[nodiscard]] WorldSimSnapshot snapshot(const std::vector<ResourceNode>& resources) const;
  void restore(const WorldSimSnapshot& s, std::vector<ResourceNode>& resources);
  static std::string to_json(const WorldSimSnapshot& s);
  static std::optional<WorldSimSnapshot> from_json(std::string_view text, std::string& error);

 private:
  void pick_next_weather();
  std::optional<glm::vec2> find_event_location(EventType t, std::span<const glm::vec3> players);
  std::optional<ActiveEvent> start_event(EventType t, glm::vec2 pos, std::vector<ResourceNode>& resources);

  const WorldSimConfig& cfg_;
  const world::WorldData& world_;
  WorldClock clock_;
  uint64_t rng_;
  double time_ = 0;
  Weather weather_ = Weather::Clear, prev_ = Weather::Clear;
  double weather_left_ = 0, blend_t_ = 1e9;
  double next_check_;
  uint32_t next_id_ = 1;
  std::array<double, static_cast<size_t>(EventType::Count)> cooldown_until_{};
  int last_event_ = -1;
  std::vector<ActiveEvent> events_;
};

}  // namespace pb::sim
