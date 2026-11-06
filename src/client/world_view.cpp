#include "client/world_view.hpp"

#include <cmath>
#include <format>

#include <glm/gtc/matrix_transform.hpp>

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

glm::vec4 district_ground(world::District d) {
  switch (d) {
    case world::District::Downtown: return rgb(0x8C9A7A);
    case world::District::Residential: return rgb(0x7FA05A);
    case world::District::Park: return rgb(0x6FA34E);
    case world::District::Industrial: return rgb(0x9A8F78);
  }
  return glm::vec4(1.0f);
}

// Biến thể màu theo id (deterministic) để nhà không giống hệt nhau.
glm::vec4 building_color(const world::Building& b) {
  static const uint32_t downtown[] = {0xC8CDD2, 0xB9C3CC, 0xD8D2C4, 0xA7B4BE};
  static const uint32_t residential[] = {0xE8DCC2, 0xD9B99B, 0xE3D3A8, 0xC9D6DF, 0xE7C9C0};
  static const uint32_t industrial[] = {0xA8A49A, 0x9FA7A8, 0xB3A68E};
  const uint32_t h = (b.id * 2654435761u) >> 16;
  switch (b.district) {
    case world::District::Downtown: return rgb(downtown[h % 4]);
    case world::District::Industrial: return rgb(industrial[h % 3]);
    default: return rgb(residential[h % 5]);
  }
}

geom::ImageRGBA8 make_window_texture() {
  geom::ImageRGBA8 img;
  img.width = img.height = 64;
  img.pixels.assign(64 * 64 * 4, 255);
  for (int y = 0; y < 64; ++y)
    for (int x = 0; x < 64; ++x) {
      uint8_t* p = &img.pixels[static_cast<size_t>(y * 64 + x) * 4];
      const bool window = x >= 14 && x < 50 && y >= 16 && y < 46;
      const bool frame = x >= 12 && x < 52 && y >= 14 && y < 48 && !window;
      const uint8_t v = window ? 70 : frame ? 200 : static_cast<uint8_t>(236 + ((x * 7 + y * 3) % 9));
      p[0] = window ? 62 : v;
      p[1] = window ? 76 : v;
      p[2] = window ? 96 : v;
    }
  return img;
}
}  // namespace

WorldView::WorldView(render::Renderer& r, world::WorldData w, std::filesystem::path dir)
    : r_(r),
      world_(std::move(w)),
      dir_(std::move(dir)),
      streamer_(world_.chunks_x, world_.chunks_z, to_m(world_.chunk_size), 2, 3) {
  tex_ground_ = r_.upload_texture(geom::make_checker(128, 16, 0xFFFFFFFFu, 0xEDEDEDFFu), true, "world/ground");
  tex_wall_ = r_.upload_texture(make_window_texture(), true, "world/window_wall");
  tex_road_ = r_.upload_texture(geom::make_checker(64, 8, 0xFFFFFFFFu, 0xF2F2F2FFu), true, "world/road");
  build_global();
}

