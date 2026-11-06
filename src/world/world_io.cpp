#include "world/world_io.hpp"

#include <cstring>
#include <format>
#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "asset/sha256.hpp"

namespace pb::world {
namespace {

// Kiểu số nguyên không dấu tương ứng (enum -> underlying), tính lười để không đụng underlying_type<int>.
template <class T, bool = std::is_enum_v<T>>
struct raw_int {
  using type = std::make_unsigned_t<T>;
};
template <class T>
struct raw_int<T, true> {
  using type = std::make_unsigned_t<std::underlying_type_t<T>>;
};
template <class T>
using raw_int_t = typename raw_int<T>::type;

class Writer {
 public:
  std::vector<uint8_t> out;
  template <class T>
  void put(T v) {
    static_assert(std::is_integral_v<T> || std::is_enum_v<T>);
    using U = raw_int_t<T>;
    const auto u = static_cast<U>(v);
    for (size_t i = 0; i < sizeof(U); ++i) out.push_back(static_cast<uint8_t>(u >> (8 * i)));  // little-endian
  }
  void str(const std::string& s) {
    put(static_cast<uint32_t>(s.size()));
    out.insert(out.end(), s.begin(), s.end());
  }
  void magic(const char* m) { out.insert(out.end(), m, m + 4); }
};

class Reader {
 public:
  explicit Reader(const std::vector<uint8_t>& b) : b_(b) {}
  template <class T>
  T get() {
    using U = raw_int_t<T>;
    need(sizeof(U));
    U u = 0;
    for (size_t i = 0; i < sizeof(U); ++i) u |= static_cast<U>(static_cast<U>(b_[pos_ + i]) << (8 * i));
    pos_ += sizeof(U);
    return static_cast<T>(u);
  }
  std::string str() {
    const auto n = get<uint32_t>();
    need(n);
    std::string s(reinterpret_cast<const char*>(b_.data() + pos_), n);
    pos_ += n;
    return s;
  }
  // Số phần tử: chặn giá trị phi lý (file hỏng) trước khi cấp phát.
  uint32_t count(size_t min_bytes_each) {
    const auto n = get<uint32_t>();
    if (min_bytes_each > 0 && static_cast<uint64_t>(n) * min_bytes_each > b_.size() - pos_)
      throw std::runtime_error(std::format("số phần tử {} vượt kích thước dữ liệu", n));
    return n;
  }
  void magic(const char* m) {
    need(4);
    if (std::memcmp(b_.data() + pos_, m, 4) != 0) throw std::runtime_error(std::format("sai magic, cần '{}'", m));
    pos_ += 4;
  }
  [[nodiscard]] bool done() const { return pos_ == b_.size(); }

