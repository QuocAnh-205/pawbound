#include <doctest/doctest.h>

#include <memory>
#include <string>
#include <vector>

#include "core/event.hpp"
#include "core/resource.hpp"

namespace {
struct Ping {
  int v;
};
}  // namespace

TEST_CASE("event: subscribe/publish và RAII unsubscribe") {
  pb::EventChannel<Ping> ch;
  int sum = 0;
  {
    auto sub = ch.subscribe([&](const Ping& p) { sum += p.v; });
    ch.publish({2});
    CHECK(sum == 2);
    CHECK(ch.subscriber_count() == 1);
  }
  ch.publish({5});
  CHECK(sum == 2);
  CHECK(ch.subscriber_count() == 0);
}

TEST_CASE("event: unsubscribe chính mình và handler khác trong lúc dispatch") {
  pb::EventChannel<Ping> ch;
  std::vector<int> calls;
  pb::EventChannel<Ping>::Subscription s1, s2, s3;
  s1 = ch.subscribe([&](const Ping&) {
    calls.push_back(1);
    s1.reset();  // tự hủy
    s2.reset();  // hủy handler sau -> không được gọi
  });
  s2 = ch.subscribe([&](const Ping&) { calls.push_back(2); });
  s3 = ch.subscribe([&](const Ping&) { calls.push_back(3); });
  ch.publish({0});
  CHECK(calls == std::vector<int>{1, 3});
  ch.publish({0});
  CHECK(calls == std::vector<int>{1, 3, 3});
}

TEST_CASE("event: subscribe trong dispatch chỉ nhận từ lần sau; subscription sống lâu hơn channel") {
  pb::EventChannel<Ping>::Subscription late;
  auto ch = std::make_unique<pb::EventChannel<Ping>>();
  int late_calls = 0;
  auto s = ch->subscribe([&](const Ping&) {
    if (!late.active()) late = ch->subscribe([&](const Ping&) { ++late_calls; });
  });
  ch->publish({0});
  CHECK(late_calls == 0);
  ch->publish({0});
  CHECK(late_calls == 1);
  ch.reset();  // hủy channel trước subscription
  late.reset();
  s.reset();  // không crash
  CHECK_FALSE(late.active());
}

TEST_CASE("event: hàng đợi enqueue/flush") {
  pb::EventChannel<Ping> ch;
  int sum = 0;
  auto s = ch.subscribe([&](const Ping& p) { sum += p.v; });
  ch.enqueue({1});
  ch.enqueue({2});
  CHECK(sum == 0);
  CHECK(ch.queued() == 2);
  ch.flush();
  CHECK(sum == 3);
  CHECK(ch.queued() == 0);
}

TEST_CASE("resource: trạng thái loading → loaded/failed, handle stale bị phát hiện") {
  pb::ResourceRegistry<std::string> reg;
  auto h = reg.reserve("tex/dog");
  CHECK(reg.state(h) == pb::ResourceState::Loading);
  CHECK(reg.get(h) == nullptr);
  CHECK(reg.set_loaded(h, "pixels"));
  REQUIRE(reg.get(h) != nullptr);
  CHECK(*reg.get(h) == "pixels");
  CHECK(reg.find("tex/dog") == h);
  CHECK(reg.reserve("tex/dog") == h);  // trùng tên -> cùng handle

  auto bad = reg.reserve("tex/missing");
  reg.set_failed(bad, "không tìm thấy file");
  CHECK(reg.state(bad) == pb::ResourceState::Failed);
  CHECK(reg.error(bad) == "không tìm thấy file");
  CHECK(reg.get(bad) == nullptr);

  CHECK(reg.release(h));
  CHECK_FALSE(reg.valid(h));
  CHECK_FALSE(reg.state(h).has_value());
  CHECK_FALSE(reg.release(h));
  auto h2 = reg.add("tex/cat", "x");  // tái dùng slot
  CHECK(h2.index == h.index);
  CHECK_FALSE(reg.valid(h));
  CHECK(reg.get(h) == nullptr);
  CHECK_FALSE(reg.valid(pb::Handle<std::string>{}));
}
