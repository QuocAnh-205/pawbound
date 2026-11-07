// pawbound_bot — N client headless trong một process để load test dedicated server.
// Mỗi bot: kết nối ENet thật, Hello với ticket dev riêng, gửi input 30 Hz (đi lang thang, nước rút,
// cắn, tương tác), ping 1 Hz đo RTT. Có thể mô phỏng mất gói/trễ ở phía client.
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <format>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <enet/enet.h>

#include "core/log.hpp"
#include "net/netsim.hpp"
#include "net/protocol.hpp"
#include "core/text.hpp"
#include "net/ticket.hpp"
#include "world/world_io.hpp"

using namespace pb;
using Clock = std::chrono::steady_clock;

namespace {
struct Bot {
  int index = 0;
  ENetHost* host = nullptr;
  ENetPeer* peer = nullptr;
  bool connected = false, welcomed = false, rejected = false, disconnected = false;
  uint32_t seq = 0, player_id = 0;
  uint64_t snapshots = 0, max_seen_players = 0, events = 0, chat_rx = 0;
  int chat_sent = 0;
  double welcomed_at = 0;
  std::vector<double> rtt_ms;
  double next_ping = 0, next_turn = 0;
  std::unordered_map<uint32_t, double> ping_sent;
  int8_t mx = 0, mz = 0;
  uint16_t buttons = 0;
  std::mt19937 rng;
  std::unique_ptr<net::NetSim> sim;
};

int64_t unix_now() {
  return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
double pct(std::vector<double> v, double p) {
  if (v.empty()) return 0;
  std::sort(v.begin(), v.end());
  return v[std::min(v.size() - 1, static_cast<size_t>(p * static_cast<double>(v.size() - 1) + 0.5))];
}
}  // namespace

int main(int argc, char** argv) {
  std::string host_name = "127.0.0.1", sk_path = "config/dev_keys/ticket_sk.b64", world_dir = "out/world_demo",
              server_id = "dev-1", out_json, fixed_ticket;  // --ticket: dùng ticket do Backend cấp (chỉ cho --count 1)
  int count = 2, port = net::kDefaultPort, char_base = 10000, chat_count = 1;
  std::string chat_text;  // --chat: mỗi bot gửi chat gần sau khi vào game (test)
  bool still = false;     // --still: đứng yên (test chat theo bán kính)
  double chat_delay = 1.0;
  double duration = 30, ramp = 0.05;
  net::NetSimSettings ns;
  const auto args = utf8_args(argc, argv);  // UTF-8 (Windows: argv gốc theo mã ANSI làm hỏng chữ tiếng Việt)
  for (int i = 1; i < argc; ++i) {
    std::string_view s = args[static_cast<size_t>(i)];
    auto next = [&]() -> std::string { return i + 1 < argc ? args[static_cast<size_t>(++i)] : ""; };
    if (s == "--host") host_name = next();
    else if (s == "--port") port = std::stoi(next());
    else if (s == "--count") count = std::stoi(next());
    else if (s == "--duration") duration = std::stod(next());
    else if (s == "--ramp") ramp = std::stod(next());
    else if (s == "--sk") sk_path = next();
    else if (s == "--world") world_dir = next();
    else if (s == "--server-id") server_id = next();
    else if (s == "--char-base") char_base = std::stoi(next());
    else if (s == "--sim-loss") ns.loss = std::stod(next());
    else if (s == "--sim-latency") ns.latency_ms = std::stod(next());
    else if (s == "--sim-jitter") ns.jitter_ms = std::stod(next());
    else if (s == "--out") out_json = next();
    else if (s == "--ticket") fixed_ticket = next();
    else if (s == "--chat") chat_text = next();
    else if (s == "--chat-count") chat_count = std::stoi(next());
    else if (s == "--still") still = true;
    else if (s == "--chat-delay") chat_delay = std::stod(next());
    else {
      std::fprintf(stderr, "cách dùng: pawbound_bot [--count n] [--duration s] [--host h] [--port p] [--world dir] [--sk file] "
                           "[--sim-loss 0..1 --sim-latency ms --sim-jitter ms] [--out kết_quả.json]\n");
      return 2;
    }
  }
  if (enet_initialize() != 0 || !net::crypto_init()) return 1;
  if (!fixed_ticket.empty() && count != 1) {
    log::fatal("bot", "--ticket chỉ dùng với --count 1 (mỗi ticket dùng một lần)");
    return 2;
  }
  net::SecretKey sk{};
  if (fixed_ticket.empty()) {
    std::ifstream f(sk_path);
    std::string b64;
    f >> b64;
    if (!f || !net::from_base64(b64, sk.data(), sk.size())) {
      log::fatal("bot", "không đọc được {}", sk_path);
      return 1;
    }
  }
  auto lr = world::load_world(world_dir);
  if (!lr.ok()) {
    log::fatal("bot", "{}", lr.error);
    return 1;
  }
  const std::string hash = world::logical_hash(lr.world);

  std::vector<Bot> bots(static_cast<size_t>(count));
  const auto t0 = Clock::now();
  auto now = [&] { return std::chrono::duration<double>(Clock::now() - t0).count(); };
  for (int i = 0; i < count; ++i) {
    auto& b = bots[static_cast<size_t>(i)];
    b.index = i;
    b.rng.seed(static_cast<uint32_t>(1234 + i));
    b.sim = std::make_unique<net::NetSim>(ns, static_cast<uint64_t>(99 + i));
  }
  auto send = [&](Bot& b, net::PacketType t, const std::vector<uint8_t>& pl, bool reliable) {
    auto bytes = net::encode({net::kProtocolVersion, t, ++b.seq, 0, 0}, pl);
    auto raw = [&b, reliable](const std::vector<uint8_t>& x) {
      if (!b.peer || b.disconnected) return;
      enet_peer_send(b.peer, reliable ? 0 : 1, enet_packet_create(x.data(), x.size(), reliable ? ENET_PACKET_FLAG_RELIABLE : 0));
    };
    if (reliable) raw(bytes);
    else b.sim->push(now(), std::move(bytes), raw);
  };
  int started = 0;
  double next_start = 0;
  const double dt = 1.0 / 30.0;
  double next_tick = 0;
  while (now() < duration) {
    const double t = now();
    // Tăng dần số bot (ramp) để tránh dồn handshake cùng lúc
    if (started < count && t >= next_start) {
      auto& b = bots[static_cast<size_t>(started++)];
      b.host = enet_host_create(nullptr, 1, 2, 0, 0);
      ENetAddress a{};
      enet_address_set_host(&a, host_name.c_str());
      a.port = static_cast<enet_uint16>(port);
      b.peer = enet_host_connect(b.host, &a, 2, 0);
      next_start = t + ramp;
    }
    for (auto& b : bots) {
      if (!b.host) continue;
      ENetEvent ev;
      while (enet_host_service(b.host, &ev, 0) > 0) {
        if (ev.type == ENET_EVENT_TYPE_CONNECT) {
          b.connected = true;
          const auto ticket = !fixed_ticket.empty() ? fixed_ticket : net::sign_ticket({static_cast<uint64_t>(char_base + b.index), static_cast<uint64_t>(char_base + b.index),
                                                server_id, unix_now() + 120, net::random_jti()}, sk);
          send(b, net::PacketType::Hello, net::write(net::Hello{net::kProtocolVersion, ticket, hash, "bot"}), true);
        } else if (ev.type == ENET_EVENT_TYPE_DISCONNECT) {
          b.disconnected = true;
        } else if (ev.type == ENET_EVENT_TYPE_RECEIVE) {
          net::Header h;
          std::span<const uint8_t> pl;
          if (net::decode_header(std::span(ev.packet->data, ev.packet->dataLength), h, pl)) {
            if (h.type == net::PacketType::Welcome) {
              net::Welcome w;
              if (net::read(pl, w)) {
                b.welcomed = true;
                b.player_id = w.player_id;
                b.welcomed_at = t;
              }
            } else if (h.type == net::PacketType::Reject) {
              b.rejected = true;
            } else if (h.type == net::PacketType::Snapshot) {
              net::Snapshot s;
              if (net::read(pl, s)) {
                ++b.snapshots;
                uint64_t players = 0;
                for (const auto& e : s.entities) players += e.kind == net::kPlayer;
                b.max_seen_players = std::max(b.max_seen_players, players);
              }
            } else if (h.type == net::PacketType::Pong) {
              if (auto it = b.ping_sent.find(h.sequence); it != b.ping_sent.end()) {
                b.rtt_ms.push_back((t - it->second) * 1000.0);
                b.ping_sent.erase(it);
              }
            } else if (h.type == net::PacketType::Event) {
              ++b.events;
              net::EventMsg e;
              if (!chat_text.empty() && net::read(pl, e)) std::printf("EVENT %d %u %s\n", b.index, e.code, e.text.c_str());
            } else if (h.type == net::PacketType::ChatMsg) {
              net::ChatMsg m;
              if (net::read(pl, m)) {
                ++b.chat_rx;
                std::printf("CHAT %d %llu %s\n", b.index, static_cast<unsigned long long>(m.from_user), m.text.c_str());
              }
            }
          }
          enet_packet_destroy(ev.packet);
        }
      }
    }
    if (t >= next_tick) {
      next_tick += dt;
      for (auto& b : bots) {
        if (!b.welcomed || b.disconnected) continue;
        // Chat: gửi chat_count tin liên tiếp (cách 50 ms) sau khi vào 1 s — kiểm tra phát theo bán kính + rate limit
        if (!chat_text.empty() && b.chat_sent < chat_count && t >= b.welcomed_at + chat_delay + 0.05 * b.chat_sent) {
          send(b, net::PacketType::ChatSay, net::write(net::ChatSay{chat_text}), true);
          ++b.chat_sent;
        }
        if (still) {
          b.mx = b.mz = 0;
          b.buttons = 0;
          b.next_turn = t + 1e9;
        }
        if (t >= b.next_turn) {  // đổi hướng 1,5–4 s; đôi khi nước rút, cắn, uống
          std::uniform_int_distribution<int> d(-127, 127);
          b.mx = static_cast<int8_t>(d(b.rng));
          b.mz = static_cast<int8_t>(d(b.rng));
          b.buttons = (b.rng() % 3 == 0) ? net::kSprint : 0;
          b.next_turn = t + 1.5 + (b.rng() % 2500) / 1000.0;
        }
        uint16_t btn = b.buttons;
        if (!still && b.rng() % 45 == 0) btn |= net::kAttack;
        if (!still && b.rng() % 90 == 0) btn |= net::kInteract;
        net::InputPacket ip{{{b.seq + 1, b.mx, b.mz, btn, 0}}};
        send(b, net::PacketType::Input, net::write(ip), false);
        if (t >= b.next_ping) {
          b.next_ping = t + 1.0;
          b.ping_sent[b.seq + 1] = t;
          send(b, net::PacketType::Ping, {}, false);
        }
      }
    }
    for (auto& b : bots) {
      if (!b.host) continue;
      b.sim->pump(t);
      enet_host_flush(b.host);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  // Tổng kết
  int welcomed = 0, rejected = 0, dropped = 0;
  uint64_t chat_rx = 0;
  for (const auto& b : bots) chat_rx += b.chat_rx;
  uint64_t snaps = 0, max_seen = 0;
  std::vector<double> rtts;
  for (auto& b : bots) {
    welcomed += b.welcomed;
    rejected += b.rejected;
    dropped += b.disconnected;
    snaps += b.snapshots;
    max_seen = std::max(max_seen, b.max_seen_players);
    rtts.insert(rtts.end(), b.rtt_ms.begin(), b.rtt_ms.end());
    if (b.peer && !b.disconnected) enet_peer_disconnect_now(b.peer, 0);
    if (b.host) {
      enet_host_flush(b.host);
      enet_host_destroy(b.host);
    }
  }
  const double snap_rate = welcomed ? static_cast<double>(snaps) / welcomed / duration : 0;
  const std::string summary = std::format(
      R"({{"bots":{},"welcomed":{},"rejected":{},"disconnected":{},"snapshots_per_bot_s":{:.1f},"max_players_seen":{},)"
      R"("rtt_p50_ms":{:.1f},"rtt_p95_ms":{:.1f},"rtt_p99_ms":{:.1f},"sim_loss":{},"sim_latency_ms":{},"chat_rx":{}}})",
      count, welcomed, rejected, dropped, snap_rate, max_seen, pct(rtts, 0.5), pct(rtts, 0.95), pct(rtts, 0.99), ns.loss,
      ns.latency_ms, chat_rx);
  std::printf("%s\n", summary.c_str());
  if (!out_json.empty()) std::ofstream(out_json) << summary << '\n';
  enet_deinitialize();
  return welcomed == count && dropped == 0 ? 0 : 3;
}
