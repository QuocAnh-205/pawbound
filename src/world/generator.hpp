#pragma once
#include "world/world_config.hpp"
#include "world/world_types.hpp"

namespace pb::world {

// Sinh toàn bộ dữ liệu logic từ config + seed. Deterministic: cùng (generator_version, seed, config)
// -> cùng WorldData (cùng logical hash). Không phụ thuộc thời gian/đa luồng.
WorldData generate_world(const WorldConfig& cfg);

// Noise giá trị fixed-point (dùng chung cho test). Trả giá trị trong [-32768, 32767].
int32_t value_noise_fixed(uint64_t seed, cm x, cm z, cm period);
int32_t fbm_fixed(uint64_t seed, cm x, cm z, cm period, int octaves);

}  // namespace pb::world