WorldView::~WorldView() {
  for (auto& [i, c] : chunks_)
    for (auto m : c.meshes) r_.release_mesh(m);
  for (auto m : global_meshes_) r_.release_mesh(m);
}

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
  // Đường + vỉa hè + lối đi: một mesh, bám địa hình (chia đoạn ≤ 4 m).
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
        v.color = color;
        roads.vertices.push_back(v);
      }
      // Hướng tam giác: đảm bảo normal hướng lên (+Y)
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
      quad_strip(a, b, to_m(e.half_width), 0.04f, rgb(0xB9A27C));
      continue;
    }
    const glm::vec2 d = glm::normalize(b - a);
    const float ext = to_m(e.half_width + e.sidewalk);  // kéo dài qua giao lộ để không hở
    quad_strip(a - d * ext, b + d * ext, to_m(e.half_width + e.sidewalk), 0.06f, rgb(0xB8B8B4));
    quad_strip(a - d * to_m(e.half_width), b + d * to_m(e.half_width), to_m(e.half_width), 0.09f,
               e.cls == world::RoadClass::Arterial ? rgb(0x4A4B50) : rgb(0x55565B));
  }
  for (auto& v : roads.vertices) v.normal = {0, 1, 0};
  roads.recompute_bounds();
  roads.generate_tangents();
  auto road_mesh = r_.upload_mesh(roads, "world/roads");
  global_meshes_.push_back(road_mesh);
  render::Material rm;
  rm.base_tex = tex_road_;
  rm.roughness = 0.92f;
  rm.cutaway = false;
  global_items_.push_back({road_mesh, glm::mat4(1.0f), rm, false});

  // Mặt nước các hồ
  for (size_t i = 0; i < world_.ponds.size(); ++i) {
    const auto& p = world_.ponds[i];
    auto disc = geom::make_cylinder(to_m(p.radius) + 1.5f, 0.02f, 40, true);
    auto mh = r_.upload_mesh(disc, std::format("world/pond{}", i));
    global_meshes_.push_back(mh);
    render::Material water{{0.18f, 0.32f, 0.42f, 1.0f}, 0.0f, 0.06f};
    water.cutaway = false;
    global_items_.push_back({mh, glm::translate(glm::mat4(1.0f), glm::vec3(to_m(p.x), to_m(p.level), to_m(p.z))), water, false});
  }
}

