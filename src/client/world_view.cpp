#include "client/world_view.hpp"

#include <algorithm>
#include <cmath>
#include <format>

#include <glm/gtc/matrix_transform.hpp>

#include "client/mesh_batcher.hpp"
#include "core/log.hpp"
#include "geom/primitives.hpp"
#include "world/world_io.hpp"

namespace pb::client {
namespace {
using world::to_m;

// Mã màu hex là sRGB; shader tính ánh sáng trong không gian tuyến tính -> phải chuyển đổi.
float srgb_to_linear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
glm::vec4 rgb(uint32_t c) {
  return {srgb_to_linear(static_cast<float>((c >> 16) & 255) / 255.0f),
          srgb_to_linear(static_cast<float>((c >> 8) & 255) / 255.0f), srgb_to_linear(static_cast<float>(c & 255) / 255.0f),
          1.0f};
}
glm::mat4 T(glm::vec3 p) { return glm::translate(glm::mat4(1.0f), p); }
glm::mat4 RY(float rad) { return glm::rotate(glm::mat4(1.0f), rad, glm::vec3(0, 1, 0)); }
glm::mat4 S(glm::vec3 s) { return glm::scale(glm::mat4(1.0f), s); }

// Băm số nguyên -> [0,1) (tất định, không phụ thuộc thứ tự nạp chunk)
float h01(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  x ^= x >> 16;
  return static_cast<float>(x & 0xFFFFFF) / 16777216.0f;
}
float hash2(int x, int z, uint32_t seed) { return h01(static_cast<uint32_t>(x) * 73856093u ^ static_cast<uint32_t>(z) * 19349663u ^ seed); }
// Value noise mượt (cho màu cỏ, đồi)
float vnoise(float x, float z, float scale, uint32_t seed) {
  x /= scale;
  z /= scale;
  const int ix = static_cast<int>(std::floor(x)), iz = static_cast<int>(std::floor(z));
  const float fx = x - static_cast<float>(ix), fz = z - static_cast<float>(iz);
  const float ux = fx * fx * (3 - 2 * fx), uz = fz * fz * (3 - 2 * fz);
  const float a = hash2(ix, iz, seed), b = hash2(ix + 1, iz, seed), c = hash2(ix, iz + 1, seed), d = hash2(ix + 1, iz + 1, seed);
  return (a + (b - a) * ux) + ((c + (d - c) * ux) - (a + (b - a) * ux)) * uz;
}

// Màu cỏ có biến thiên (xanh tươi ↔ vàng xanh ↔ đậm) — thay cho màu phẳng "nhựa"
glm::vec4 grass_color(float x, float z, world::District d) {
  const float n1 = vnoise(x, z, 18.0f, 11), n2 = vnoise(x, z, 5.0f, 23);
  glm::vec4 c = glm::mix(rgb(0x4E9A2E), rgb(0x86C042), n1);
  c = glm::mix(c, rgb(0x3D7F26), std::max(0.0f, n2 - 0.55f) * 1.6f);
  if (d == world::District::Industrial) c = glm::mix(c, rgb(0x9C8A62), 0.45f);
  if (d == world::District::Downtown) c = glm::mix(c, rgb(0x7DAE4A), 0.25f);
  return c;
}

// Ảnh nhiễu xám mịn, lặp liền mạch: làm mặt đất/đường có chất liệu thay vì ô cờ
geom::ImageRGBA8 make_noise_texture(int size, uint32_t seed, int lo) {
  geom::ImageRGBA8 img;
  img.width = img.height = size;
  img.pixels.resize(static_cast<size_t>(size * size * 4));
  for (int y = 0; y < size; ++y)
    for (int x = 0; x < size; ++x) {
      float v = 0, a = 0.5f;
      for (int o = 0; o < 4; ++o) {
        const int p = 4 << o;  // chu kỳ lặp chia hết size -> liền mạch
        const float fx = static_cast<float>(x) * static_cast<float>(p) / static_cast<float>(size);
        const float fz = static_cast<float>(y) * static_cast<float>(p) / static_cast<float>(size);
        const int ix = static_cast<int>(fx), iz = static_cast<int>(fz);
        const float tx = fx - static_cast<float>(ix), tz = fz - static_cast<float>(iz);
        auto g = [&](int i, int j) { return hash2(i % p, j % p, seed + static_cast<uint32_t>(o)); };
        const float n = (g(ix, iz) * (1 - tx) + g(ix + 1, iz) * tx) * (1 - tz) + (g(ix, iz + 1) * (1 - tx) + g(ix + 1, iz + 1) * tx) * tz;
        v += n * a;
        a *= 0.5f;
      }
      const auto c = static_cast<uint8_t>(lo + static_cast<int>(v / 0.9375f * static_cast<float>(255 - lo)));
      uint8_t* px = &img.pixels[static_cast<size_t>(y * size + x) * 4];
      px[0] = px[1] = px[2] = c;
      px[3] = 255;
    }
  return img;
}

glm::vec4 district_ground(world::District d) {
  switch (d) {
    case world::District::Downtown: return rgb(0x8C9A7A);
    case world::District::Residential: return rgb(0x7FA05A);
    case world::District::Park: return rgb(0x6FA34E);
    case world::District::Industrial: return rgb(0x9A8F78);
  }
  return glm::vec4(1.0f);
}
glm::vec4 building_color(const world::Building& b) {
  static const uint32_t pal[] = {0xE8DCC2, 0xD9B99B, 0xE3D3A8, 0xC9D6DF, 0xE7C9C0};
  return rgb(pal[((b.id * 2654435761u) >> 16) % 5]);
}

// Hướng ngoài của 4 mặt tường (0:+Z, 1:+X, 2:-Z, 3:-X) — khớp quy ước Building::rot
const glm::vec2 kSide[4] = {{0, 1}, {1, 0}, {0, -1}, {-1, 0}};
}  // namespace

WorldView::WorldView(render::Renderer& r, world::WorldData w, std::filesystem::path dir, const std::filesystem::path& asset_root)
    : r_(r),
      world_(std::move(w)),
      dir_(std::move(dir)),
      streamer_(world_.chunks_x, world_.chunks_z, to_m(world_.chunk_size), 3, 4) {  // 3D: tầm nhìn ~190 m
  tex_ground_ = r_.upload_texture(make_noise_texture(256, 7, 196), true, "world/ground_noise");
  tex_road_ = r_.upload_texture(make_noise_texture(256, 31, 170), true, "world/road_noise");
  tex_wall_ = tex_ground_;
  kit_ = std::make_unique<ModelLibrary>(r_, asset_root);
  have_kit_ = kit("cc0_village_wall_plaster_straight") != nullptr && kit("cc0_nature_commontree_1") != nullptr;
  if (!have_kit_) log::warn("world", "không có asset CC0 (assets/cc0) — dùng hình học đơn giản");
  build_global();
  if (have_kit_) build_horizon();
}

WorldView::~WorldView() {
  for (auto& [i, c] : chunks_)
    for (auto m : c.meshes) r_.release_mesh(m);
  for (auto m : global_meshes_) r_.release_mesh(m);
}

const LoadedModel* WorldView::kit(const char* id) { return kit_ ? kit_->get(id) : nullptr; }

glm::vec3 WorldView::spawn_point() const {
  for (const auto& c : world_.chunks)
    for (const auto& p : c.pois)
      if (p.kind == world::PoiKind::Spawn) return {to_m(p.x), world_.height_at(to_m(p.x), to_m(p.z)), to_m(p.z)};
  return {to_m(world_.size_x) / 2, 0, to_m(world_.size_z) / 2};
}

const world::ChunkData* WorldView::chunk(int index) const {
  auto it = chunks_.find(index);
  return it == chunks_.end() ? nullptr : &it->second.data;
}

void WorldView::build_global() {
  // Đường (đá lát / đường đất kiểu làng) + lối đi: một mesh, bám địa hình (chia đoạn ≤ 4 m).
  geom::MeshData roads;
  auto quad_strip = [&](glm::vec2 a, glm::vec2 b, float half, float lift, glm::vec4 color) {
    const glm::vec2 dir = glm::normalize(b - a);
    const glm::vec2 n(-dir.y, dir.x);
    const float len = glm::length(b - a);
    const int seg = std::max(1, static_cast<int>(std::ceil(len / 4.0f)));
    for (int i = 0; i < seg; ++i) {
      const glm::vec2 p0 = a + dir * (len * static_cast<float>(i) / static_cast<float>(seg));
      const glm::vec2 p1 = a + dir * (len * static_cast<float>(i + 1) / static_cast<float>(seg));
      const glm::vec2 c[4] = {p0 - n * half, p0 + n * half, p1 + n * half, p1 - n * half};
      const auto base = static_cast<uint32_t>(roads.vertices.size());
      for (const auto& q : c) {
        geom::Vertex v;
        v.position = {q.x, world_.height_at(q.x, q.y) + lift, q.y};
        v.uv = q * 0.25f;
        v.color = color * (0.9f + 0.2f * vnoise(q.x, q.y, 3.0f, 5));
        v.color.a = 1.0f;
        roads.vertices.push_back(v);
      }
      const glm::vec3 e1 = roads.vertices[base + 1].position - roads.vertices[base].position;
      const glm::vec3 e2 = roads.vertices[base + 2].position - roads.vertices[base].position;
      if (glm::cross(e1, e2).y > 0) roads.indices.insert(roads.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
      else roads.indices.insert(roads.indices.end(), {base, base + 2, base + 1, base, base + 3, base + 2});
    }
  };
  for (const auto& e : world_.road_edges) {
    const auto& A = world_.road_nodes[e.a];
    const auto& B = world_.road_nodes[e.b];
    const glm::vec2 a(to_m(A.x), to_m(A.z)), b(to_m(B.x), to_m(B.z));
    if (e.cls == world::RoadClass::Path) {
      quad_strip(a, b, to_m(e.half_width), 0.04f, rgb(0xC9A878));  // đường đất
      continue;
    }
    const glm::vec2 d = glm::normalize(b - a);
    const float ext = to_m(e.half_width + e.sidewalk);
    quad_strip(a - d * ext, b + d * ext, to_m(e.half_width + e.sidewalk), 0.06f, rgb(0xB8AE9C));  // lề đá
    quad_strip(a - d * to_m(e.half_width), b + d * to_m(e.half_width), to_m(e.half_width), 0.09f,
               e.cls == world::RoadClass::Arterial ? rgb(0x8E8172) : rgb(0xA08C70));  // đá lát / đất nện
  }
  for (auto& v : roads.vertices) v.normal = {0, 1, 0};
  roads.recompute_bounds();
  roads.generate_tangents();
  auto road_mesh = r_.upload_mesh(roads, "world/roads");
  global_meshes_.push_back(road_mesh);
  render::Material rm;
  rm.base_tex = tex_road_;
  rm.roughness = 0.95f;
  rm.cutaway = false;
  global_items_.push_back({road_mesh, glm::mat4(1.0f), rm, false});

  // Mặt nước các hồ: sóng + phản chiếu bầu trời
  for (size_t i = 0; i < world_.ponds.size(); ++i) {
    const auto& p = world_.ponds[i];
    auto disc = geom::make_cylinder(to_m(p.radius) + 1.5f, 0.02f, 48, true);
    auto mh = r_.upload_mesh(disc, std::format("world/pond{}", i));
    global_meshes_.push_back(mh);
    render::Material water{rgb(0x2E7FA0), 0.0f, 0.05f};
    water.cutaway = false;
    water.water = true;
    global_items_.push_back({mh, glm::translate(glm::mat4(1.0f), glm::vec3(to_m(p.x), to_m(p.level), to_m(p.z))), water, false});
  }
}

// Đồi + bờ biển quanh rìa thế giới (chỉ để nhìn — ngoài vùng chơi, không có va chạm)
void WorldView::build_horizon() {
  const float W = to_m(world_.size_x), D = to_m(world_.size_z);
  const float ext = 900.0f, step = 12.0f;
  auto edge_dist = [&](float x, float z) {
    const float dx = std::max({0.0f, -x, x - W}), dz = std::max({0.0f, -z, z - D});
    return std::sqrt(dx * dx + dz * dz);
  };
  auto height = [&](float x, float z) {
    const float d = edge_dist(x, z);
    const float base = world_.height_at(std::clamp(x, 0.0f, W), std::clamp(z, 0.0f, D));
    if (x > W + 30.0f) {  // phía đông: dốc xuống biển, có vách đá
      const float t = std::clamp((x - W - 30.0f) / 60.0f, 0.0f, 1.0f);
      return glm::mix(base + 6.0f * vnoise(x, z, 40, 3), -8.0f, t * t);
    }
    const float ramp = std::clamp(d / 140.0f, 0.0f, 1.0f);
    const float hills = vnoise(x, z, 90.0f, 1) * 45.0f + vnoise(x, z, 30.0f, 2) * 14.0f;
    return base + hills * ramp * ramp + d * 0.04f;
  };
  geom::MeshData m;
  const int nx = static_cast<int>((W + 2 * ext) / step), nz = static_cast<int>((D + 2 * ext) / step);
  for (int j = 0; j <= nz; ++j)
    for (int i = 0; i <= nx; ++i) {
      const float x = -ext + static_cast<float>(i) * step, z = -ext + static_cast<float>(j) * step;
      geom::Vertex v;
      v.position = {x, height(x, z) - (edge_dist(x, z) < 1.0f ? 0.4f : 0.0f), z};
      const float hx = height(x + 1, z) - height(x - 1, z), hz = height(x, z + 1) - height(x, z - 1);
      v.normal = glm::normalize(glm::vec3(-hx * 0.5f, 1.0f, -hz * 0.5f));
      v.uv = glm::vec2(x, z) * 0.12f;
      const float slope = 1.0f - v.normal.y;
      glm::vec4 c = grass_color(x, z, world::District::Park);
      c = glm::mix(c, rgb(0x8A7D6B), std::clamp((slope - 0.25f) * 3.0f, 0.0f, 1.0f));      // vách đá
      if (v.position.y < 1.2f && x > W) c = glm::mix(c, rgb(0xE3D3A4), 0.9f);               // bãi cát
      v.color = c;
      m.vertices.push_back(v);
    }
  const auto row = static_cast<uint32_t>(nx + 1);
  for (int j = 0; j < nz; ++j)
    for (int i = 0; i < nx; ++i) {
      const float cx = -ext + (static_cast<float>(i) + 0.5f) * step, cz = -ext + (static_cast<float>(j) + 0.5f) * step;
      if (cx > step && cx < W - step && cz > step && cz < D - step) continue;  // trong vùng chơi: địa hình chunk lo
      const uint32_t a = static_cast<uint32_t>(j) * row + static_cast<uint32_t>(i);
      m.indices.insert(m.indices.end(), {a, a + row, a + row + 1, a, a + row + 1, a + 1});
    }
  m.recompute_bounds();
  m.generate_tangents();
  auto hm = r_.upload_mesh(m, "world/horizon");
  global_meshes_.push_back(hm);
  render::Material g;
  g.base_tex = tex_ground_;
  g.roughness = 0.95f;
  g.cutaway = false;
  global_items_.push_back({hm, glm::mat4(1.0f), g, false});
  // Biển
  auto sea = geom::make_box({4000, 0.02f, 4000});
  auto sm = r_.upload_mesh(sea, "world/sea");
  global_meshes_.push_back(sm);
  render::Material water{rgb(0x1F86A8), 0.0f, 0.04f};
  water.cutaway = false;
  water.water = true;
  global_items_.push_back({sm, T({W * 0.5f, -1.2f, D * 0.5f}), water, false});
  // Rừng trên đồi (gộp một mesh)
  MeshBatcher b;
  static const char* trees[] = {"cc0_nature_pine_1", "cc0_nature_pine_2", "cc0_nature_pine_3", "cc0_nature_commontree_2", "cc0_nature_commontree_4"};
  int placed = 0;
  for (int k = 0; k < 900 && placed < 140; ++k) {
    const float x = -260.0f + h01(k * 3u + 1u) * (W + 520.0f), z = -260.0f + h01(k * 3u + 2u) * (D + 520.0f);
    const float d = edge_dist(x, z);
    if (d < 25.0f || x > W + 20.0f || vnoise(x, z, 60, 9) < 0.45f) continue;
    if (const auto* t = kit(trees[k % 5])) {
      const float s = 0.9f + h01(k * 7u) * 0.6f;
      b.add(*t, T({x, height(x, z) - 0.3f, z}) * RY(h01(k * 5u) * 6.28f) * S(glm::vec3(s)), glm::vec4(1.0f), 0.0f);
      ++placed;
    }
  }
  b.flush(r_, "world/hill_trees", global_items_, global_meshes_, false);
}

int WorldView::update(const glm::vec3& focus) {
  focus_ = {focus.x, focus.z};
  const auto u = streamer_.update(focus.x, focus.z);
  for (int i : u.unload) unload_chunk(i);
  for (int i : u.load) load_chunk(i);
  return static_cast<int>(u.load.size());
}

void WorldView::unload_chunk(int index) {
  auto it = chunks_.find(index);
  if (it == chunks_.end()) return;
  for (auto m : it->second.meshes) r_.release_mesh(m);
  chunks_.erase(it);
}

// ---------------------------------------------------------------- nhà ghép mô-đun
bool WorldView::add_house(MeshBatcher& b, const world::Building& bd, float base_y) {
  const float cx = to_m(bd.x), cz = to_m(bd.z);
  const int nx = std::clamp(static_cast<int>(std::lround(to_m(bd.half_x))), 2, 10);  // số mô-đun 2 m theo X
  const int nz = std::clamp(static_cast<int>(std::lround(to_m(bd.half_z))), 2, 10);
  const float W = static_cast<float>(nx) * 2.0f, D = static_cast<float>(nz) * 2.0f;
  const int floors = std::clamp(static_cast<int>(std::lround(to_m(bd.height) / 3.4f)), 1, 3);
  const float fh = 3.12f;
  const float r = h01(bd.id * 2654435761u);
  const bool brick_ground = r < 0.6f;
  const glm::vec4 tint = glm::mix(glm::vec4(1.0f), building_color(bd), 0.35f);  // vữa hơi đổi màu từng nhà
  auto get = [&](const char* id) { return kit(id); };
  const LoadedModel* w_plain = get(brick_ground ? "cc0_village_wall_unevenbrick_straight" : "cc0_village_wall_plaster_straight");
  const LoadedModel* w_win = get(brick_ground ? "cc0_village_wall_unevenbrick_window_wide_round" : "cc0_village_wall_plaster_window_wide_round");
  const LoadedModel* w_door = get(brick_ground ? "cc0_village_wall_unevenbrick_door_round" : "cc0_village_wall_plaster_door_round");
  const LoadedModel* u_grid = get("cc0_village_wall_plaster_woodgrid");
  const LoadedModel* u_win = get(r < 0.5f ? "cc0_village_wall_plaster_window_wide_round" : "cc0_village_wall_plaster_window_thin_round");
  const LoadedModel* corner = get(brick_ground ? "cc0_village_corner_exterior_brick" : "cc0_village_corner_exterior_wood");
  if (!w_plain || !w_win || !w_door || !u_grid || !u_win) return false;
  for (int f = 0; f < floors; ++f) {
    const float y = base_y + static_cast<float>(f) * fh;
    for (int side = 0; side < 4; ++side) {
      const glm::vec2 n = kSide[side];
      const bool along_x = side == 0 || side == 2;
      const int cnt = along_x ? nx : nz;
      const float half = (along_x ? D : W) * 0.5f;
      // Mặt ngoài mô-đun là -Z của model -> xoay để -Z trùng hướng ngoài n
      const float yaw = std::atan2(-n.x, -n.y);
      for (int i = 0; i < cnt; ++i) {
        const float off = -static_cast<float>(cnt) + 1.0f + 2.0f * static_cast<float>(i);
        const glm::vec2 p = glm::vec2(cx, cz) + n * half + (along_x ? glm::vec2(off, 0) : glm::vec2(0, off));
        const LoadedModel* mdl;
        if (f == 0) mdl = (side == bd.rot && i == cnt / 2) ? w_door : ((i + side) % 2 == 0 ? w_win : w_plain);
        else mdl = (i % 2 == 0) ? u_win : u_grid;
        b.add(*mdl, T({p.x, y, p.y}) * RY(yaw), tint);
      }
    }
    if (corner)
      for (int sx : {-1, 1})
        for (int sz : {-1, 1}) b.add(*corner, T({cx + static_cast<float>(sx) * W * 0.5f, y, cz + static_cast<float>(sz) * D * 0.5f}));
  }
  // Mái ngói: chọn kích thước gần nhất rồi co giãn cho khít; nóc chạy theo cạnh dài
  static const int roofs[][2] = {{4, 4}, {4, 6}, {4, 8}, {6, 6}, {6, 8}, {6, 10}, {6, 12}, {8, 8}, {8, 10}, {8, 12}, {8, 14}};
  const bool swap = W > D;  // mái A×B: A theo X, B theo Z (nóc theo Z)
  const float a = swap ? D : W, l = swap ? W : D;
  int best = 0;
  float bd2 = 1e9f;
  for (int i = 0; i < 11; ++i) {
    const float e = std::abs(static_cast<float>(roofs[i][0]) - a) + std::abs(static_cast<float>(roofs[i][1]) - l) * 0.7f;
    if (e < bd2) bd2 = e, best = i;
  }
  const auto rid = std::format("cc0_village_roof_roundtiles_{}x{}", roofs[best][0], roofs[best][1]);
  if (const auto* roof = kit(rid.c_str())) {
    const float sx = a / static_cast<float>(roofs[best][0]), sz = l / static_cast<float>(roofs[best][1]);
    const glm::mat4 m = T({cx, base_y + static_cast<float>(floors) * fh, cz}) * RY(swap ? 1.5707963f : 0.0f) *
                        S({sx, std::clamp((sx + sz) * 0.5f, 0.8f, 1.6f), sz});
    b.add(*roof, m);
  }
  if (r > 0.35f)
    if (const auto* ch = kit(r > 0.7f ? "cc0_village_prop_chimney" : "cc0_village_prop_chimney"))
      b.add(*ch, T({cx + W * 0.22f * (r > 0.5f ? 1.0f : -1.0f), base_y + static_cast<float>(floors) * fh + 0.4f, cz + D * 0.18f}));
  return true;
}

bool WorldView::blocked_for_grass(glm::vec2 p, const ChunkView& cv) const {
  const auto ix = static_cast<world::cm>(p.x * 100), iz = static_cast<world::cm>(p.y * 100);
  if (world_.in_pond(ix, iz, 120)) return true;
  for (const auto& b : cv.data.buildings)
    if (std::abs(p.x - to_m(b.x)) < to_m(b.half_x) + 1.2f && std::abs(p.y - to_m(b.z)) < to_m(b.half_z) + 1.2f) return true;
  for (const auto& e : world_.road_edges) {
    const auto& A = world_.road_nodes[e.a];
    const auto& B = world_.road_nodes[e.b];
    const glm::vec2 a(to_m(A.x), to_m(A.z)), bb(to_m(B.x), to_m(B.z));
    const glm::vec2 ab = bb - a;
    const float t = std::clamp(glm::dot(p - a, ab) / std::max(glm::dot(ab, ab), 1e-4f), 0.0f, 1.0f);
    if (glm::distance(p, a + ab * t) < to_m(e.half_width + e.sidewalk) + 0.6f) return true;
  }
  return false;
}

// Cỏ, hoa, cỏ ba lá, dương xỉ: rải tất định trên nền đất trống của chunk (lay theo gió)
void WorldView::scatter_detail(ChunkView& cv, int index, glm::vec2 origin, float size) {
  struct Kind {
    const char* id;
    float weight, smin, smax;
  };
  static const Kind kinds[] = {{"cc0_nature_grass_common_short", 0.55f, 0.55f, 0.85f}, {"cc0_nature_grass_common_tall", 0.17f, 0.28f, 0.4f},
                               {"cc0_nature_grass_wispy_tall", 0.08f, 0.22f, 0.32f},   {"cc0_nature_clover_1", 0.08f, 0.5f, 0.8f},
                               {"cc0_nature_flower_3_group", 0.06f, 0.35f, 0.55f},     {"cc0_nature_flower_4_group", 0.04f, 0.35f, 0.55f},
                               {"cc0_nature_fern_1", 0.02f, 0.35f, 0.5f}};
  MeshBatcher b;
  const float cell = 1.6f;  // mật độ: ~1 cụm / 2,6 m²
  const int n = static_cast<int>(size / cell);
  for (int j = 0; j < n; ++j)
    for (int i = 0; i < n; ++i) {
      const uint32_t s = static_cast<uint32_t>(index) * 92821u + static_cast<uint32_t>(j * n + i) * 7919u;
      const glm::vec2 p = origin + glm::vec2((static_cast<float>(i) + h01(s)) * cell, (static_cast<float>(j) + h01(s + 1)) * cell);
      const auto dist = world_.district_at(static_cast<world::cm>(p.x * 100), static_cast<world::cm>(p.y * 100));
      const float density = dist == world::District::Park ? 1.0f : dist == world::District::Residential ? 0.8f : 0.45f;
      if (h01(s + 2) > density * (0.55f + 0.45f * vnoise(p.x, p.y, 9, 4)) || blocked_for_grass(p, cv)) continue;
      float pick = h01(s + 3), acc = 0;
      const Kind* k = &kinds[0];
      for (const auto& c : kinds)
        if ((acc += c.weight) >= pick) {
          k = &c;
          break;
        }
      const auto* mdl = kit(k->id);
      if (!mdl) continue;
      const float sc = k->smin + (k->smax - k->smin) * h01(s + 4);
      const glm::vec4 tint = glm::mix(glm::vec4(0.85f, 0.95f, 0.75f, 1), glm::vec4(1.1f, 1.08f, 0.9f, 1), vnoise(p.x, p.y, 14, 6));
      b.add(*mdl, T({p.x, world_.height_at(p.x, p.y) - 0.02f, p.y}) * RY(h01(s + 5) * 6.283f) * S(glm::vec3(sc)), tint, 1.0f);
    }
  b.flush(r_, std::format("chunk/{}/{}/detail", index, mesh_serial_++), cv.detail, cv.meshes, false);
}

void WorldView::load_chunk(int index) {
  if (chunks_.contains(index)) return;  // không bao giờ nạp trùng
  const int cx = index % world_.chunks_x, cz = index / world_.chunks_x;
  ChunkView cv;
  std::string err;
  if (!world::load_chunk(dir_, cx, cz, cv.data, err)) {
    log::error("world", "{}", err);
    return;
  }
  const float cs = to_m(world_.chunk_size);
  const glm::vec2 origin(static_cast<float>(cx) * cs, static_cast<float>(cz) * cs);
  cv.center = origin + glm::vec2(cs * 0.5f);

  // ---- địa hình chunk: màu cỏ biến thiên theo nhiễu
  {
    const auto& t = world_.terrain;
    const float cell = to_m(t.cell) * 0.5f;  // lưới mịn hơn để màu chuyển mượt
    const int n = static_cast<int>(cs / cell);
    geom::MeshData m;
    for (int z = 0; z <= n; ++z)
      for (int x = 0; x <= n; ++x) {
        const float px = origin.x + static_cast<float>(x) * cell, pz = origin.y + static_cast<float>(z) * cell;
        geom::Vertex v;
        v.position = {px, world_.height_at(px, pz), pz};
        const float hx = world_.height_at(px + 0.5f, pz) - world_.height_at(px - 0.5f, pz);
        const float hz = world_.height_at(px, pz + 0.5f) - world_.height_at(px, pz - 0.5f);
        v.normal = glm::normalize(glm::vec3(-hx, 1.0f, -hz));
        v.uv = glm::vec2(px, pz) * 0.12f;
        const auto d = world_.district_at(static_cast<world::cm>(px * 100), static_cast<world::cm>(pz * 100));
        v.color = have_kit_ ? grass_color(px, pz, d) : district_ground(d);
        if (world_.in_pond(static_cast<world::cm>(px * 100), static_cast<world::cm>(pz * 100), 150)) v.color = rgb(0x8A7A5C);
        m.vertices.push_back(v);
      }
    const auto row = static_cast<uint32_t>(n + 1);
    for (uint32_t z = 0; z < static_cast<uint32_t>(n); ++z)
      for (uint32_t x = 0; x < static_cast<uint32_t>(n); ++x) {
        const uint32_t a = z * row + x;
        m.indices.insert(m.indices.end(), {a, a + row, a + row + 1, a, a + row + 1, a + 1});
      }
    m.recompute_bounds();
    m.generate_tangents();
    auto mh = r_.upload_mesh(m, std::format("chunk/{}/{}/terrain", index, mesh_serial_++));
    cv.meshes.push_back(mh);
    render::Material g;
    g.base_tex = tex_ground_;
    g.roughness = 0.95f;
    g.cutaway = false;
    cv.items.push_back({mh, glm::mat4(1.0f), g, false});
  }

  MeshBatcher kitb;
  geom::MeshData walls, plain;
  // ---- nhà
  for (const auto& b : cv.data.buildings) {
    const float x = to_m(b.x), z = to_m(b.z), hx = to_m(b.half_x), hz = to_m(b.half_z);
    float base = world_.height_at(x, z);
    for (float sx : {-hx, hx})
      for (float sz : {-hz, hz}) base = std::min(base, world_.height_at(x + sx, z + sz));
    if (have_kit_ && add_house(kitb, b, base - 0.05f)) continue;
    base -= 0.3f;
    const float h = to_m(b.height) + 0.3f;
    walls.append(geom::make_box({hx * 2, h, hz * 2}, 1.0f / 3.0f), T({x, base + h * 0.5f, z}), building_color(b));
    plain.append(geom::make_box({hx * 2 + 0.3f, 0.35f, hz * 2 + 0.3f}), T({x, base + h + 0.17f, z}), rgb(0x8A4B3C));
  }
  // ---- props
  static const auto pole = geom::make_cylinder(0.07f, 4.2f, 6);
  static const auto cube = geom::make_box({1, 1, 1});
  static const auto bin = geom::make_cylinder(0.28f, 0.9f, 10);
  static const auto trunk = geom::make_cylinder(0.16f, 2.2f, 8);
  static const auto crown = geom::make_sphere(1.4f, 8, 10);
  for (const auto& p : cv.data.props) {
    const float x = to_m(p.x), z = to_m(p.z), y = world_.height_at(x, z);
    const float s = static_cast<float>(p.scale_pct) / 100.0f;
    const float yaw = glm::radians(static_cast<float>(p.yaw_deg));
    const glm::mat4 base = T({x, y, z}) * RY(yaw);
    auto put = [&](const char* id, float scale, float wind = 0.0f) {
      if (const auto* m = have_kit_ ? kit(id) : nullptr) {
        kitb.add(*m, base * S(glm::vec3(scale)), glm::vec4(1.0f), wind);
        return true;
      }
      return false;
    };
    const uint32_t v = p.id % 5;
    switch (p.kind) {
      case world::PropKind::Tree: {
        static const char* t[] = {"cc0_nature_commontree_1", "cc0_nature_commontree_2", "cc0_nature_commontree_3",
                                  "cc0_nature_commontree_4", "cc0_nature_commontree_5"};
        if (!put(t[v], 0.62f * s, 0.25f)) {
          plain.append(trunk, base * S(glm::vec3(s)), rgb(0x6B4A2E));
          plain.append(crown, base * S(glm::vec3(s)) * T({0, 3.0f, 0}), rgb(0x4E8A3A));
        }
        break;
      }
      case world::PropKind::Pine: {
        static const char* t[] = {"cc0_nature_pine_1", "cc0_nature_pine_2", "cc0_nature_pine_3"};
        put(t[v % 3], 0.6f * s, 0.15f);
        break;
      }
      case world::PropKind::Bush: put(v < 2 ? "cc0_nature_bush_common_flowers" : "cc0_nature_bush_common", 0.8f * s, 0.4f); break;
      case world::PropKind::Rock: {
        static const char* t[] = {"cc0_nature_rock_medium_1", "cc0_nature_rock_medium_2", "cc0_nature_rock_medium_3"};
        put(t[v % 3], 0.45f * s);
        break;
      }
      case world::PropKind::Bench:
        plain.append(cube, base * T({0, 0.45f, 0}) * S({1.6f, 0.1f, 0.5f}), rgb(0x8B5A2B));
        plain.append(cube, base * T({0, 0.75f, -0.22f}) * S({1.6f, 0.5f, 0.08f}), rgb(0x8B5A2B));
        break;
      case world::PropKind::Lamp:
        plain.append(pole, base, rgb(0x2E2A26));
        plain.append(cube, base * T({0, 4.25f, 0}) * S({0.35f, 0.3f, 0.35f}), rgb(0xFFE7B0));
        cv.lights.push_back({{x, y + 4.0f, z}, 10.0f, {1.0f, 0.82f, 0.55f}, 22.0f});
        break;
      case world::PropKind::TrashBin:
        if (!put("cc0_village_prop_crate", 0.7f)) plain.append(bin, base, rgb(0x2F5D3A));
        break;
      case world::PropKind::Hydrant: plain.append(bin, base * S({0.6f, 0.8f, 0.6f}), rgb(0xC0392B)); break;
      case world::PropKind::Dumpster:
        if (!put("cc0_village_prop_wagon", 0.8f)) plain.append(cube, base * T({0, 0.6f, 0}) * S({2.0f, 1.2f, 1.2f}), rgb(0x2C5E8C));
        break;
      case world::PropKind::Crate:
        if (!put("cc0_village_prop_crate", 0.8f)) plain.append(cube, base * T({0, 0.4f, 0}) * S(glm::vec3(0.8f)), rgb(0x9C7A4C));
        break;
      default: break;
    }
  }
  // Đánh dấu POI (gameplay dùng dữ liệu POI, không dùng mesh này)
  static const auto disc = geom::make_cylinder(0.6f, 0.08f, 16);
  for (const auto& p : cv.data.pois) {
    const float x = to_m(p.x), z = to_m(p.z), y = world_.height_at(x, z) + 0.06f;
    switch (p.kind) {
      case world::PoiKind::Water: plain.append(disc, T({x, y, z}), rgb(0x3A8DDE)); break;
      case world::PoiKind::Food: plain.append(cube, T({x, y + 0.15f, z}) * S({0.5f, 0.3f, 0.4f}), rgb(0xE67E22)); break;
      case world::PoiKind::Shelter: plain.append(cube, T({x, y + 0.6f, z}) * S({1.6f, 1.2f, 1.2f}), rgb(0x7A5230)); break;
      case world::PoiKind::Shop: plain.append(disc, T({x, y, z}), rgb(0xF1C40F)); break;
      case world::PoiKind::Spawn: plain.append(disc, T({x, y - 0.03f, z}) * S({1.5f, 0.4f, 1.5f}), rgb(0xD8CBB0)); break;
      default: break;
    }
  }
  kitb.flush(r_, std::format("chunk/{}/{}/kit", index, mesh_serial_++), cv.items, cv.meshes, true);
  if (!walls.vertices.empty()) {
    auto mh = r_.upload_mesh(walls, std::format("chunk/{}/{}/walls", index, mesh_serial_++));
    cv.meshes.push_back(mh);
    render::Material m;
    m.base_tex = tex_wall_;
    m.roughness = 0.8f;
    cv.items.push_back({mh, glm::mat4(1.0f), m, true});
  }
  if (!plain.vertices.empty()) {
    auto mh = r_.upload_mesh(plain, std::format("chunk/{}/{}/plain", index, mesh_serial_++));
    cv.meshes.push_back(mh);
    render::Material m;
    m.roughness = 0.75f;
    cv.items.push_back({mh, glm::mat4(1.0f), m, true});
  }
  if (have_kit_) scatter_detail(cv, index, origin, cs);
  chunks_.emplace(index, std::move(cv));
}

void WorldView::collect(std::vector<render::DrawItem>& out, std::vector<render::PointLight>& lights) const {
  out.insert(out.end(), global_items_.begin(), global_items_.end());
  for (const auto& [i, c] : chunks_) {
    out.insert(out.end(), c.items.begin(), c.items.end());
    // Cỏ/hoa chỉ trong ~90 m quanh nhân vật (xa hơn khó thấy, tiết kiệm GPU — quan trọng trên điện thoại)
    if (glm::distance(c.center, focus_) < 90.0f) out.insert(out.end(), c.detail.begin(), c.detail.end());
    lights.insert(lights.end(), c.lights.begin(), c.lights.end());
  }
}

}  // namespace pb::client
