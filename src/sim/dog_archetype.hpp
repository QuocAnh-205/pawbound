#pragma once
// Cấu hình giống chó (data/dogs/breeds.json). Chỉ số gameplay bị giới hạn quanh baseline để
// giống chó không tạo lợi thế lớn; ngoại hình (DogAppearance) là cosmetic thuần, không ảnh hưởng chỉ số.
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

#include "core/config_reader.hpp"

namespace pb::sim {

struct MovementStats {
  float walk_speed = 1.5f;    // m/s
  float trot_speed = 3.5f;
  float run_speed = 6.0f;
  float sprint_speed = 9.0f;
  float accel = 18.0f;        // m/s²
  float decel = 24.0f;
  float turn_rate_deg = 600.0f;
  float jump_speed = 5.0f;    // m/s (≈ 0,6 m)
  float gravity = 20.0f;
  float radius = 0.32f;       // bán kính va chạm (m)
  float step_height = 0.3f;
  float max_slope_deg = 40.0f;
};

struct ModelBinding {
  std::string asset_id;  // id trong assets/manifest.json
  float scale = 1.0f;
  float yaw_offset_deg = 0.0f;  // xoay model để mặt trước trùng +Z
  std::map<std::string, std::string> clips;  // "idle"/"walk"/"run" -> tên clip trong model
  float walk_clip_speed = 1.5f;  // tốc độ (m/s) mà clip walk được hoạt họa
  float run_clip_speed = 6.0f;
};

struct DogBreed {
  std::string id;
  std::string display_name;
  std::string model;   // khóa vào models
  float speed_mod = 1.0f;  // giới hạn [0.95, 1.05]
  float size = 1.0f;       // hình thể (ảnh hưởng bán kính va chạm trong giới hạn)
};

struct DogAppearance {  // cosmetic thuần
  glm::vec3 coat_tint{1.0f};
  float size_variant = 1.0f;  // [0.9, 1.1] chỉ thay đổi hình ảnh
  std::string collar;
};

struct DogCatalog {
  MovementStats baseline;
  std::vector<DogBreed> breeds;
  std::map<std::string, ModelBinding> models;
  std::vector<glm::vec3> coat_colors;
  [[nodiscard]] const DogBreed* find(const std::string& id) const;
};

DogCatalog load_dog_catalog(const std::filesystem::path& path, ConfigDiagnostics& diag);
DogCatalog parse_dog_catalog(std::string_view json_text, ConfigDiagnostics& diag);

// Chỉ số di chuyển cuối cùng: CHỈ phụ thuộc baseline + breed (không nhận appearance — theo thiết kế).
MovementStats movement_for(const DogCatalog& cat, const DogBreed& breed);

}  // namespace pb::sim