int WorldView::update(const glm::vec3& focus) {
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

  // ---- địa hình chunk
  {
    const auto& t = world_.terrain;
    const float cell = to_m(t.cell);
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
        v.uv = glm::vec2(px, pz) * 0.25f;
        v.color = district_ground(world_.district_at(static_cast<world::cm>(px * 100), static_cast<world::cm>(pz * 100)));
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

  // ---- nhà (tường có cửa sổ) + mái/props (màu vertex): hai mesh gộp
  geom::MeshData walls, plain;
  auto T = [](glm::vec3 p) { return glm::translate(glm::mat4(1.0f), p); };
  for (const auto& b : cv.data.buildings) {
    const float x = to_m(b.x), z = to_m(b.z), hx = to_m(b.half_x), hz = to_m(b.half_z);
    float base = world_.height_at(x, z);
    for (float sx : {-hx, hx})
      for (float sz : {-hz, hz}) base = std::min(base, world_.height_at(x + sx, z + sz));
    base -= 0.3f;  // móng chìm xuống đất, không hở
    const float h = to_m(b.height) + 0.3f;
    walls.append(geom::make_box({hx * 2, h, hz * 2}, 1.0f / 3.0f), T({x, base + h * 0.5f, z}), building_color(b));
    const glm::vec4 roof = b.district == world::District::Industrial ? rgb(0x6E6F70) : b.district == world::District::Downtown ? rgb(0x5E6266) : rgb(0x8A4B3C);
    plain.append(geom::make_box({hx * 2 + 0.3f, 0.35f, hz * 2 + 0.3f}), T({x, base + h + 0.17f, z}), roof);
  }
  static const auto trunk = geom::make_cylinder(0.16f, 2.2f, 8);
  static const auto crown = geom::make_sphere(1.4f, 8, 10);
  static const auto pine = geom::make_cone(1.3f, 3.2f, 10);
  static const auto bush = geom::make_sphere(0.8f, 6, 8);
  static const auto pole = geom::make_cylinder(0.07f, 4.2f, 6);
  static const auto bin = geom::make_cylinder(0.28f, 0.9f, 10);
  static const auto rock = geom::make_sphere(0.6f, 5, 7);
  static const auto cube = geom::make_box({1, 1, 1});
  for (const auto& p : cv.data.props) {
    const float x = to_m(p.x), z = to_m(p.z), y = world_.height_at(x, z);
    const float s = static_cast<float>(p.scale_pct) / 100.0f;
    const glm::mat4 base = glm::scale(glm::rotate(T({x, y, z}), glm::radians(static_cast<float>(p.yaw_deg)), {0, 1, 0}), glm::vec3(s));
    switch (p.kind) {
      case world::PropKind::Tree:
        plain.append(trunk, base, rgb(0x6B4A2E));
        plain.append(crown, base * T({0, 3.0f, 0}), rgb((p.id & 1) ? 0x3F7A34 : 0x4E8A3A));
        break;
      case world::PropKind::Pine:
        plain.append(trunk, base * glm::scale(glm::mat4(1.0f), {1, 0.6f, 1}), rgb(0x5B3F28));
        plain.append(pine, base * T({0, 1.1f, 0}), rgb(0x2E5E37));
        break;
      case world::PropKind::Bush: plain.append(bush, base * T({0, 0.4f, 0}) * glm::scale(glm::mat4(1.0f), {1, 0.7f, 1}), rgb(0x4C8B3F)); break;
      case world::PropKind::Rock: plain.append(rock, base * glm::scale(glm::mat4(1.0f), {1.2f, 0.6f, 1.0f}), rgb(0x8C8A86)); break;
      case world::PropKind::Bench:
        plain.append(cube, base * T({0, 0.45f, 0}) * glm::scale(glm::mat4(1.0f), {1.6f, 0.1f, 0.5f}), rgb(0x8B5A2B));
        plain.append(cube, base * T({0, 0.75f, -0.22f}) * glm::scale(glm::mat4(1.0f), {1.6f, 0.5f, 0.08f}), rgb(0x8B5A2B));
        break;
      case world::PropKind::Lamp:
        plain.append(pole, base, rgb(0x3A3D42));
        plain.append(cube, base * T({0, 4.25f, 0}) * glm::scale(glm::mat4(1.0f), {0.35f, 0.15f, 0.35f}), rgb(0xFFE7B0));
        cv.lights.push_back({{x, y + 4.0f, z}, 10.0f, {1.0f, 0.82f, 0.55f}, 22.0f});
        break;
      case world::PropKind::TrashBin: plain.append(bin, base, rgb(0x2F5D3A)); break;
      case world::PropKind::Hydrant: plain.append(bin, base * glm::scale(glm::mat4(1.0f), {0.6f, 0.8f, 0.6f}), rgb(0xC0392B)); break;
      case world::PropKind::Dumpster: plain.append(cube, base * T({0, 0.6f, 0}) * glm::scale(glm::mat4(1.0f), {2.0f, 1.2f, 1.2f}), rgb(0x2C5E8C)); break;
      case world::PropKind::Crate: plain.append(cube, base * T({0, 0.4f, 0}) * glm::scale(glm::mat4(1.0f), glm::vec3(0.8f)), rgb(0x9C7A4C)); break;
      default: break;
    }
  }
  // Đánh dấu POI (placeholder hiển thị — gameplay dùng dữ liệu POI, không dùng mesh này)
  static const auto disc = geom::make_cylinder(0.6f, 0.08f, 16);
  for (const auto& p : cv.data.pois) {
    const float x = to_m(p.x), z = to_m(p.z), y = world_.height_at(x, z) + 0.06f;
    switch (p.kind) {
      case world::PoiKind::Water: plain.append(disc, T({x, y, z}), rgb(0x3A8DDE)); break;
      case world::PoiKind::Food: plain.append(cube, T({x, y + 0.15f, z}) * glm::scale(glm::mat4(1.0f), {0.5f, 0.3f, 0.4f}), rgb(0xE67E22)); break;
      case world::PoiKind::Shelter:
        plain.append(cube, T({x, y + 0.6f, z}) * glm::scale(glm::mat4(1.0f), {1.6f, 1.2f, 1.2f}), rgb(0x7A5230));
        break;
      case world::PoiKind::Shop: plain.append(disc, T({x, y, z}), rgb(0xF1C40F)); break;
      case world::PoiKind::Spawn: plain.append(disc, T({x, y, z}) * glm::scale(glm::mat4(1.0f), {1.5f, 1, 1.5f}), rgb(0xF5F5F5)); break;
      default: break;
    }
  }
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
  chunks_.emplace(index, std::move(cv));
}

void WorldView::collect(std::vector<render::DrawItem>& out, std::vector<render::PointLight>& lights) const {
  out.insert(out.end(), global_items_.begin(), global_items_.end());
  for (const auto& [i, c] : chunks_) {
    out.insert(out.end(), c.items.begin(), c.items.end());
    lights.insert(lights.end(), c.lights.begin(), c.lights.end());
  }
}

}  // namespace pb::client