 private:
  void need(size_t n) const {
    if (pos_ + n > b_.size()) throw std::runtime_error("dữ liệu bị cắt cụt");
  }
  const std::vector<uint8_t>& b_;
  size_t pos_ = 0;
};

bool read_file(const std::filesystem::path& p, std::vector<uint8_t>& out) {
  std::ifstream in(p, std::ios::binary);
  if (!in) return false;
  out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  return true;
}
bool write_file(const std::filesystem::path& p, const void* data, size_t n) {
  std::ofstream out(p, std::ios::binary);
  if (!out) return false;
  out.write(static_cast<const char*>(data), static_cast<std::streamsize>(n));
  return out.good();
}
std::filesystem::path chunk_path(const std::filesystem::path& dir, int cx, int cz) {
  return dir / "chunks" / std::format("{}_{}.bin", cx, cz);
}
}  // namespace

std::vector<uint8_t> serialize_global(const WorldData& w) {
  Writer o;
  o.magic("PBWD");
  o.put(w.format_version);
  o.put(w.generator_version);
  o.put(w.seed);
  o.put(w.size_x);
  o.put(w.size_z);
  o.put(w.chunk_size);
  o.put(static_cast<int32_t>(w.chunks_x));
  o.put(static_cast<int32_t>(w.chunks_z));
  o.put(w.district_cell);
  o.put(static_cast<int32_t>(w.dcells_x));
  o.put(static_cast<int32_t>(w.dcells_z));
  o.put(static_cast<uint32_t>(w.districts.size()));
  for (auto d : w.districts) o.put(d);
  for (auto z : w.zone_rules) o.put(z);
  o.put(static_cast<int32_t>(w.terrain.cells_x));
  o.put(static_cast<int32_t>(w.terrain.cells_z));
  o.put(w.terrain.cell);
  o.put(static_cast<uint32_t>(w.terrain.height_cm.size()));
  for (auto h : w.terrain.height_cm) o.put(h);
  o.put(static_cast<uint32_t>(w.ponds.size()));
  for (const auto& p : w.ponds) {
    o.put(p.x);
    o.put(p.z);
    o.put(p.radius);
    o.put(p.level);
  }
  o.put(static_cast<uint32_t>(w.road_nodes.size()));
  for (const auto& n : w.road_nodes) {
    o.put(n.x);
    o.put(n.z);
  }
  o.put(static_cast<uint32_t>(w.road_edges.size()));
  for (const auto& e : w.road_edges) {
    o.put(e.a);
    o.put(e.b);
    o.put(e.cls);
    o.put(e.half_width);
    o.put(e.sidewalk);
  }
  o.put(static_cast<uint32_t>(w.nav_nodes.size()));
  for (const auto& n : w.nav_nodes) {
    o.put(n.x);
    o.put(n.z);
  }
  o.put(static_cast<uint32_t>(w.nav_edges.size()));
  for (const auto& e : w.nav_edges) {
    o.put(e.a);
    o.put(e.b);
  }
  o.put(static_cast<uint32_t>(w.asset_ids.size()));
  for (const auto& s : w.asset_ids) o.str(s);
  return o.out;
}

std::vector<uint8_t> serialize_chunk(const ChunkData& c) {
  Writer o;
  o.magic("PBCK");
  o.put(kWorldFormatVersion);
  o.put(static_cast<int32_t>(c.cx));
  o.put(static_cast<int32_t>(c.cz));
  o.put(static_cast<uint32_t>(c.buildings.size()));
  for (const auto& b : c.buildings) {
    o.put(b.id);
    o.put(b.x);
    o.put(b.z);
    o.put(b.half_x);
    o.put(b.half_z);
    o.put(b.height);
    o.put(b.rot);
    o.put(b.district);
    o.put(b.asset);
  }
  o.put(static_cast<uint32_t>(c.props.size()));
  for (const auto& p : c.props) {
    o.put(p.id);
    o.put(p.x);
    o.put(p.z);
    o.put(p.yaw_deg);
    o.put(p.kind);
    o.put(p.scale_pct);
    o.put(p.asset);
  }
  o.put(static_cast<uint32_t>(c.pois.size()));
  for (const auto& p : c.pois) {
    o.put(p.id);
    o.put(p.x);
    o.put(p.z);
    o.put(p.kind);
    o.put(p.district);
  }
  return o.out;
}

std::string logical_hash(const WorldData& w) {
  asset::Sha256 h;
  const auto g = serialize_global(w);
  h.update(g);
  for (const auto& c : w.chunks) h.update(serialize_chunk(c));
  return h.finish_hex();
}

bool deserialize_global(const std::vector<uint8_t>& bytes, WorldData& w, std::string& error) {
  try {
    Reader r(bytes);
    r.magic("PBWD");
    w.format_version = r.get<uint32_t>();
    if (w.format_version != kWorldFormatVersion)
      throw std::runtime_error(std::format("format_version {} không hỗ trợ (cần {})", w.format_version, kWorldFormatVersion));
    w.generator_version = r.get<uint32_t>();
    w.seed = r.get<uint64_t>();
    w.size_x = r.get<cm>();
    w.size_z = r.get<cm>();
    w.chunk_size = r.get<cm>();
    w.chunks_x = r.get<int32_t>();
    w.chunks_z = r.get<int32_t>();
    w.district_cell = r.get<cm>();
    w.dcells_x = r.get<int32_t>();
    w.dcells_z = r.get<int32_t>();
    if (w.chunks_x <= 0 || w.chunks_z <= 0 || w.chunks_x > 4096 || w.chunks_z > 4096 || w.dcells_x <= 0 ||
        w.dcells_z <= 0 || w.chunk_size <= 0 || w.district_cell <= 0)
      throw std::runtime_error("kích thước lưới không hợp lệ");
    w.districts.resize(r.count(1));
    for (auto& d : w.districts) {
      d = r.get<uint8_t>();
      if (d >= kDistrictCount) throw std::runtime_error("district id không hợp lệ");
    }
    if (w.districts.size() != static_cast<size_t>(w.dcells_x) * w.dcells_z) throw std::runtime_error("số ô district sai");
    for (auto& z : w.zone_rules) z = r.get<ZoneRule>();
    w.terrain.cells_x = r.get<int32_t>();
    w.terrain.cells_z = r.get<int32_t>();
    w.terrain.cell = r.get<cm>();
    w.terrain.height_cm.resize(r.count(2));
    for (auto& h : w.terrain.height_cm) h = r.get<int16_t>();
    if (w.terrain.height_cm.size() != static_cast<size_t>(w.terrain.cells_x + 1) * (w.terrain.cells_z + 1))
      throw std::runtime_error("số đỉnh địa hình sai");
    w.ponds.resize(r.count(16));
    for (auto& p : w.ponds) {
      p.x = r.get<cm>();
      p.z = r.get<cm>();
      p.radius = r.get<cm>();
      p.level = r.get<cm>();
    }
    w.road_nodes.resize(r.count(8));
    for (auto& n : w.road_nodes) {
      n.x = r.get<cm>();
      n.z = r.get<cm>();
    }
    w.road_edges.resize(r.count(17));
    for (auto& e : w.road_edges) {
      e.a = r.get<uint32_t>();
      e.b = r.get<uint32_t>();
      e.cls = r.get<RoadClass>();
      e.half_width = r.get<cm>();
      e.sidewalk = r.get<cm>();
      if (e.a >= w.road_nodes.size() || e.b >= w.road_nodes.size()) throw std::runtime_error("cạnh đường trỏ nút không tồn tại");
    }
    w.nav_nodes.resize(r.count(8));
    for (auto& n : w.nav_nodes) {
      n.x = r.get<cm>();
      n.z = r.get<cm>();
    }
    w.nav_edges.resize(r.count(8));
    for (auto& e : w.nav_edges) {
      e.a = r.get<uint32_t>();
      e.b = r.get<uint32_t>();
      if (e.a >= w.nav_nodes.size() || e.b >= w.nav_nodes.size()) throw std::runtime_error("cạnh nav trỏ nút không tồn tại");
    }
    w.asset_ids.resize(r.count(4));
    for (auto& s : w.asset_ids) s = r.str();
    if (!r.done()) throw std::runtime_error("dư dữ liệu cuối file");
  } catch (const std::exception& e) {
    error = std::string("world.bin: ") + e.what();
    return false;
  }
  return true;
}

bool deserialize_chunk(const std::vector<uint8_t>& bytes, ChunkData& c, std::string& error) {
  try {
    Reader r(bytes);
    r.magic("PBCK");
    if (r.get<uint32_t>() != kWorldFormatVersion) throw std::runtime_error("chunk version không hỗ trợ");
    c.cx = r.get<int32_t>();
    c.cz = r.get<int32_t>();
    c.buildings.resize(r.count(28));
    for (auto& b : c.buildings) {
      b.id = r.get<uint32_t>();
      b.x = r.get<cm>();
      b.z = r.get<cm>();
      b.half_x = r.get<cm>();
      b.half_z = r.get<cm>();
      b.height = r.get<cm>();
      b.rot = r.get<uint8_t>();
      b.district = r.get<District>();
      b.asset = r.get<uint16_t>();
    }
    c.props.resize(r.count(18));
    for (auto& p : c.props) {
      p.id = r.get<uint32_t>();
      p.x = r.get<cm>();
      p.z = r.get<cm>();
      p.yaw_deg = r.get<uint16_t>();
      p.kind = r.get<PropKind>();
      p.scale_pct = r.get<uint8_t>();
      p.asset = r.get<uint16_t>();
      if (static_cast<int>(p.kind) >= static_cast<int>(PropKind::Count)) throw std::runtime_error("prop kind không hợp lệ");
    }
    c.pois.resize(r.count(14));
    for (auto& p : c.pois) {
      p.id = r.get<uint32_t>();
      p.x = r.get<cm>();
      p.z = r.get<cm>();
      p.kind = r.get<PoiKind>();
      p.district = r.get<District>();
      if (static_cast<int>(p.kind) >= static_cast<int>(PoiKind::Count)) throw std::runtime_error("poi kind không hợp lệ");
    }
    if (!r.done()) throw std::runtime_error("dư dữ liệu cuối chunk");
  } catch (const std::exception& e) {
    error = std::format("chunk: {}", e.what());
    return false;
  }
  return true;
}

bool save_world(const std::filesystem::path& dir, const WorldData& w, std::string& error) {
  std::error_code ec;
  std::filesystem::create_directories(dir / "chunks", ec);
  if (ec) {
    error = "không tạo được thư mục: " + ec.message();
    return false;
  }
  const auto g = serialize_global(w);
  if (!write_file(dir / "world.bin", g.data(), g.size())) {
    error = "không ghi được world.bin";
    return false;
  }
  for (const auto& c : w.chunks) {
    const auto b = serialize_chunk(c);
    if (!write_file(chunk_path(dir, c.cx, c.cz), b.data(), b.size())) {
      error = "không ghi được chunk";
      return false;
    }
  }
  const std::string hash = logical_hash(w);
  if (!write_file(dir / "logical_hash.txt", hash.data(), hash.size())) {
    error = "không ghi được logical_hash.txt";
    return false;
  }
  nlohmann::ordered_json j;
  j["format_version"] = w.format_version;
  j["generator_version"] = w.generator_version;
  j["seed"] = w.seed;
  j["config_hash"] = w.config_hash;
  j["logical_hash"] = hash;
  j["size_m"] = {to_m(w.size_x), to_m(w.size_z)};
  j["chunk_size_m"] = to_m(w.chunk_size);
  j["chunks"] = {w.chunks_x, w.chunks_z};
  j["stats"] = {{"road_nodes", w.stats.road_nodes}, {"road_edges", w.stats.road_edges}, {"blocks", w.stats.blocks},
                {"lots", w.stats.lots},           {"buildings", w.stats.buildings},   {"props", w.stats.props},
                {"pois", w.stats.pois},           {"nav_nodes", w.stats.nav_nodes},   {"ponds", w.stats.ponds},
                {"generate_ms", w.stats.generate_ms}};
  j["asset_ids"] = w.asset_ids;
  const std::string js = j.dump(2) + "\n";
  if (!write_file(dir / "world.json", js.data(), js.size())) {
    error = "không ghi được world.json";
    return false;
  }
  return true;
}

WorldLoad load_world(const std::filesystem::path& dir) {
  WorldLoad r;
  std::vector<uint8_t> g;
  if (!read_file(dir / "world.bin", g)) {
    r.error = "không đọc được " + (dir / "world.bin").string();
    return r;
  }
  if (!deserialize_global(g, r.world, r.error)) return r;
  r.world.chunks.resize(static_cast<size_t>(r.world.chunks_x * r.world.chunks_z));
  for (int cz = 0; cz < r.world.chunks_z; ++cz)
    for (int cx = 0; cx < r.world.chunks_x; ++cx)
      if (!load_chunk(dir, cx, cz, r.world.chunks[static_cast<size_t>(r.world.chunk_index(cx, cz))], r.error)) return r;
  std::vector<uint8_t> h;
  if (read_file(dir / "logical_hash.txt", h)) r.stored_hash.assign(h.begin(), h.end());
  // Thống kê đọc lại từ dữ liệu
  auto& s = r.world.stats;
  s.road_nodes = static_cast<uint32_t>(r.world.road_nodes.size());
  s.road_edges = static_cast<uint32_t>(r.world.road_edges.size());
  s.nav_nodes = static_cast<uint32_t>(r.world.nav_nodes.size());
  s.ponds = static_cast<uint32_t>(r.world.ponds.size());
  for (const auto& c : r.world.chunks) {
    s.buildings += static_cast<uint32_t>(c.buildings.size());
    s.props += static_cast<uint32_t>(c.props.size());
    s.pois += static_cast<uint32_t>(c.pois.size());
  }
  return r;
}

bool load_chunk(const std::filesystem::path& dir, int cx, int cz, ChunkData& out, std::string& error) {
  std::vector<uint8_t> b;
  const auto p = chunk_path(dir, cx, cz);
  if (!read_file(p, b)) {
    error = "không đọc được " + p.string();
    return false;
  }
  if (!deserialize_chunk(b, out, error)) {
    error = p.string() + ": " + error;
    return false;
  }
  if (out.cx != cx || out.cz != cz) {
    error = std::format("{}: tọa độ chunk trong file ({},{}) không khớp tên file", p.string(), out.cx, out.cz);
    return false;
  }
  return true;
}

}  // namespace pb::world
