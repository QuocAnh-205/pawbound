#include <doctest/doctest.h>

#include <stdexcept>
#include <vector>

#include "core/ecs.hpp"

using namespace pb::ecs;

namespace {
struct Pos {
  int x = 0;
};
struct Vel {
  int dx = 0;
};
struct Tag {};
}  // namespace

TEST_CASE("ecs: entity destroy làm handle cũ mất hiệu lực, kể cả khi index được tái sử dụng") {
  Registry r;
  Entity a = r.create();
  CHECK(r.valid(a));
  CHECK(r.destroy(a));
  CHECK_FALSE(r.valid(a));
  CHECK_FALSE(r.destroy(a));  // destroy lần 2 không có tác dụng
  Entity b = r.create();
  CHECK(b.index == a.index);  // tái sử dụng slot
  CHECK(b.generation != a.generation);
  CHECK_FALSE(r.valid(a));
  CHECK(r.valid(b));
  CHECK_FALSE(r.valid(Entity{}));  // entity mặc định không hợp lệ
}

TEST_CASE("ecs: add/get/remove/has component") {
  Registry r;
  Entity e = r.create();
  r.add<Pos>(e, 3);
  CHECK(r.has<Pos>(e));
  CHECK(r.get<Pos>(e).x == 3);
  r.add<Pos>(e, 7);  // add lại = thay thế
  CHECK(r.get<Pos>(e).x == 7);
  CHECK(r.count<Pos>() == 1);
  CHECK(r.remove<Pos>(e));
  CHECK_FALSE(r.has<Pos>(e));
  CHECK(r.try_get<Pos>(e) == nullptr);
  CHECK_THROWS_AS(r.get<Pos>(e), std::logic_error);
}

TEST_CASE("ecs: component bị xóa khi destroy entity, entity stale không truy cập được") {
  Registry r;
  Entity e = r.create();
  r.add<Pos>(e, 1);
  r.destroy(e);
  CHECK(r.count<Pos>() == 0);
  CHECK(r.try_get<Pos>(e) == nullptr);
  CHECK_THROWS_AS(r.add<Pos>(e, 1), std::logic_error);
  Entity f = r.create();  // cùng index
  CHECK_FALSE(r.has<Pos>(f));
}

TEST_CASE("ecs: each chỉ duyệt entity có đủ component") {
  Registry r;
  std::vector<Entity> es;
  for (int i = 0; i < 10; ++i) {
    Entity e = r.create();
    r.add<Pos>(e, i);
    if (i % 2 == 0) r.add<Vel>(e, 1);
    es.push_back(e);
  }
  int visited = 0;
  r.each<Pos, Vel>([&](Entity, Pos& p, Vel& v) {
    p.x += v.dx;
    ++visited;
  });
  CHECK(visited == 5);
  CHECK(r.get<Pos>(es[0]).x == 1);
  CHECK(r.get<Pos>(es[1]).x == 1);
  int none = 0;
  r.each<Tag>([&](Entity, Tag&) { ++none; });
  CHECK(none == 0);
}

TEST_CASE("ecs: destroy entity trong lúc each an toàn") {
  Registry r;
  for (int i = 0; i < 100; ++i) r.add<Pos>(r.create(), i);
  int visited = 0;
  r.each<Pos>([&](Entity e, Pos& p) {
    ++visited;
    if (p.x % 3 == 0) r.destroy(e);
  });
  CHECK(visited == 100);
  CHECK(r.count<Pos>() == 66);
  CHECK(r.alive() == 66);
}
