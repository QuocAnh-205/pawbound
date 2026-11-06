#pragma once
#include <string>
#include <vector>

#include "world/world_types.hpp"

namespace pb::world {

struct WorldIssue {
  std::string category;  // road_connectivity, building_overlap, building_road, prop_obstacle, poi_unreachable, spawn, chunk, hash
  std::string message;
};

struct ValidateOptions {
  std::string expected_hash;  // rỗng = không kiểm tra hash
  int max_issues_per_category = 20;
};

// Trả danh sách lỗi (rỗng = hợp lệ). Không sửa dữ liệu.
std::vector<WorldIssue> validate_world(const WorldData& w, const ValidateOptions& opt = {});

}  // namespace pb::world
