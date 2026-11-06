#include <doctest/doctest.h>

#include <atomic>
#include <stdexcept>
#include <vector>

#include "core/application.hpp"
#include "core/config_reader.hpp"
#include "core/job.hpp"
#include "core/time.hpp"

TEST_CASE("fixed timestep: tích lũy đúng số tick") {
  pb::FixedTimestep ts(0.1, 5, 1.0);
  CHECK(ts.advance(0.05) == 0);
  CHECK(ts.advance(0.05) == 1);
  CHECK(ts.advance(0.25) == 2);
  CHECK(ts.alpha() == doctest::Approx(0.5));
  CHECK(ts.total_steps() == 3);
}

TEST_CASE("fixed timestep: delta rất lớn bị giới hạn, không spiral of death") {
  pb::FixedTimestep ts(1.0 / 30.0, 5, 0.25);
  const int steps = ts.advance(5.0);  // alt-tab 5 giây
  CHECK(steps == 5);
  CHECK(ts.alpha() < 1.0);
  CHECK(ts.alpha() >= 0.0);
  CHECK(ts.dropped_seconds() > 4.7);
  CHECK(ts.advance(-1.0) == 0);  // delta âm bị bỏ qua
}

TEST_CASE("fixed timestep: 1 giây ở 30 Hz cho đúng 30 tick khi chia nhỏ frame") {
  pb::FixedTimestep ts(1.0 / 30.0);
  int total = 0;
  for (int i = 0; i < 144; ++i) total += ts.advance(1.0 / 144.0);
  CHECK(total >= 29);
  CHECK(total <= 30);
}

namespace {
class FakeApp : public pb::Application {
 public:
  FakeApp() : pb::Application(pb::AppSettings{30.0, 5, 0.25, 0.0, "test"}) {}
  int inits = 0, shutdowns = 0, fixed = 0, renders = 0;
  bool throw_in_update = false;
  bool fail_init = false;
  double t = 0;

 protected:
  bool on_init() override {
    ++inits;
    return !fail_init;
  }
  void on_frame_begin() override {
    t += 1.0 / 60.0;
    if (frame_count() >= 60) request_quit(7);
  }
  void on_fixed_update(double) override { ++fixed; }
  void on_update(double) override {
    if (throw_in_update) throw std::runtime_error("boom");
  }
  void on_render(double alpha) override {
    CHECK(alpha >= 0.0);
    CHECK(alpha < 1.0);
    ++renders;
  }
  void on_shutdown() override { ++shutdowns; }
  double now_seconds() override { return t; }
};
}  // namespace

TEST_CASE("application: vòng đời chạy lặp nhiều lần, init/shutdown cân bằng") {
  FakeApp app;
  for (int round = 0; round < 3; ++round) {
    CHECK(app.run() == 7);
  }
  CHECK(app.inits == 3);
  CHECK(app.shutdowns == 3);
  CHECK(app.renders == 60);  // frame_count tích lũy, chỉ vòng đầu có 60 frame
}

TEST_CASE("application: 60 frame ở 60 FPS -> khoảng 30 tick ở 30 Hz") {
  FakeApp app;
  app.run();
  CHECK(app.fixed >= 29);
  CHECK(app.fixed <= 30);
}

TEST_CASE("application: exception được bắt, vẫn shutdown; init lỗi trả 1") {
  FakeApp a;
  a.throw_in_update = true;
  CHECK(a.run() == 1);
  CHECK(a.shutdowns == 1);
  FakeApp b;
  b.fail_init = true;
  CHECK(b.run() == 1);
}

TEST_CASE("thread pool: chạy job song song, exception đi qua future") {
  pb::ThreadPool pool(4);
  std::vector<std::future<int>> fs;
  for (int i = 0; i < 100; ++i) fs.push_back(pool.submit([i] { return i * i; }));
  long long sum = 0;
  for (auto& f : fs) sum += f.get();
  CHECK(sum == 328350);
  auto bad = pool.submit([]() -> int { throw std::runtime_error("x"); });
  CHECK_THROWS_AS(bad.get(), std::runtime_error);
}

TEST_CASE("config reader: mặc định, khoảng giá trị, object con, range") {
  pb::ConfigDiagnostics d;
  auto j = pb::parse_json_config(R"({ // comment được phép
     "a": 5, "b": 2.5, "c": true, "s": "x", "sub": {"n": 99}, "r": [2, 4], "bad_r": [5, 1] })",
                                 d);
  REQUIRE(d.ok());
  pb::ConfigReader rd(j, "", d);
  int a = 0, missing = 42, n = 0, lo = 0, hi = 0, blo = 0, bhi = 0;
  double b = 0;
  bool c = false;
  std::string s;
  rd.read("a", a, 0, 10);
  rd.read("b", b, 0.0, 10.0);
  rd.read("c", c);
  rd.read("s", s);
  rd.read("missing", missing, 0, 100);
  rd.child("sub").read("n", n, 0, 50);  // 99 ngoài khoảng
  rd.read_range("r", lo, hi, 0, 10);
  rd.read_range("bad_r", blo, bhi, 0, 10);
  CHECK(a == 5);
  CHECK(b == doctest::Approx(2.5));
  CHECK(c);
  CHECK(s == "x");
  CHECK(missing == 42);
  CHECK(n == 0);
  CHECK(lo == 2);
  CHECK(hi == 4);
  CHECK(d.errors.size() == 2);
  CHECK(d.warnings.size() == 1);
  CHECK(d.errors[0].find("sub.n") != std::string::npos);
}
