// Test tích hợp: dedicated server thật (trong process) + client ENet thật qua UDP localhost.
#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <thread>

#include <enet/enet.h>
#include <glm/geometric.hpp>

#include "net/protocol.hpp"
#include "net/ticket.hpp"
#include "server/game_server.hpp"
#include "world/generator.hpp"
#include "world/world_config.hpp"
#include "world/world_io.hpp"

using namespace pb;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {
int64_t unix_now() {
  return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

struct TestServer {
  fs::path dir = fs::temp_directory_path() / "pawbound_tests" / "server";
  net::PublicKey pk{};
  net::SecretKey sk{};
  std::string world_hash;
  uint16_t port = 0;
  std::atomic<bool> stop{false};
  std::unique_ptr<server::GameServer> srv;
  std::thread th;

  TestServer() {
    REQUIRE(net::crypto_init());
    fs::remove_all(dir);
    fs::create_directories(dir);
    ConfigDiagnostics d;
    auto w = world::generate_world(world::load_world_config(fs::path(PB_SOURCE_DIR) / "data/world/demo.json", d));
    std::string err;
    REQUIRE(world::save_world(dir / "world", w, err));
    world_hash = world::logical_hash(w);
    net::generate_keypair(pk, sk);
    std::ofstream(dir / "pk.b64") << net::to_base64(pk.data(), pk.size());
    port = static_cast<uint16_t>(28000 + std::random_device{}() % 2000);
    server::ServerConfig cfg;
    cfg.port = port;
    cfg.world_dir = dir / "world";
    cfg.data_root = PB_SOURCE_DIR;
    cfg.ticket_pubkey = dir / "pk.b64";
    cfg.server_id = "test-1";
    cfg.linger_s = 30;
    cfg.metrics_interval_s = 3600;
    srv = std::make_unique<server::GameServer>(cfg);
    REQUIRE_MESSAGE(srv->init(err), err);
    th = std::thread([this] { srv->run(stop); });
  }
  ~TestServer() {
    stop = true;
    th.join();
  }
  std::string ticket(uint64_t chr, int64_t ttl = 60, const std::string& server_id = "test-1") const {
    return net::sign_ticket({1000 + chr, chr, server_id, unix_now() + ttl, net::random_jti()}, sk);
  }
};
TestServer& srv() {
  static TestServer s;
  return s;
}

struct Client {
  ENetHost* host = nullptr;
  ENetPeer* peer = nullptr;
  uint32_t seq = 0;
  uint32_t player_id = 0;
  std::optional<net::Snapshot> last_snapshot;
  std::optional<std::string> rejected;
  bool disconnected = false;

  explicit Client(uint16_t port) {
    host = enet_host_create(nullptr, 1, 2, 0, 0);
    ENetAddress a{};
    enet_address_set_host(&a, "127.0.0.1");
    a.port = port;
    peer = enet_host_connect(host, &a, 2, 0);
    ENetEvent ev;
    const auto until = std::chrono::steady_clock::now() + 3s;
    while (std::chrono::steady_clock::now() < until)
      if (enet_host_service(host, &ev, 50) > 0 && ev.type == ENET_EVENT_TYPE_CONNECT) return;
    FAIL("không kết nối được tới server");
  }
  ~Client() {
    if (peer && !disconnected) enet_peer_disconnect_now(peer, 0);
    enet_host_flush(host);
    enet_host_destroy(host);
  }
  void raw(const std::vector<uint8_t>& b, bool reliable = true) {
    enet_peer_send(peer, reliable ? 0 : 1, enet_packet_create(b.data(), b.size(), reliable ? ENET_PACKET_FLAG_RELIABLE : 0));
    enet_host_flush(host);
  }
  void send(net::PacketType t, const std::vector<uint8_t>& payload, bool reliable = true) {
    raw(net::encode({net::kProtocolVersion, t, ++seq, 0, 0}, payload), reliable);
  }
  void hello(const std::string& ticket, const std::string& hash) {
    send(net::PacketType::Hello, net::write(net::Hello{net::kProtocolVersion, ticket, hash, "test"}));
  }
  void pump(std::chrono::milliseconds dur) {
    const auto until = std::chrono::steady_clock::now() + dur;
    ENetEvent ev;
    while (std::chrono::steady_clock::now() < until) {
      if (enet_host_service(host, &ev, 5) <= 0) continue;
      if (ev.type == ENET_EVENT_TYPE_DISCONNECT) {
        disconnected = true;
        continue;
      }
      if (ev.type != ENET_EVENT_TYPE_RECEIVE) continue;
      net::Header h;
      std::span<const uint8_t> pl;
      if (net::decode_header(std::span(ev.packet->data, ev.packet->dataLength), h, pl)) {
        if (h.type == net::PacketType::Welcome) {
          net::Welcome w;
          if (net::read(pl, w)) player_id = w.player_id;
        } else if (h.type == net::PacketType::Snapshot) {
          net::Snapshot s;
          if (net::read(pl, s)) last_snapshot = s;
        } else if (h.type == net::PacketType::Reject) {
          net::Reject r;
          if (net::read(pl, r)) rejected = r.reason;
        }
      }
      enet_packet_destroy(ev.packet);
    }
  }
  // Gửi input 30 Hz trong khoảng thời gian, vừa nhận gói
  void drive(int8_t mx, int8_t mz, uint16_t buttons, std::chrono::milliseconds dur) {
    const auto until = std::chrono::steady_clock::now() + dur;
    while (std::chrono::steady_clock::now() < until) {
      net::InputPacket ip{{{++seq, mx, mz, buttons, 0}}};
      send(net::PacketType::Input, net::write(ip), false);
      pump(33ms);
    }
  }
  std::optional<net::EntityState> entity(uint32_t id) const {
    if (!last_snapshot) return std::nullopt;
    for (const auto& e : last_snapshot->entities)
      if (e.id == id) return e;
    return std::nullopt;
  }
};
}  // namespace

TEST_CASE("server: hai client thật thấy nhau di chuyển (vị trí do server tính)") {
  auto& s = srv();
  Client a(s.port), b(s.port);
  a.hello(s.ticket(1), s.world_hash);
  b.hello(s.ticket(2), s.world_hash);
  a.pump(300ms);
  b.pump(300ms);
  REQUIRE(a.player_id != 0);
  REQUIRE(b.player_id != 0);
  CHECK(a.player_id != b.player_id);
  b.pump(200ms);
  const auto before = b.entity(a.player_id);
  REQUIRE_MESSAGE(before, "B không thấy A trong snapshot");
  a.drive(0, 127, 0, 1000ms);  // A chạy tới 1 giây
  b.pump(300ms);
  const auto after = b.entity(a.player_id);
  REQUIRE(after);
  const float moved = glm::distance(glm::vec2(before->x_cm, before->z_cm), glm::vec2(after->x_cm, after->z_cm)) / 100.0f;
  CHECK(moved > 3.0f);
  CHECK(moved < 7.5f);  // ≤ tốc độ chạy 6 m/s × ~1,1 s
}

TEST_CASE("server: không teleport/tăng tốc bằng input lớn; client không gửi được trạng thái") {
  auto& s = srv();
  Client c(s.port);
  c.hello(s.ticket(3), s.world_hash);
  c.pump(300ms);
  REQUIRE(c.player_id);
  const auto p0 = c.entity(c.player_id);
  REQUIRE(p0);
  // move (127,127) có độ dài √2 — server kẹp về 1; giữ nước rút 1 giây
  c.drive(127, 127, net::kSprint, 1000ms);
  const auto p1 = c.entity(c.player_id);
  REQUIRE(p1);
  const float d = glm::distance(glm::vec2(p0->x_cm, p0->z_cm), glm::vec2(p1->x_cm, p1->z_cm)) / 100.0f;
  CHECK(d < 9.0f * 1.15f + 0.5f);  // ≤ tốc độ nước rút (9 m/s), không phải √2 lần
  CHECK(c.last_snapshot->self.hp == 100);
  // Client gửi gói "Snapshot"/"Welcome" giả (cố đặt trạng thái): bị coi là vi phạm, không có tác dụng
  for (int i = 0; i < 4; ++i) c.send(net::PacketType::Snapshot, net::write(net::Snapshot{}));
  c.pump(300ms);
  CHECK(c.last_snapshot->self.hp == 100);
  CHECK_FALSE(c.rejected);  // 4 vi phạm < ngưỡng 10
}

TEST_CASE("server: gói rác/quá cỡ -> kick, server vẫn phục vụ client khác") {
  auto& s = srv();
  Client good(s.port), bad(s.port);
  good.hello(s.ticket(4), s.world_hash);
  bad.hello(s.ticket(5), s.world_hash);
  good.pump(200ms);
  bad.pump(200ms);
  REQUIRE(bad.player_id);
  std::mt19937 rng(7);
  for (int i = 0; i < 30; ++i) {
    std::vector<uint8_t> junk(1 + rng() % 3000);
    for (auto& x : junk) x = static_cast<uint8_t>(rng());
    bad.raw(junk);
  }
  bad.pump(800ms);
  CHECK(bad.rejected.has_value());
  CHECK(bad.disconnected);
  good.drive(64, 0, 0, 300ms);
  CHECK(good.last_snapshot.has_value());
  CHECK(good.entity(good.player_id).has_value());
}

TEST_CASE("server: từ chối ticket giả/hết hạn/sai server/dùng lại và world khác") {
  auto& s = srv();
  auto expect_reject = [&](const std::string& ticket, const std::string& hash, const char* why) {
    Client c(s.port);
    c.hello(ticket, hash);
    c.pump(500ms);
    CHECK_MESSAGE(c.rejected.has_value(), why);
    CHECK_MESSAGE(c.player_id == 0, why);
  };
  auto t = s.ticket(6);
  t[5] = t[5] == 'A' ? 'B' : 'A';
  expect_reject(t, s.world_hash, "ticket bị sửa");
  expect_reject(s.ticket(7, -10), s.world_hash, "ticket hết hạn");
  expect_reject(s.ticket(8, 60, "server-khac"), s.world_hash, "ticket cho server khác");
  expect_reject(s.ticket(9), "hash-khac", "world hash khác");
  const auto once = s.ticket(10);
  {
    Client c(s.port);
    c.hello(once, s.world_hash);
    c.pump(300ms);
    CHECK(c.player_id != 0);
  }
  expect_reject(once, s.world_hash, "dùng lại ticket");
}

TEST_CASE("server: rớt mạng rồi kết nối lại giữ nguyên nhân vật và vị trí") {
  auto& s = srv();
  uint32_t id = 0;
  net::EntityState pos{};
  {
    Client c(s.port);
    c.hello(s.ticket(11), s.world_hash);
    c.pump(300ms);
    id = c.player_id;
    REQUIRE(id);
    c.drive(127, 0, 0, 700ms);
    pos = *c.entity(id);
  }  // ngắt kết nối đột ngột (disconnect_now)
  std::this_thread::sleep_for(300ms);
  Client again(s.port);
  again.hello(s.ticket(11), s.world_hash);  // ticket MỚI cho cùng nhân vật
  again.pump(400ms);
  CHECK(again.player_id == id);
  const auto now = again.entity(id);
  REQUIRE(now);
  CHECK(glm::distance(glm::vec2(now->x_cm, now->z_cm), glm::vec2(pos.x_cm, pos.z_cm)) < 150.0f);
}
