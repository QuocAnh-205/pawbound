#include "sim/dog_archetype.hpp"

#include <algorithm>
#include <format>

namespace pb::sim {

const DogBreed* DogCatalog::find(const std::string& id) const {
  for (const auto& b : breeds)
    if (b.id == id) return &b;
  return nullptr;
}

namespace {
DogCatalog from_json(const nlohmann::json& j, ConfigDiagnostics& d) {
  DogCatalog c;
  ConfigReader r(j, "", d);
  auto b = r.child("baseline");
  auto& m = c.baseline;
  b.read("walk_speed", m.walk_speed, 0.5, 5.0);
  b.read("trot_speed", m.trot_speed, 1.0, 8.0);
  b.read("run_speed", m.run_speed, 2.0, 12.0);
  b.read("sprint_speed", m.sprint_speed, 3.0, 16.0);
  b.read("accel", m.accel, 1.0, 100.0);
  b.read("decel", m.decel, 1.0, 100.0);
  b.read("turn_rate_deg", m.turn_rate_deg, 30.0, 2000.0);
  b.read("jump_speed", m.jump_speed, 0.0, 12.0);
  b.read("gravity", m.gravity, 1.0, 50.0);
  b.read("radius", m.radius, 0.1, 1.0);
  b.read("step_height", m.step_height, 0.0, 1.0);
  b.read("max_slope_deg", m.max_slope_deg, 5.0, 80.0);
  if (!(m.walk_speed < m.trot_speed && m.trot_speed < m.run_speed && m.run_speed < m.sprint_speed))
    d.errors.emplace_back("baseline: cần walk < trot < run < sprint");

  if (j.contains("models") && j["models"].is_object()) {
    for (const auto& [key, mj] : j["models"].items()) {
      ModelBinding mb;
      ConfigReader mr(mj, "models." + key, d);
      mr.read("asset_id", mb.asset_id);
      mr.read("scale", mb.scale, 1e-4, 100.0);
      if (mr.has("yaw_offset_deg")) mr.read("yaw_offset_deg", mb.yaw_offset_deg, -360.0, 360.0);
      mr.read("walk_clip_speed", mb.walk_clip_speed, 0.1, 20.0);
      mr.read("run_clip_speed", mb.run_clip_speed, 0.1, 30.0);
      if (mj.contains("clips") && mj["clips"].is_object())
        for (const auto& [ck, cv] : mj["clips"].items())
          if (cv.is_string()) mb.clips[ck] = cv.get<std::string>();
      for (const char* need : {"idle", "walk", "run"})
        if (!mb.clips.contains(need)) d.errors.push_back(std::format("models.{}: thiếu clip '{}'", key, need));
      c.models[key] = mb;
    }
  } else {
    d.errors.emplace_back("thiếu object 'models'");
  }

  if (!j.contains("breeds") || !j["breeds"].is_array() || j["breeds"].empty()) {
    d.errors.emplace_back("cần mảng 'breeds' không rỗng");
    return c;
  }
  size_t i = 0;
  for (const auto& bj : j["breeds"]) {
    DogBreed br;
    ConfigReader brr(bj, std::format("breeds[{}]", i++), d);
    brr.read("id", br.id);
    brr.read("display_name", br.display_name);
    brr.read("model", br.model);
    // Giới hạn cân bằng: giống chó chỉ khác tối đa ±5% tốc độ và ±20% kích thước.
    brr.read("speed_mod", br.speed_mod, 0.95, 1.05);
    brr.read("size", br.size, 0.8, 1.2);
    if (!c.models.contains(br.model)) d.errors.push_back(std::format("breed '{}': model '{}' không tồn tại", br.id, br.model));
    if (c.find(br.id)) d.errors.push_back("breed trùng id: " + br.id);
    c.breeds.push_back(br);
  }
  if (j.contains("coat_colors") && j["coat_colors"].is_array())
    for (const auto& cc : j["coat_colors"])
      if (cc.is_array() && cc.size() == 3) c.coat_colors.emplace_back(cc[0].get<float>(), cc[1].get<float>(), cc[2].get<float>());
  return c;
}
}  // namespace

DogCatalog parse_dog_catalog(std::string_view text, ConfigDiagnostics& d) { return from_json(parse_json_config(text, d), d); }
DogCatalog load_dog_catalog(const std::filesystem::path& p, ConfigDiagnostics& d) {
  return from_json(load_json_config(p, d, true), d);
}

MovementStats movement_for(const DogCatalog& cat, const DogBreed& breed) {
  MovementStats m = cat.baseline;
  const float s = std::clamp(breed.speed_mod, 0.95f, 1.05f);
  m.walk_speed *= s;
  m.trot_speed *= s;
  m.run_speed *= s;
  m.sprint_speed *= s;
  m.radius *= std::clamp(breed.size, 0.8f, 1.2f);
  return m;
}

}  // namespace pb::sim
