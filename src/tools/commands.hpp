#pragma once
#include <string_view>
#include <vector>

namespace pb::tools {
// Nhóm lệnh `pawbound world ...` (Phase 5).
int world_command(std::string_view cmd, const std::vector<std::string_view>& args);
// Nhóm lệnh `pawbound net ...` (Phase 10, công cụ dev).
int net_command(std::string_view cmd, const std::vector<std::string_view>& args);
}  // namespace pb::tools
