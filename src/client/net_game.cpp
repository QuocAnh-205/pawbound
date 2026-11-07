#include "client/net_game.hpp"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <format>
#include <thread>

#include <enet/enet.h>
#include <glm/geometric.hpp>

#include "core/log.hpp"
#include "sim/gameplay.hpp"
#include "sim/world_sim.hpp"

namespace pb::client {
namespace {
constexpr double kInterpDelay = 0.1;  // giây
float yaw_of(uint16_t y) { return static_cast<float>(y) / 65535.0f * 6.2831853f; }
glm::vec3 pos_of(const net::EntityState& e) {
  return {static_cast<float>(e.x_cm) / 100.0f, static_cast<float>(e.y_cm) / 100.0f, static_cast<float>(e.z_cm) / 100.0f};
}
}  // namespace

NetGame::NetGame(render::Renderer& r, const sim::DogCatalog& cat, const std::filesystem::path& asset_root) : r_(r), cat_(cat) {
  lib_ = std::make_unique<ModelLibrary>(r, asset_root);
  npc_view_ = std::make_unique<NpcView>(r, *lib_, cat);
}

NetGame::~NetGame() {
  if (peer_) {
    send(net::PacketType::Bye, {}, true);
    enet_host_flush(host_);
    enet_peer_disconnect_now(peer_, 0);
  }
  if (host_) enet_host_destroy(host_);
  enet_deinitialize();
}

bool NetGame::connect(const std::string& host, uint16_t port, const std::string& ticket, const std::string& world_hash,
                      std::string& error) {
  if (enet_initialize() != 0) {
    error = "enet_initialize thất bại";
    return false;
  }
  host_ = enet_host_create(nullptr, 1, 2, 0, 0);
  ENetAddress a{};
  if (enet_address_set_host(&a, host.c_str()) != 0) {
    error = "không phân giải được " + host;
    return false;
  }
  a.port = port;
  peer_ = enet_host_connect(host_, &a, 2, 0);
  ENetEvent ev;
  if (enet_host_service(host_, &ev, 3000) <= 0 || ev.type != ENET_EVENT_TYPE_CONNECT) {
    error = std::format("không kết nối được {}:{}", host, port);
    return false;
  }
  send(net::PacketType::Hello, net::write(net::Hello{net::kProtocolVersion, ticket, world_hash, "client-0.1"}), true);
  // Chờ Welcome/Reject tối đa 3 s
  for (int i = 0; i < 300 && !welcomed_ && reject_.empty() && !lost_; ++i) {
    poll();
    enet_host_flush(host_);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  if (!welcomed_) {
    error = reject_.empty() ? "server không trả lời Hello" : "server từ chối: " + reject_;
    return false;
  }
  log::info("net", "đã vào server {}:{} với tư cách người chơi #{}", host, port, player_id_);
  return true;
}

void NetGame::send(net::PacketType t, const std::vector<uint8_t>& payload, bool reliable) {
  if (!peer_) return;
  const auto b = net::encode({net::kProtocolVersion, t, ++out_seq_, 0, 0}, payload);
  enet_peer_send(peer_, reliable ? 0 : 1, enet_packet_create(b.data(), b.size(), reliable ? ENET_PACKET_FLAG_RELIABLE : 0));
}

void NetGame::poll() {
  ENetEvent ev;
  while (host_ && enet_host_service(host_, &ev, 0) > 0) {
    if (ev.type == ENET_EVENT_TYPE_DISCONNECT) {
      lost_ = true;
      log::warn("net", "mất kết nối tới server");
      continue;
    }
    if (ev.type != ENET_EVENT_TYPE_RECEIVE) continue;
    net::Header h;
    std::span<const uint8_t> pl;
    if (net::decode_header(std::span(ev.packet->data, ev.packet->dataLength), h, pl)) {
      last_rx_ = clock_;
      switch (h.type) {
        case net::PacketType::Welcome: {
          net::Welcome w;
          if (net::read(pl, w)) {
            welcomed_ = true;
            player_id_ = w.player_id;
          }
          break;
        }
        case net::PacketType::Reject: {
          net::Reject r;
          if (net::read(pl, r)) reject_ = r.reason;
          break;
        }
        case net::PacketType::Snapshot: {
          net::Snapshot s;
          if (net::read(pl, s)) {
            self_ = s.self;
            snaps_.push_back({clock_, std::move(s)});
            while (snaps_.size() > 30) snaps_.pop_front();
          }
          break;
        }
        case net::PacketType::Event: {
          net::EventMsg e;
          if (net::read(pl, e)) {
            log_.emplace_back(clock_, e.text);
            while (log_.size() > 5) log_.pop_front();
          }
          break;
        }
        case net::PacketType::ChatMsg: {
          net::ChatMsg m;
          if (net::read(pl, m) && !blocked_.contains(m.from_user)) {
            chat_.emplace_back(clock_, std::format("{}#{}: {}", m.from_entity == player_id_ ? "Ban" : "Cho", m.from_entity, m.text));
            while (chat_.size() > 8) chat_.pop_front();
          }
          break;
        }
        case net::PacketType::Pong:
          if (auto it = pings_.find(h.sequence); it != pings_.end()) {
            rtt_.push_back((clock_ - it->second) * 1000.0);
            if (rtt_.size() > 20) rtt_.erase(rtt_.begin());
            pings_.erase(it);
          }
          break;
        default: break;
      }
    }
    enet_packet_destroy(ev.packet);
  }
}

void NetGame::fixed_update(const InputState& in, const geom::Camera25D& cam, float dt) {
  clock_ += dt;
  poll();
  if (!welcomed_ || lost_) return;
  // Ý định: hướng di chuyển trong không gian thế giới (camera-relative) + nút. KHÔNG gửi vị trí.
  // Đang gõ chat: gửi lệnh "đứng yên" (vẫn gửi để server không coi là mất gói).
  const InputState idle{};
  const InputState& inp = typing_ ? idle : in;
  const glm::vec3 d = cam.ground_right() * inp.move.x + cam.ground_forward() * inp.move.y;
  net::InputCmd c;
  c.seq = ++seq_;
  c.move_x = static_cast<int8_t>(std::clamp(d.x * 127.0f, -127.0f, 127.0f));
  c.move_z = static_cast<int8_t>(std::clamp(d.z * 127.0f, -127.0f, 127.0f));
  c.buttons = static_cast<uint16_t>((inp.sprint ? net::kSprint : 0) | (inp.walk ? net::kWalk : 0) | (inp.jump ? net::kJump : 0) |
                                    (inp.interact ? net::kInteract : 0) | (inp.attack ? net::kAttack : 0) |
                                    (inp.dodge ? net::kDodge : 0) | (inp.sniff ? net::kSniff : 0));
  recent_.push_front(c);
  while (recent_.size() > 3) recent_.pop_back();
  send(net::PacketType::Input, net::write(net::InputPacket{{recent_.begin(), recent_.end()}}), false);
  if (clock_ >= next_ping_) {
    next_ping_ = clock_ + 1.0;
    pings_[out_seq_ + 1] = clock_;
    send(net::PacketType::Ping, {}, false);
  }
  enet_host_flush(host_);
}

bool NetGame::sample(const std::deque<Timed>& buf, double t, uint32_t id, net::EntityState& out) {
  const net::EntityState* a = nullptr;
  const net::EntityState* b = nullptr;
  double ta = 0, tb = 0;
  for (const auto& s : buf) {
    for (const auto& e : s.s.entities) {
      if (e.id != id) continue;
      if (s.t <= t) {
        a = &e;
        ta = s.t;
      } else if (!b) {
        b = &e;
        tb = s.t;
      }
    }
  }
  if (!a && !b) return false;
  if (!a || !b) {
    out = a ? *a : *b;
    return true;
  }
  const float f = static_cast<float>(std::clamp((t - ta) / std::max(1e-6, tb - ta), 0.0, 1.0));
  out = *a;
  out.x_cm = static_cast<int32_t>(std::lround(a->x_cm + (b->x_cm - a->x_cm) * f));
  out.z_cm = static_cast<int32_t>(std::lround(a->z_cm + (b->z_cm - a->z_cm) * f));
  out.y_cm = static_cast<int16_t>(std::lround(a->y_cm + (b->y_cm - a->y_cm) * f));
  float dyaw = static_cast<float>(b->yaw) - static_cast<float>(a->yaw);
  if (dyaw > 32768) dyaw -= 65536;
  if (dyaw < -32768) dyaw += 65536;
  out.yaw = static_cast<uint16_t>(std::fmod(static_cast<float>(a->yaw) + dyaw * f + 65536.0f, 65536.0f));
  return true;
}

void NetGame::update_visual(float dt) {
  if (snaps_.empty()) return;
  const double t = clock_ - kInterpDelay;
  const auto& latest = snaps_.back().s;
  npc_frame_.clear();
  std::map<uint32_t, bool> seen;
  for (const auto& e : latest.entities) {
    net::EntityState st = e;
    if (e.id != player_id_) sample(snaps_, t, e.id, st);  // bản thân: mới nhất; còn lại: nội suy
    const glm::vec3 p = pos_of(st);
    if (e.kind == net::kPlayer) {
      auto& dog = dogs_[e.id];
      if (!dog) dog = std::make_unique<DogActor>(r_, *lib_, cat_, cat_.breeds.front().id, p);
      if (e.id != player_id_) dog->set_tint({0.85f, 0.85f, 1.0f, 1});
      dog->set_visual(p, yaw_of(st.yaw), static_cast<sim::Gait>(st.gait));
      seen[e.id] = true;
      if (e.id == player_id_) self_pos_ = p;
    } else {
      sim::Npc n;
      n.id = e.id;
      n.kind = (e.kind == net::kNpcHuman || e.kind == net::kThief) ? sim::NpcKind::Human : sim::NpcKind::Dog;
      n.pers = e.kind == net::kThief       ? sim::Personality::Aggressive   // kẻ trộm chó
               : e.kind == net::kCompanion ? sim::Personality::Friendly     // bạn đồng hành
                                           : static_cast<sim::Personality>((e.flags >> 2) & 7);
      n.motor.pos = p;
      n.motor.yaw = yaw_of(st.yaw);
      n.motor.gait = static_cast<sim::Gait>(st.gait);
      n.sv.hp = st.hp;
      n.sv.life = (e.flags & 1) ? sim::LifeState::Alive : sim::LifeState::Downed;
      npc_frame_.push_back(n);
    }
  }
  for (auto it = dogs_.begin(); it != dogs_.end();) it = seen.contains(it->first) ? std::next(it) : dogs_.erase(it);
  for (auto& [id, d] : dogs_) d->update_visual(dt);
  npc_view_->after_tick(npc_frame_);
  npc_view_->after_tick(npc_frame_);  // prev = cur (đã nội suy ở trên)
  npc_view_->update_visual(npc_frame_, dt);
}

void NetGame::append_draws(bool debug, std::vector<render::DrawItem>& out) {
  for (auto& [id, d] : dogs_) d->append_draws(1.0f, debug, out);
  npc_view_->append(npc_frame_, 1.0f, debug, out);
}

void NetGame::draw_hud(render::Overlay& o, int w, int h) const {
  const float s = std::max(0.75f, static_cast<float>(h) / 720.0f);
  const float x = 16 * s, bw = 180 * s, bh = 12 * s;
  float y = 16 * s;
  o.rect(x - 8 * s, y - 8 * s, bw + 140 * s, 5 * (bh + 8 * s) + 12 * s, {0, 0, 0, 0.6f});
  const std::pair<const char*, uint8_t> bars[] = {{"HP", self_.hp}, {"FOOD", self_.hunger}, {"WATER", self_.thirst},
                                                  {"STAMINA", self_.stamina}, {"REST", self_.rest}};
  const glm::vec4 col[] = {{0.85f, 0.2f, 0.2f, 1}, {0.95f, 0.6f, 0.15f, 1}, {0.2f, 0.55f, 0.95f, 1}, {0.95f, 0.85f, 0.2f, 1}, {0.6f, 0.4f, 0.9f, 1}};
  for (size_t i = 0; i < 5; ++i) {
    o.text(x, y, bars[i].first, {1, 1, 1, 1}, 1.6f * s);
    o.bar(x + 80 * s, y, bw, bh, bars[i].second / 100.0f, col[i], {0.15f, 0.15f, 0.15f, 0.8f});
    o.text(x + 86 * s + bw, y, std::to_string(bars[i].second), {0.8f, 0.8f, 0.8f, 1}, 1.6f * s);
    y += bh + 8 * s;
  }
  const std::string st = status();
  o.rect(static_cast<float>(w) - render::Overlay::text_width(st, 1.6f * s) - 28 * s, 10 * s,
         render::Overlay::text_width(st, 1.6f * s) + 18 * s, 24 * s, {0, 0, 0, 0.5f});
  o.text(static_cast<float>(w) - render::Overlay::text_width(st, 1.6f * s) - 19 * s, 16 * s, st,
         connected() ? glm::vec4(0.6f, 1, 0.6f, 1) : glm::vec4(1, 0.4f, 0.3f, 1), 1.6f * s);
  // Trạng thái bắt chó (server quyết định; client chỉ hiển thị)
  if (self_.capture != 0) {
    static const char* txt[] = {"", "KE TROM DANG DEN! Chay (Shift) / can (J) / vao khu an toan", "BI BAT - cho dong doi giai cuu",
                                "DANG DUOC GIAI CUU..."};
    const std::string line = std::format("{}  ({}:{:02d})", txt[std::min<int>(self_.capture, 3)], self_.capture_s / 60, self_.capture_s % 60);
    const float tw = render::Overlay::text_width(line, 2.2f * s);
    const float cx = (static_cast<float>(w) - tw) * 0.5f, cy = 70 * s;
    o.rect(cx - 12 * s, cy - 8 * s, tw + 24 * s, 30 * s, self_.capture == 1 ? glm::vec4(0.6f, 0.05f, 0.05f, 0.85f) : glm::vec4(0.1f, 0.1f, 0.3f, 0.85f));
    o.text(cx, cy, line, {1, 1, 1, 1}, 2.2f * s);
  }
  float ly = static_cast<float>(h) - 40 * s;
  if (typing_) {  // ô nhập chat
    const std::string line = "Chat: " + render::Overlay::to_display(draft_) + "_";
    o.rect(10 * s, ly - 6 * s, std::max(320 * s, render::Overlay::text_width(line, 1.6f * s) + 16 * s), 24 * s, {0, 0, 0, 0.7f});
    o.text(16 * s, ly, line, {1, 1, 1, 1}, 1.6f * s);
    ly -= 28 * s;
  }
  for (auto it = chat_.rbegin(); it != chat_.rend(); ++it) {
    if (clock_ - it->first > 30 && !typing_) continue;  // tin cũ mờ đi sau 30 s
    o.text(16 * s, ly, it->second, {0.75f, 0.95f, 1, 1}, 1.6f * s);
    ly -= 20 * s;
  }
  for (auto it = log_.rbegin(); it != log_.rend(); ++it) {
    o.text(16 * s, ly, it->second, {1, 1, 0.8f, 1}, 1.6f * s);
    ly -= 20 * s;
  }
}

std::string NetGame::status() const {
  double rtt = 0;
  for (double r : rtt_) rtt += r;
  if (!rtt_.empty()) rtt /= static_cast<double>(rtt_.size());
  size_t players = snaps_.empty() ? 0 : std::count_if(snaps_.back().s.entities.begin(), snaps_.back().s.entities.end(),
                                                       [](const net::EntityState& e) { return e.kind == net::kPlayer; });
  return std::format("ONLINE #{} | {} players nearby | RTT {:.0f} ms | {:02d}:{:02d} {}", player_id_, players, rtt,
                     self_.hour_x100 / 100, self_.hour_x100 % 100 * 60 / 100,
                     sim::to_string(static_cast<sim::Weather>(std::min<int>(self_.weather, 4))));
}

void NetGame::chat_edit(const std::string& utf8, int backspaces) {
  if (!typing_) return;
  for (int i = 0; i < backspaces && !draft_.empty(); ++i) {
    while (!draft_.empty() && (static_cast<unsigned char>(draft_.back()) & 0xC0) == 0x80) draft_.pop_back();  // byte tiếp UTF-8
    if (!draft_.empty()) draft_.pop_back();
  }
  if (draft_.size() + utf8.size() <= net::kMaxChatBytes) draft_ += utf8;
}

bool NetGame::chat_submit() {
  typing_ = false;
  const std::string text = net::sanitize_chat(draft_);
  draft_.clear();
  if (text.empty() || !welcomed_ || lost_) return false;
  send(net::PacketType::ChatSay, net::write(net::ChatSay{text}), true);
  enet_host_flush(host_);
  return true;
}

}  // namespace pb::client
