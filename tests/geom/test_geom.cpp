#include <doctest/doctest.h>

#include <filesystem>

#include <glm/gtc/matrix_transform.hpp>

#include "geom/camera.hpp"
#include "geom/image.hpp"
#include "geom/primitives.hpp"

using namespace pb::geom;

TEST_CASE("primitives: mesh hợp lệ, bounds đúng") {
  auto box = make_box({2, 4, 6});
  CHECK(validate_mesh(box).empty());
  CHECK(box.triangle_count() == 12);
  CHECK(box.bounds.min.x == doctest::Approx(-1));
  CHECK(box.bounds.max.y == doctest::Approx(2));
  CHECK(box.bounds.max.z == doctest::Approx(3));
  for (auto* m : {&box}) (void)m;
  auto grid = make_grid(10, 10, 4, 4);
  CHECK(validate_mesh(grid).empty());
  CHECK(grid.triangle_count() == 32);
  CHECK(validate_mesh(make_cylinder(0.5f, 2, 12)).empty());
  CHECK(validate_mesh(make_cone(0.5f, 2, 12)).empty());
  CHECK(validate_mesh(make_sphere(1, 8, 12)).empty());
}

TEST_CASE("primitives: tam giác box quay CCW ra ngoài (normal hình học = normal vertex)") {
  auto box = make_box({1, 1, 1});
  for (size_t i = 0; i < box.indices.size(); i += 3) {
    const auto& a = box.vertices[box.indices[i]];
    const auto& b = box.vertices[box.indices[i + 1]];
    const auto& c = box.vertices[box.indices[i + 2]];
    const glm::vec3 n = glm::normalize(glm::cross(b.position - a.position, c.position - a.position));
    CHECK(glm::dot(n, a.normal) > 0.99f);
  }
}

TEST_CASE("mesh: validate phát hiện index lỗi") {
  MeshData m = make_box({1, 1, 1});
  m.indices.push_back(9999);
  m.indices.push_back(0);
  m.indices.push_back(1);
  CHECK_FALSE(validate_mesh(m).empty());
  MeshData empty;
  CHECK_FALSE(validate_mesh(empty).empty());
}

TEST_CASE("aabb: transformed bao đúng sau xoay 90°") {
  Aabb b;
  b.expand(glm::vec3(-1, 0, -2));
  b.expand(glm::vec3(1, 1, 2));
  const glm::mat4 r = glm::rotate(glm::mat4(1), glm::radians(90.0f), glm::vec3(0, 1, 0));
  auto t = b.transformed(glm::translate(glm::mat4(1), glm::vec3(10, 0, 0)) * r);
  CHECK(t.min.x == doctest::Approx(8));
  CHECK(t.max.x == doctest::Approx(12));
  CHECK(t.min.z == doctest::Approx(-1));
  CHECK(t.max.z == doctest::Approx(1));
}

TEST_CASE("camera 2.5D: xoay 90° theo bước, hướng tiến/phải vuông góc, zoom giới hạn") {
  Camera25D cam;
  const float y0 = cam.yaw_deg();
  cam.rotate_step(1);
  for (int i = 0; i < 200; ++i) cam.update(1.0f / 60.0f);
  CHECK(cam.yaw_deg() == doctest::Approx(y0 + 90.0f));
  CHECK(std::abs(glm::dot(cam.ground_forward(), cam.ground_right())) < 1e-5f);
  // "tiến" phải hướng ra xa camera trên mặt đất
  const glm::vec3 back = cam.back_dir();
  CHECK(glm::dot(cam.ground_forward(), glm::vec3(back.x, 0, back.z)) < 0.0f);
  for (int i = 0; i < 100; ++i) cam.add_zoom(10.0f);
  CHECK(cam.zoom() == doctest::Approx(30.0f));
  for (int i = 0; i < 100; ++i) cam.add_zoom(-10.0f);
  CHECK(cam.zoom() == doctest::Approx(3.5f));
}

