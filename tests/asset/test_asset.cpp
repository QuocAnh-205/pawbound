#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <numbers>

#include <glm/gtc/matrix_transform.hpp>

#include "asset/glb_writer.hpp"
#include "asset/manifest.hpp"
#include "asset/model.hpp"
#include "asset/placeholders.hpp"
#include "asset/sha256.hpp"
#include "geom/primitives.hpp"

using namespace pb::asset;
namespace fs = std::filesystem;

namespace {
fs::path tmp(const char* name) {
  const auto dir = fs::temp_directory_path() / "pawbound_tests";
  fs::create_directories(dir);
  return dir / name;
}
}  // namespace

TEST_CASE("sha256: vector chuẩn NIST") {
  CHECK(sha256_hex({}) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  const std::string abc = "abc";
  CHECK(sha256_hex({reinterpret_cast<const uint8_t*>(abc.data()), abc.size()}) ==
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  const std::string two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  CHECK(sha256_hex({reinterpret_cast<const uint8_t*>(two.data()), two.size()}) ==
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
  Sha256 h;  // update nhiều mảnh = update một lần
  for (int i = 0; i < 1000; ++i) h.update(std::string_view("a"));
  std::string a1000(1000, 'a');
  CHECK(h.finish_hex() == sha256_hex({reinterpret_cast<const uint8_t*>(a1000.data()), a1000.size()}));
}

TEST_CASE("glb: ghi rồi đọc lại mesh tĩnh có texture") {
  GlbDesc d;
  d.mesh = pb::geom::make_box({2, 1, 3}, 1.0f);
  d.base_color = {0.5f, 0.25f, 1.0f, 1.0f};
  auto tex = pb::geom::make_checker(8, 2, 0xFF0000FFu, 0x0000FFFFu);
  d.base_texture = &tex;
  std::string err;
  const auto p = tmp("static_box.glb");
  REQUIRE(write_glb(p, d, err));
  auto r = load_gltf(p);
  REQUIRE_MESSAGE(r.ok(), r.error);
  const auto& m = r.model;
  REQUIRE(m.meshes.size() == 1);
  CHECK(m.meshes[0].primitives[0].mesh.vertices.size() == d.mesh.vertices.size());
  CHECK(m.meshes[0].primitives[0].mesh.indices == d.mesh.indices);
  CHECK(m.triangle_count() == 12);
  CHECK(m.bounds.min.x == doctest::Approx(-1));
  CHECK(m.bounds.max.z == doctest::Approx(1.5));
  CHECK(m.materials[0].base_color.g == doctest::Approx(0.25f));
  REQUIRE(m.images.size() == 1);
  CHECK(m.images[0].image.pixels == tex.pixels);
  CHECK(m.images[0].used_as_color);
  // UV và normal giữ nguyên
  const auto& v0 = m.meshes[0].primitives[0].mesh.vertices[0];
  CHECK(v0.uv == d.mesh.vertices[0].uv);
  CHECK(v0.normal == d.mesh.vertices[0].normal);
}

TEST_CASE("glb: skeleton + animation placeholder đọc lại đúng, sampling nội suy đúng") {
  PlaceholderDog dog;
  make_placeholder_dog(dog);
  std::string err;
  const auto p = tmp("dog.glb");
  REQUIRE(write_glb(p, dog.desc, err));
  auto r = load_gltf(p);
  REQUIRE_MESSAGE(r.ok(), r.error);
  const auto& m = r.model;
  REQUIRE(m.skins.size() == 1);
  const Skeleton& s = m.skins[0];
  CHECK(s.size() == 8);
  CHECK(validate_skeleton(s).empty());
  CHECK(s.find("leg_fl") >= 0);
  CHECK(s.parents[static_cast<size_t>(s.find("head"))] == s.find("body"));
  REQUIRE(m.animations.size() == 3);
  const auto* walk = m.find_animation("walk");
  REQUIRE(walk);
  CHECK(walk->duration == doctest::Approx(1.0f));

  // Bind pose: ma trận skinning = I
  Pose bind = s.bind_local;
  std::vector<glm::mat4> sk;
  skinning_matrices(s, bind, sk);
  for (const auto& mtx : sk)
    for (int c = 0; c < 4; ++c)
      for (int rr = 0; rr < 4; ++rr) CHECK(mtx[c][rr] == doctest::Approx(c == rr ? 1.0f : 0.0f).epsilon(1e-4));

  // Walk: chân trước trái ở 1/4 chu kỳ quay quanh X ~25°
  Pose pose;
  sample(*walk, s, 0.25f, true, pose);
  const auto fl = static_cast<size_t>(s.find("leg_fl"));
  const float angle = glm::degrees(glm::angle(pose[fl].r));
  CHECK(angle == doctest::Approx(25.0f).epsilon(0.02));
  // Loop: t và t + duration cho cùng kết quả
  Pose pose2;
  sample(*walk, s, 1.25f, true, pose2);
  CHECK(glm::dot(pose[fl].r, pose2[fl].r) == doctest::Approx(1.0f).epsilon(1e-5));
  // Blend 0 = a, 1 = b
  Pose idle;
  sample(*m.find_animation("idle"), s, 0.0f, true, idle);
  Pose mixed;
  blend(idle, pose, 1.0f, mixed);
  CHECK(glm::dot(mixed[fl].r, pose[fl].r) == doctest::Approx(1.0f).epsilon(1e-5));
  blend(idle, pose, 0.0f, mixed);
  CHECK(glm::dot(mixed[fl].r, idle[fl].r) == doctest::Approx(1.0f).epsilon(1e-5));
}

TEST_CASE("animation: step và linear interpolation") {
  Skeleton s;
  s.names = {"j"};
  s.parents = {-1};
  s.bind_local = {Transform{}};
  s.inverse_bind = {glm::mat4(1.0f)};
  AnimationClip c{"c", 2.0f, {}};
  Track t;
  t.joint = 0;
  t.path = Path::Translation;
  t.times = {0.0f, 2.0f};
  t.values = {{0, 0, 0, 0}, {10, 0, 0, 0}};
  c.tracks.push_back(t);
  Pose p;
  sample(c, s, 0.5f, false, p);
  CHECK(p[0].t.x == doctest::Approx(2.5f));
  sample(c, s, 99.0f, false, p);  // clamp khi không loop
  CHECK(p[0].t.x == doctest::Approx(10.0f));
  c.tracks[0].interp = Interp::Step;
  sample(c, s, 1.9f, false, p);
  CHECK(p[0].t.x == doctest::Approx(0.0f));
}

TEST_CASE("loader: file lỗi trả lỗi rõ ràng, không crash") {
  CHECK_FALSE(load_gltf("khong_ton_tai.glb").ok());
  const auto junk = tmp("junk.glb");
  {
    std::ofstream o(junk, std::ios::binary);
    o << "đây không phải glb";
  }
  auto r = load_gltf(junk);
  CHECK_FALSE(r.ok());
  CHECK_FALSE(r.error.empty());
  // File GLB bị cắt cụt
  GlbDesc d;
  d.mesh = pb::geom::make_box({1, 1, 1});
  std::string err;
  const auto full = tmp("full.glb");
  REQUIRE(write_glb(full, d, err));
  const auto size = fs::file_size(full);
  const auto cut = tmp("cut.glb");
  fs::copy_file(full, cut, fs::copy_options::overwrite_existing);
  fs::resize_file(cut, size / 2);
  CHECK_FALSE(load_gltf(cut).ok());
}

TEST_CASE("manifest: license, sha256, scale, animation bắt buộc") {
  CHECK(license_allowed("CC0-1.0"));
  CHECK(license_allowed("CC0-1.0 AND CC-BY-4.0"));
  CHECK_FALSE(license_allowed(""));
  CHECK_FALSE(license_allowed("UNKNOWN"));
  CHECK_FALSE(license_allowed("CC0-1.0 AND GPL-3.0"));

  const auto dir = fs::temp_directory_path() / "pawbound_tests" / "manifest";
  fs::create_directories(dir);
  PlaceholderDog dog;
  make_placeholder_dog(dog);
  std::string err;
  REQUIRE(write_glb(dir / "dog.glb", dog.desc, err));
  Manifest m;
  m.base_dir = dir;
  ManifestEntry e;
  e.id = "dog";
  e.path = "dog.glb";
  e.type = "character";
  e.quality = "placeholder";
  e.source = "test";
  e.author = "test";
  e.license = "PAWBOUND-Generated";
  e.sha256 = *sha256_file_hex(dir / "dog.glb");
  e.required_animations = {"walk"};
  auto errors = [&](const ManifestEntry& x) {
    int n = 0;
    for (const auto& f : validate_entry(m, x)) n += f.error;
    return n;
  };
  CHECK(errors(e) == 0);
  auto bad = e;
  bad.license = "";
  CHECK(errors(bad) >= 1);
  bad = e;
  bad.sha256 = std::string(64, '0');
  CHECK(errors(bad) >= 1);
  bad = e;
  bad.scale = 100.0;  // 80 m: sai scale với loại character
  CHECK(errors(bad) >= 1);
  bad = e;
  bad.required_animations = {"fly"};
  CHECK(errors(bad) >= 1);
  bad = e;
  bad.path = "missing.glb";
  CHECK(errors(bad) >= 1);
  bad = e;
  bad.type = "spaceship";
  CHECK(errors(bad) >= 1);
}

TEST_CASE("asset thật: Fox.glb (Khronos, prototype) có skeleton 24 joint và 3 animation") {
  const fs::path fox = fs::path(PB_SOURCE_DIR) / "assets/prototype/fox/Fox.glb";
  REQUIRE(fs::exists(fox));
  auto r = load_gltf(fox);
  REQUIRE_MESSAGE(r.ok(), r.error);
  CHECK(r.model.skins.size() == 1);
  CHECK(r.model.skins[0].size() == 24);
  CHECK(r.model.find_animation("Walk"));
  CHECK(r.model.find_animation("Run"));
  CHECK(r.model.find_animation("Survey"));
  CHECK(r.model.images.size() == 1);
  CHECK(r.model.triangle_count() == 576);
}
