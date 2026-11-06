#pragma once
// Định dạng file thế giới (version, little-endian):
//   <dir>/world.bin          — header "PBWD" + dữ liệu toàn cục (district, terrain, hồ, đường, nav, asset ids)
//   <dir>/chunks/<x>_<z>.bin — header "PBCK" + nhà/props/POI của chunk
//   <dir>/world.json         — manifest đọc được bằng người (version, seed, hash, thống kê)
//   <dir>/logical_hash.txt   — SHA-256 của world.bin + các chunk theo thứ tự chỉ số
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "world/world_types.hpp"

namespace pb::world {

std::vector<uint8_t> serialize_global(const WorldData& w);
std::vector<uint8_t> serialize_chunk(const ChunkData& c);
// Hash logic: không phụ thuộc thời gian sinh hay đường dẫn.
std::string logical_hash(const WorldData& w);

bool save_world(const std::filesystem::path& dir, const WorldData& w, std::string& error);

struct WorldLoad {
  WorldData world;
  std::string stored_hash;  // từ logical_hash.txt
  std::string error;
  [[nodiscard]] bool ok() const { return error.empty(); }
};
// Nạp toàn bộ (generator/validator/server). Client streaming dùng load_chunk.
WorldLoad load_world(const std::filesystem::path& dir);
bool load_chunk(const std::filesystem::path& dir, int cx, int cz, ChunkData& out, std::string& error);
// Giải mã (dùng chung cho test dữ liệu hỏng).
bool deserialize_global(const std::vector<uint8_t>& bytes, WorldData& w, std::string& error);
bool deserialize_chunk(const std::vector<uint8_t>& bytes, ChunkData& c, std::string& error);

}  // namespace pb::world