TEST_CASE("frustum: target luôn nhìn thấy; vật ở xa bị loại") {
  Camera25D cam;
  cam.set_target({50, 0, 50}, true);
  const auto f = Frustum::from_matrix(cam.proj(16.0f / 9.0f) * cam.view());
  CHECK(f.intersects({49, 0, 49}, {51, 1, 51}));
  CHECK_FALSE(f.intersects({500, 0, 500}, {501, 1, 501}));
  CHECK_FALSE(f.intersects({-400, 0, 50}, {-399, 1, 51}));
}

TEST_CASE("image: ghi PNG rồi đọc lại khớp từng pixel") {
  auto img = make_checker(16, 4, 0xFF0000FFu, 0x00FF00FFu);
  const auto path = std::filesystem::temp_directory_path() / "pb_test_checker.png";
  REQUIRE(write_png(path, img));
  std::string err;
  auto back = load_image(path, err);
  REQUIRE(back.valid());
  CHECK(back.width == 16);
  CHECK(back.pixels == img.pixels);
  std::filesystem::remove(path);
  auto bad = load_image("khong_ton_tai.png", err);
  CHECK_FALSE(bad.valid());
  CHECK_FALSE(err.empty());
}

TEST_CASE("camera 3D: goc thu ba sau lung, goc thu nhat o mat, doi bang V, pitch co gioi han") {
  GameCamera cam;  // mặc định: góc thứ ba
  CHECK(cam.mode() == CameraMode::ThirdPerson);
  CHECK(cam.perspective());
  cam.set_target({100, 0, 100}, true);
  const glm::vec3 p3 = cam.position();
  CHECK(glm::distance(p3, cam.pivot()) == doctest::Approx(cam.distance()).epsilon(0.01));
  // camera ở phía sau theo hướng "tiến" (nhìn về phía trước nhân vật)
  CHECK(glm::dot(glm::vec3(p3.x - 100, 0, p3.z - 100), cam.ground_forward()) < 0.0f);
  // Vật phía trước thấy được, vật sau lưng camera bị loại
  const auto f = Frustum::from_matrix(cam.proj(16.0f / 9.0f) * cam.view());
  const glm::vec3 ahead = glm::vec3(100, 0, 100) + cam.ground_forward() * 30.0f;
  CHECK(f.intersects(ahead - glm::vec3(1), ahead + glm::vec3(1)));
  const glm::vec3 behind = glm::vec3(100, 0, 100) - cam.ground_forward() * 60.0f;
  CHECK_FALSE(f.intersects(behind - glm::vec3(1), behind + glm::vec3(1)));
  // Không chui xuống đất
  cam.set_ground([](float, float) { return 50.0f; });
  CHECK(cam.position().y >= 50.0f);
  cam.set_ground(nullptr);
  // Góc thứ nhất: camera ở mắt, gần mục tiêu
  cam.toggle_person();
  CHECK(cam.mode() == CameraMode::FirstPerson);
  CHECK(glm::distance(cam.position(), glm::vec3(100, 0, 100)) < 1.0f);
  cam.toggle_person();
  CHECK(cam.mode() == CameraMode::ThirdPerson);
  // Nhìn bằng chuột: pitch bị kẹp, yaw đổi hướng tiến
  for (int i = 0; i < 100; ++i) cam.look(0, 10);
  CHECK(cam.pitch_deg() <= 75.0f);
  const glm::vec3 f0 = cam.ground_forward();
  cam.look(90, 0);
  CHECK(std::abs(glm::dot(f0, cam.ground_forward())) < 1e-4f);
  // Chế độ 2.5D không bị chuột ảnh hưởng
  Camera25D iso;
  const float y0 = iso.yaw_deg();
  iso.look(30, 30);
  CHECK(iso.yaw_deg() == doctest::Approx(y0));
}
