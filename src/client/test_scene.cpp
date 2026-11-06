#include "client/test_scene.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include "asset/model.hpp"
#include "client/model_upload.hpp"
#include "core/log.hpp"
#include "geom/primitives.hpp"

namespace pb::client {

TestScene build_test_scene(render::Renderer& r, const std::filesystem::path& asset_root) {
  using geom::make_box;
  TestScene s;
  const auto ground_tex = r.upload_texture(geom::make_checker(256, 8, 0x6E8B4EFFu, 0x5F7A43FFu), true, "tex/grass_checker");
  const auto road_tex = r.upload_texture(geom::make_checker(64, 2, 0x4A4A4EFFu, 0x505055FFu), true, "tex/asphalt");
  const auto wall_tex = r.upload_texture(geom::make_checker(128, 16, 0xD9CBB4FFu, 0xCFC0A8FFu), true, "tex/wall");

  const auto ground = r.upload_mesh(geom::make_grid(80, 80, 8, 8, 10.0f), "mesh/ground");
  const auto road = r.upload_mesh(geom::make_grid(6, 80, 1, 8, 1.0f), "mesh/road");
  const auto box = r.upload_mesh(make_box({1, 1, 1}, 1.0f), "mesh/unit_box");
  const auto trunk = r.upload_mesh(geom::make_cylinder(0.18f, 2.2f, 10), "mesh/trunk");
  const auto crown = r.upload_mesh(geom::make_sphere(1.3f, 10, 14), "mesh/crown");
  const auto cone = r.upload_mesh(geom::make_cone(1.2f, 2.6f, 12), "mesh/pine");
  const auto pole = r.upload_mesh(geom::make_cylinder(0.07f, 4.0f, 8), "mesh/lamp_pole");
  const auto ball = r.upload_mesh(geom::make_sphere(0.5f, 12, 16), "mesh/sphere");

  auto add = [&](render::MeshHandle m, glm::mat4 t, render::Material mat, bool shadow = true) {
    s.items.push_back({m, t, mat, shadow});
  };
  auto T = [](glm::vec3 p) { return glm::translate(glm::mat4(1.0f), p); };
  auto TS = [&](glm::vec3 p, glm::vec3 sc) { return glm::scale(T(p), sc); };

  render::Material grass;
  grass.base_tex = ground_tex;
  grass.roughness = 0.95f;
  grass.cutaway = false;
  add(ground, T({0, 0, 0}), grass, false);
  render::Material asphalt;
  asphalt.base_tex = road_tex;
  asphalt.roughness = 0.9f;
  asphalt.cutaway = false;
  add(road, T({0, 0.01f, 0}), asphalt, false);

  // "Nhà" placeholder hai bên đường
  const glm::vec4 colors[] = {{0.85f, 0.8f, 0.72f, 1}, {0.7f, 0.45f, 0.38f, 1}, {0.62f, 0.68f, 0.74f, 1},
                              {0.9f, 0.86f, 0.6f, 1}};
  for (int i = 0; i < 6; ++i) {
    for (int side = -1; side <= 1; side += 2) {
      const float h = 4.0f + static_cast<float>((i * 7 + side + 3) % 4) * 2.5f;
      const glm::vec3 size(7.0f, h, 8.0f);
      const glm::vec3 pos(static_cast<float>(side) * 9.0f, h * 0.5f, -25.0f + static_cast<float>(i) * 10.0f);
      render::Material wall;
      wall.base_tex = wall_tex;
      wall.base_color = colors[(i + (side > 0 ? 2 : 0)) % 4];
      wall.roughness = 0.85f;
      add(box, TS(pos, size), wall);
      render::Material roof;
      roof.base_color = {0.35f, 0.2f, 0.18f, 1};
      roof.roughness = 0.7f;
      add(box, TS(pos + glm::vec3(0, h * 0.5f + 0.15f, 0), {7.4f, 0.3f, 8.4f}), roof);
    }
  }
  // Cây
  render::Material bark{{0.35f, 0.25f, 0.16f, 1}, 0.0f, 0.9f};
  render::Material leaf{{0.22f, 0.45f, 0.18f, 1}, 0.0f, 0.8f};
  render::Material pine{{0.12f, 0.32f, 0.16f, 1}, 0.0f, 0.85f};
  for (int i = 0; i < 10; ++i) {
    const float z = -30.0f + static_cast<float>(i) * 6.5f;
    const float x = (i % 2 ? 4.2f : -4.2f);
    add(trunk, T({x, 0, z}), bark);
    if (i % 3 == 0) add(cone, T({x, 1.6f, z}), pine);
    else add(crown, T({x, 3.0f, z}), leaf);
  }
  // Đèn đường (point light)
  render::Material metal{{0.3f, 0.3f, 0.32f, 1}, 1.0f, 0.4f};
  render::Material bulb{{1.0f, 0.85f, 0.6f, 1}, 0.0f, 0.5f, 4.0f};
  bulb.unlit = true;
  for (int i = 0; i < 4; ++i) {
    const glm::vec3 p(i % 2 ? 3.4f : -3.4f, 0.0f, -18.0f + static_cast<float>(i) * 12.0f);
    add(pole, T(p), metal);
    add(ball, TS(p + glm::vec3(0, 4.0f, 0), glm::vec3(0.4f)), bulb, false);
    s.lights.push_back({p + glm::vec3(0, 3.8f, 0), 9.0f, {1.0f, 0.82f, 0.55f}, 25.0f});
  }
  // Vật liệu PBR mẫu: hàng cầu từ nhám → bóng, phi kim → kim loại
  for (int i = 0; i < 5; ++i) {
    for (int m = 0; m < 2; ++m) {
      render::Material mat{{0.9f, 0.55f, 0.2f, 1}, static_cast<float>(m), 0.1f + 0.2f * static_cast<float>(i)};
      add(ball, T({-1.6f + 0.8f * static_cast<float>(i), 0.5f, 6.0f + 1.0f * static_cast<float>(m)}), mat);
    }
  }
  // Chó placeholder (thân + đầu + 4 chân) ở tâm cảnh
  render::Material fur{{0.78f, 0.55f, 0.32f, 1}, 0.0f, 0.75f};
  const glm::vec3 dog(0.0f, 0.0f, 2.0f);
  add(box, TS(dog + glm::vec3(0, 0.45f, 0), {0.35f, 0.3f, 0.75f}), fur);
  add(box, TS(dog + glm::vec3(0, 0.62f, 0.45f), {0.26f, 0.26f, 0.3f}), fur);
  for (int k = 0; k < 4; ++k) {
    const glm::vec3 lp(k % 2 ? 0.12f : -0.12f, 0.15f, k < 2 ? 0.28f : -0.28f);
    add(box, TS(dog + lp, {0.08f, 0.3f, 0.08f}), fur);
  }
  s.focus = dog;

  // Phase 4: asset GLB thật nạp qua pipeline (bind pose tĩnh; skinning ở Phase 6).
  const char* glbs[][3] = {{"assets/prototype/fox/Fox.glb", "fox", "0.0075"},
                           {"assets/placeholder/placeholder_dog.glb", "pdog", "1.0"}};
  float x = 1.6f;
  for (const auto& g : glbs) {
    auto lr = asset::load_gltf(asset_root / g[0]);
    if (!lr.ok()) {
      log::warn("scene", "không nạp được {}: {}", g[0], lr.error);
      continue;
    }
    const auto gm = upload_model(r, lr.model, g[1], std::stof(g[2]));
    append_draws(gm, glm::rotate(T({x, 0, 2.0f}), glm::radians(-30.0f), glm::vec3(0, 1, 0)), s.items);
    log::info("scene", "đã nạp {} ({} part, {} tam giác)", g[0], gm.parts.size(), lr.model.triangle_count());
    x += 1.4f;
  }
  return s;
}

}  // namespace pb::client
