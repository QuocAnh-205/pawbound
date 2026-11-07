#include "server/game_server.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <format>
#include <fstream>
#include <sstream>
#include <thread>

#include <enet/enet.h>
#include <glm/geometric.hpp>
#include <nlohmann/json.hpp>

#include "core/log.hpp"
#include "core/version.hpp"
#include "sim/dog_archetype.hpp"
#include "world/world_io.hpp"

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#endif

namespace pb::server {
namespace {
using clock_t_ = std::chrono::steady_clock;
glm::vec2 xz(glm::vec3 v) { return {v.x, v.z}; }

double rss_mb() {
#if defined(_WIN32)
  PROCESS_MEMORY_COUNTERS pmc{};
  if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) return static_cast<double>(pmc.WorkingSetSize) / (1024.0 * 1024.0);
  return 0;
#else
  std::ifstream f("/proc/self/statm");
  long pages = 0, rss = 0;
  f >> pages >> rss;
  return static_cast<double>(rss) * 4096.0 / (1024.0 * 1024.0);
#endif
}
double pct(std::vector<double> v, double p) {
  if (v.empty()) return 0;
  std::sort(v.begin(), v.end());
  return v[std::min(v.size() - 1, static_cast<size_t>(p * static_cast<double>(v.size() - 1) + 0.5))];
}
int64_t unix_now() { return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }
uint8_t u8clamp(float v) { return static_cast<uint8_t>(std::clamp(v, 0.0f, 255.0f)); }
}  // namespace

GameServer::GameServer(ServerConfig cfg) : cfg_(std::move(cfg)), netsim_(cfg_.netsim, 0xBEEF) {}

GameServer::~GameServer() {
  if (host_) {
    for (auto& [peer, c] : conns_) enet_peer_disconnect_now(peer, 0);
    enet_host_destroy(host_);
  }
  enet_deinitialize();
}

bool GameServer::init(std::string& error) {
  if (enet_initialize() != 0) {
    error = "enet_initialize thất bại";
    return false;
  }
  if (!net::crypto_init()) {
    error = "libsodium init thất bại";
    return false;
  }
  // Public key xác minh ticket
  {
    std::ifstream f(cfg_.ticket_pubkey);
    std::string b64;
    f >> b64;
    if (!f || !net::from_base64(b64, ticket_pk_.data(), ticket_pk_.size())) {
      error = "không đọc được public key ticket: " + cfg_.ticket_pubkey.string() + " (tạo bằng: pawbound net keygen)";
      return false;
    }
  }
  auto lr = world::load_world(cfg_.world_dir);
  if (!lr.ok()) {
    error = "nạp world: " + lr.error;
    return false;
  }
  world_ = std::move(lr.world);
  world_hash_ = world::logical_hash(world_);
  if (!lr.stored_hash.empty() && lr.stored_hash != world_hash_) {
    error = "logical hash world không khớp file logical_hash.txt";
    return false;
  }
  ConfigDiagnostics d;
  gc_ = sim::load_gameplay_config(cfg_.data_root / "data/gameplay/gameplay.json", d);
  nc_ = sim::load_npc_config(cfg_.data_root / "data/gameplay/npcs.json", d);
  wc_ = sim::load_world_sim_config(cfg_.data_root / "data/gameplay/world_sim.json", d);
  auto cat = sim::load_dog_catalog(cfg_.data_root / "data/dogs/breeds.json", d);
  if (!d.ok()) {
    error = "config: " + d.errors.front();
    return false;
  }
  dog_stats_ = sim::movement_for(cat, cat.breeds.front());
  {  // Từ cấm cho chat gần — cùng nguồn với Backend
    std::ifstream sf(cfg_.data_root / "data/social/social.json");
    const auto sj = nlohmann::json::parse(sf, nullptr, false);
    if (!sj.is_discarded() && sj.contains("chat") && sj["chat"].contains("banned_words"))
      for (const auto& w : sj["chat"]["banned_words"])
        if (w.is_string() && !w.get<std::string>().empty()) banned_words_.push_back(w.get<std::string>());
  }
  col_ = std::make_unique<sim::CollisionWorld>(world_);
  nav_ = std::make_unique<sim::NavGraph>(world_);
  resources_ = sim::make_resource_nodes(world_, gc_);
  npcs_ = std::make_unique<sim::NpcManager>(world_, *col_, *nav_, gc_, nc_, &resources_);
  wsim_ = std::make_unique<sim::WorldSim>(wc_, world_, world_.seed);
  for (const auto& c : world_.chunks)
    for (const auto& p : c.pois)
      if (p.kind == world::PoiKind::Spawn && spawn_ == glm::vec2(0.0f)) spawn_ = {world::to_m(p.x), world::to_m(p.z)};

  ENetAddress addr{};
  addr.host = ENET_HOST_ANY;
  addr.port = cfg_.port;
  host_ = enet_host_create(&addr, static_cast<size_t>(cfg_.max_players + 8), 2, 0, 0);
  if (!host_) {
    error = std::format("không mở được cổng UDP {}", cfg_.port);
    return false;
  }
  log::info("server", "PAWBOUND server '{}' cổng {} | world seed {} hash {}… | {} NPC config, tối đa {} người", cfg_.server_id,
            cfg_.port, world_.seed, world_hash_.substr(0, 12), nc_.max_active, cfg_.max_players);
  if (netsim_.active())
    log::warn("server", "MÔ PHỎNG MẠNG XẤU: mất {:.0f}% trễ {:.0f}±{:.0f} ms (chỉ dùng để test)", cfg_.netsim.loss * 100,
              cfg_.netsim.latency_ms, cfg_.netsim.jitter_ms);
  if (!cfg_.backend_url.empty()) {
    std::string berr;
    if (!backend_.start(cfg_.backend_url, cfg_.service_token, berr)) {
      error = "backend: " + berr;
      return false;
    }
    log::info("server", "Backend: {} (lưu trạng thái mỗi {:.0f} s, heartbeat mỗi {:.0f} s)", cfg_.backend_url,
              cfg_.autosave_interval_s, cfg_.heartbeat_interval_s);
  } else {
    log::warn("server", "KHÔNG có Backend (--backend): chế độ DEV, trạng thái nhân vật không được lưu");
  }
  return true;
}

size_t GameServer::online_players() const {
  size_t n = 0;
  for (const auto& [id, p] : players_) n += p.online;
  return n;
}

GameServer::Player* GameServer::player_by_id(uint32_t id) {
  auto it = players_.find(id);
  return it == players_.end() ? nullptr : &it->second;
}
GameServer::Conn* GameServer::conn(ENetPeer* peer) {
  auto it = conns_.find(peer);
  return it == conns_.end() ? nullptr : &it->second;
}

void GameServer::send(ENetPeer* peer, net::PacketType t, const std::vector<uint8_t>& payload, bool reliable, uint32_t seq) {
  const auto bytes = net::encode({net::kProtocolVersion, t, seq, 0, static_cast<uint32_t>(tick_)}, payload);
  metrics_.pkts_out++;
  metrics_.bytes_out += bytes.size();
  auto do_send = [this, peer, reliable](const std::vector<uint8_t>& b) {
    if (!conns_.contains(peer)) return;  // peer đã ngắt trong lúc trễ mô phỏng
    ENetPacket* pk = enet_packet_create(b.data(), b.size(), reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
    enet_peer_send(peer, reliable ? 0 : 1, pk);
  };
  if (reliable || !netsim_.active()) do_send(bytes);
  else netsim_.push(now_, bytes, do_send);
}

void GameServer::kick(ENetPeer* peer, const std::string& reason) {
  send(peer, net::PacketType::Reject, net::write(net::Reject{reason}), true);
  enet_host_flush(host_);
  enet_peer_disconnect_later(peer, 0);
  metrics_.kicked++;
  log::warn("server", "kick peer: {}", reason);
}

void GameServer::violation(Conn& c, float amount, const char* why) {
  c.violations += amount;
  if (c.violations >= static_cast<float>(cfg_.max_violations)) kick(c.peer, std::string("quá nhiều gói không hợp lệ (") + why + ")");
}

void GameServer::on_receive(ENetPeer* peer, const uint8_t* data, size_t len) {
  Conn* c = conn(peer);
  if (!c) return;
  metrics_.pkts_in++;
  metrics_.bytes_in += len;
  // Rate limit trước khi parse (rẻ, chống flood)
  if (!c->pkts.take(now_) || !c->bytes.take(now_, static_cast<double>(len))) {
    violation(*c, 0.2f, "rate limit");
    return;
  }
  net::Header h;
  std::span<const uint8_t> pl;
  if (!net::decode_header(std::span(data, len), h, pl)) {
    metrics_.malformed++;
    violation(*c, 1, "header");
    return;
  }
  if (c->player == 0) {  // chưa xác thực: chỉ nhận Hello
    net::Hello hello;
    if (h.type != net::PacketType::Hello || !net::read(pl, hello)) {
      violation(*c, 2, "chưa xác thực");
      return;
    }
    handle_hello(*c, hello);
    return;
  }
  Player* p = player_by_id(c->player);
  if (!p) return;
  switch (h.type) {
    case net::PacketType::Input: {
      net::InputPacket in;
      if (!net::read(pl, in)) {
        metrics_.malformed++;
        violation(*c, 1, "input");
        return;
      }
      // Lệnh cũ hơn lệnh đã nhận bị bỏ (chống replay/trùng do dư thừa). Gói chứa lệnh mới nhất trước.
      for (auto it = in.cmds.rbegin(); it != in.cmds.rend(); ++it) {
        if (p->have_seq && !net::seq_newer(it->seq, p->last_seq)) continue;
        p->last_seq = it->seq;
        p->have_seq = true;
        p->inputs.push_back(*it);
      }
      while (p->inputs.size() > 8) p->inputs.pop_front();  // client gửi quá nhanh: bỏ lệnh cũ
      break;
    }
    case net::PacketType::ChatSay: {
      net::ChatSay m;
      if (!net::read(pl, m)) {
        metrics_.malformed++;
        violation(*c, 1, "chat");
        return;
      }
      if (p->loaded) handle_chat(*p, *c, m);
      break;
    }
    case net::PacketType::Ping: send(peer, net::PacketType::Pong, {}, false, h.sequence); break;
    case net::PacketType::Bye: enet_peer_disconnect_later(peer, 0); break;
    default: violation(*c, 1, "loại gói không được phép"); break;  // vd client gửi Snapshot/Welcome giả
  }
}

void GameServer::handle_hello(Conn& c, const net::Hello& h) {
  auto reject = [&](const std::string& why) {
    metrics_.rejected++;
    log::warn("server", "từ chối kết nối: {}", why);
    kick(c.peer, why);
  };
  if (h.protocol_version != net::kProtocolVersion) return reject("sai phiên bản giao thức");
  if (h.world_hash != world_hash_) return reject("world hash không khớp (client có world khác server)");
  net::TicketClaims claims;
  const auto te = net::verify_ticket(h.ticket, ticket_pk_, cfg_.server_id, unix_now(), &replay_, claims);
  if (te != net::TicketError::Ok) return reject(net::to_string(te));
  // Reconnect: cùng nhân vật -> gắn lại peer, giữ nguyên trạng thái
  Player* p = nullptr;
  for (auto& [id, pl] : players_)
    if (pl.char_id == claims.character_id) p = &pl;
  if (p && p->online) {
    kick(p->peer, "nhân vật đăng nhập ở nơi khác");
    conns_.erase(p->peer);
  }
  if (!p) {
    if (online_players() >= static_cast<size_t>(cfg_.max_players)) return reject("server đầy");
    Player np;
    np.id = next_player_++;
    np.user_id = claims.user_id;
    np.char_id = claims.character_id;
    np.stats = dog_stats_;
    np.inv = sim::Inventory(gc_.inventory_slots);
    glm::vec2 sp = spawn_ + glm::vec2(static_cast<float>(np.id % 5) - 2.0f, static_cast<float>(np.id / 5 % 5) - 2.0f);
    col_->resolve(sp, 0.5f, world_.height_at(sp.x, sp.y), 0.3f);
    np.motor.pos = {sp.x, world_.height_at(sp.x, sp.y), sp.y};
    np.jti = claims.jti;
    p = &players_.emplace(np.id, std::move(np)).first->second;
    log::info("server", "người chơi #{} (user {} nhân vật {}) vào game", p->id, p->user_id, p->char_id);
    if (backend_.enabled()) {  // nạp trạng thái đã lưu; Welcome gửi khi có kết quả
      p->loaded = false;
      backend_.submit({BackendJob::Load, p->id, p->char_id, "GET", std::format("/internal/characters/{}/state", p->char_id), ""});
    }
  } else {
    log::info("server", "người chơi #{} kết nối lại", p->id);
    if (p->jti != claims.jti) {
      end_session(p->jti, "reconnect");
      p->jti = claims.jti;
    }
  }
  p->peer = c.peer;
  p->online = true;
  p->inputs.clear();
  p->have_seq = false;
  c.player = p->id;
  if (p->loaded) welcome(*p);
}

void GameServer::welcome(Player& p) {
  send(p.peer, net::PacketType::Welcome, net::write(net::Welcome{p.id, p.id, static_cast<uint32_t>(tick_), 30}), true);
}

void GameServer::tick_player(Player& p, double dt) {
  // Lấy một lệnh mỗi tick; mất gói thì giữ lệnh trước tối đa 3 tick rồi dừng
  net::InputCmd cmd{};
  if (!p.inputs.empty()) {
    cmd = p.inputs.front();
    p.inputs.pop_front();
    p.last_cmd = cmd;
    p.starved_ticks = 0;
  } else if (++p.starved_ticks <= 3) {
    cmd = p.last_cmd;
    cmd.buttons &= static_cast<uint16_t>(net::kSprint | net::kWalk | net::kSniff);  // không lặp hành động một lần
  }
  const uint16_t pressed = static_cast<uint16_t>(cmd.buttons & ~p.prev_buttons);
  p.prev_buttons = cmd.buttons;
  const auto& sc = gc_.survival;
  const bool alive = p.sv.life == sim::LifeState::Alive;
  // Kẹp đầu vào: độ dài ≤ 1 (client không thể "chạy nhanh hơn" bằng giá trị lớn)
  glm::vec2 move(static_cast<float>(cmd.move_x) / 127.0f, static_cast<float>(cmd.move_z) / 127.0f);
  if (glm::length(move) > 1.0f) move = glm::normalize(move);
  if (glm::length(move) > 0.1f) p.resting = false;
  if (pressed & net::kRest) p.resting = !p.resting;
  sim::MotorInput mi;
  if (alive && !p.resting) {
    mi.move = move;
    mi.walk = cmd.buttons & net::kWalk;
    p.sprinting = (cmd.buttons & net::kSprint) && glm::length(move) > 0.1f && sim::can_sprint(p.sv, sc, p.sprinting);
    mi.sprint = p.sprinting;
    mi.jump = (pressed & net::kJump) && p.motor.grounded && sim::spend_stamina(p.sv, sc, sc.jump_stamina);
  }
  auto st = p.stats;
  const float sm = sim::speed_multiplier(p.sv, sc) * wsim_->effects().move;
  st.walk_speed *= sm;
  st.trot_speed *= sm;
  st.run_speed *= sm;
  st.sprint_speed *= sm;
  sim::step_motor(p.motor, mi, st, *col_, static_cast<float>(dt));
  if (glm::length(move) < 0.1f && cmd.yaw) p.motor.yaw = static_cast<float>(cmd.yaw) / 65535.0f * 6.2831853f;

  sim::Activity act;
  act.moving = glm::length(p.motor.vel) > 0.3f;
  act.sprinting = p.sprinting && act.moving;
  act.resting = p.resting;
  sim::tick_survival(p.sv, sc, act, static_cast<float>(dt));
  sim::tick_combat(p.cb, gc_.combat, static_cast<float>(dt));

  if (alive && (pressed & net::kInteract)) {
    if (auto* n = sim::nearest_node(resources_, xz(p.motor.pos), gc_.interact_range_m)) {
      const auto r = sim::consume_at(*n, xz(p.motor.pos), p.sv, gc_);
      send(p.peer, net::PacketType::Event, net::write(net::EventMsg{1, std::string("interact: ") + sim::to_string(r)}), true);
    }
  }
  if (alive && (pressed & net::kForage))
    if (auto* n = sim::nearest_node(resources_, xz(p.motor.pos), gc_.interact_range_m, world::PoiKind::Food))
      sim::forage_at(*n, xz(p.motor.pos), p.inv, gc_);
  if (alive && (pressed & net::kDodge)) {
    sim::Combatant me{p.motor.pos, p.motor.yaw, true, &p.sv, &p.cb};
    sim::request_dodge(me, gc_);
  }
  if (alive && (pressed & net::kAttack)) {
    // Mục tiêu gần nhất (người chơi khác hoặc NPC) — server chọn, client không chỉ định
    float best = 2.5f;
    Player* tp = nullptr;
    sim::Npc* tn = nullptr;
    for (auto& [id, o] : players_)
      if (id != p.id && o.online && o.sv.life == sim::LifeState::Alive && glm::distance(xz(o.motor.pos), xz(p.motor.pos)) < best) {
        best = glm::distance(xz(o.motor.pos), xz(p.motor.pos));
        tp = &o;
      }
    for (auto& n : npcs_->npcs_mut())
      if (n.sv.life == sim::LifeState::Alive && glm::distance(xz(n.motor.pos), xz(p.motor.pos)) < best) {
        best = glm::distance(xz(n.motor.pos), xz(p.motor.pos));
        tn = &n;
        tp = nullptr;
      }
    const auto zone = world_.zone_at(static_cast<world::cm>(p.motor.pos.x * 100), static_cast<world::cm>(p.motor.pos.z * 100));
    sim::Combatant me{p.motor.pos, p.motor.yaw, true, &p.sv, &p.cb};
    if (tp) {
      sim::Combatant them{tp->motor.pos, tp->motor.yaw, true, &tp->sv, &tp->cb};
      const auto o = sim::request_bite(me, them, zone, gc_, static_cast<float>(sim_time_));
      send(p.peer, net::PacketType::Event, net::write(net::EventMsg{2, std::string("bite: ") + sim::to_string(o.result)}), true);
      if (o.result == sim::BiteResult::Hit)
        send(tp->peer, net::PacketType::Event, net::write(net::EventMsg{3, std::format("bitten by player #{} -{:.0f}", p.id, o.damage)}), true);
    } else if (tn) {
      sim::Combatant them{tn->motor.pos, tn->motor.yaw, false, &tn->sv, &tn->cb};
      const auto o = sim::request_bite(me, them, zone, gc_, static_cast<float>(sim_time_));
      if (o.result == sim::BiteResult::Hit) {
        npcs_->on_attacked(tn->id, p.id);
        noises_.push_back({xz(p.motor.pos), 15.0f, p.id});
      }
      send(p.peer, net::PacketType::Event, net::write(net::EventMsg{2, std::string("bite: ") + sim::to_string(o.result)}), true);
    }
  }
  if (p.sv.life == sim::LifeState::Dead) {
    const auto o = sim::respawn(p.sv, p.inv, xz(p.motor.pos), world_, gc_);
    p.motor.pos = {o.pos.x, world_.height_at(o.pos.x, o.pos.y), o.pos.y};
    p.motor.vel = glm::vec2(0.0f);
    send(p.peer, net::PacketType::Event, net::write(net::EventMsg{4, "respawned"}), true);
  }
}

void GameServer::tick(double dt) {
  ++tick_;
  sim_time_ += dt;
  std::vector<sim::PlayerView> views;
  std::vector<glm::vec3> positions;
  for (auto& [id, p] : players_) {
    if (!p.online || !p.loaded) continue;
    tick_player(p, dt);
    views.push_back({p.id, p.motor.pos, p.motor.yaw, &p.sv, &p.cb});
    positions.push_back(p.motor.pos);
  }
  // Người chơi rớt mạng quá thời gian giữ chỗ -> lưu về Backend (nếu có) rồi mới gỡ
  std::erase_if(players_, [&](auto& kv) {
    Player& p = kv.second;
    if (p.online || now_ - p.offline_since <= cfg_.linger_s) return false;
    if (backend_.enabled() && p.loaded && !p.save_conflict) {
      if (p.save_in_flight) return false;  // chờ lần lưu đang chạy xong rồi lưu lần cuối
      if (!p.final_save_sent) {
        request_save(p, "logout");
        p.final_save_sent = true;
        return false;
      }
    }
    end_session(p.jti, "disconnect");
    log::info("server", "gỡ người chơi #{} (rớt mạng quá {:.0f} s)", kv.first, cfg_.linger_s);
    return true;
  });
  npcs_->set_human_density(wsim_->pedestrian_density());
  const auto events = npcs_->update(static_cast<float>(dt), views, noises_);
  noises_.clear();
  for (const auto& e : events) {
    Player* p = player_by_id(e.player);
    if (!p || !p->online) continue;
    if (e.type == sim::AiEvent::Type::FeedPlayer) p->inv.add(gc_, "meat_bone", 1);
    static const char* names[] = {"fed", "dog_bite", "kick", "shout", "call_help", "growl"};
    send(p->peer, net::PacketType::Event, net::write(net::EventMsg{10, std::string("npc: ") + names[static_cast<int>(e.type)]}), true);
  }
  const auto ch = wsim_->tick(dt, positions, resources_);
  for (const auto& e : ch.started)
    for (auto& [id, p] : players_)
      if (p.online) send(p.peer, net::PacketType::Event, net::write(net::EventMsg{20, std::string("event: ") + sim::to_string(e.type)}), true);
  send_snapshots();
}

void GameServer::send_snapshots() {
  const double interval = 1.0 / cfg_.snapshot_hz;
  for (auto& [id, p] : players_) {
    if (!p.online || !p.loaded || now_ < p.next_snapshot) continue;
    // Cộng dồn (không phải now + interval) để đạt đúng 20 Hz trung bình dù tick là 30 Hz.
    p.next_snapshot = std::max(p.next_snapshot + interval, now_ - interval);
    const bool full = (++p.snap_count % 2) == 0;  // entity xa (> 25 m) chỉ gửi mỗi snapshot thứ hai = 10 Hz
    net::Snapshot s;
    s.self = {p.last_seq, u8clamp(p.sv.hp), u8clamp(p.sv.hunger), u8clamp(p.sv.thirst), u8clamp(p.sv.stamina),
              u8clamp(p.sv.rest), static_cast<uint8_t>(p.sv.life), static_cast<uint16_t>(wsim_->clock().hour() * 100),
              static_cast<uint8_t>(wsim_->weather()), static_cast<uint32_t>(wsim_->clock().day())};
    struct Cand {
      float d;
      net::EntityState e;
    };
    std::vector<Cand> cands;
    auto add = [&](uint32_t eid, uint8_t kind, uint8_t flags, const sim::MotorState& m, float hp) {
      const float d = glm::distance(xz(m.pos), xz(p.motor.pos));
      if (d > cfg_.aoi_radius_m) return;  // Interest management: chỉ gửi entity trong vùng quan tâm
      if (d > 25.0f && !full) return;     // xa: tần suất thấp hơn (multiplayer-design §2)
      cands.push_back({d, {eid, kind, flags, static_cast<int32_t>(std::lround(m.pos.x * 100)), static_cast<int32_t>(std::lround(m.pos.z * 100)),
                           static_cast<int16_t>(std::clamp(std::lround(m.pos.y * 100), -32000L, 32000L)),
                           static_cast<uint16_t>(std::fmod(m.yaw / 6.2831853f + 10.0f, 1.0f) * 65535.0f),
                           static_cast<uint8_t>(m.gait), u8clamp(hp)}});
    };
    for (auto& [oid, o] : players_)
      if (o.online && o.loaded)
        add(oid, net::kPlayer, static_cast<uint8_t>((o.sv.life == sim::LifeState::Alive) | (o.sprinting << 1) | (oid == p.id ? 32 : 0)),
            o.motor, o.sv.hp);
    for (const auto& n : npcs_->npcs())
      add(0x80000000u | n.id, n.kind == sim::NpcKind::Human ? net::kNpcHuman : net::kNpcDog,
          static_cast<uint8_t>((n.sv.life == sim::LifeState::Alive) | (static_cast<int>(n.pers) << 2)), n.motor, n.sv.hp);
    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.d < b.d; });
    for (size_t i = 0; i < cands.size() && i < static_cast<size_t>(net::kMaxSnapshotEntities); ++i) s.entities.push_back(cands[i].e);
    send(p.peer, net::PacketType::Snapshot, net::write(s), false, ++p.out_seq);
  }
}

void GameServer::log_metrics(bool final) {
  const double span = cfg_.metrics_interval_s;
  const auto& t = metrics_.tick_ms;
  const double p50 = pct(t, 0.5), p95 = pct(t, 0.95), p99 = pct(t, 0.99), mx = t.empty() ? 0 : *std::max_element(t.begin(), t.end());
  const double mem = rss_mb();
  log::info("metrics", "người chơi {} | NPC {} | tick p50 {:.2f} p95 {:.2f} p99 {:.2f} max {:.2f} ms | in {:.0f} gói/s {:.1f} KB/s | "
                       "out {:.0f} gói/s {:.1f} KB/s | RAM {:.1f} MB | từ chối {} kick {} sai gói {}",
            online_players(), npcs_->npcs().size(), p50, p95, p99, mx, metrics_.pkts_in / span, metrics_.bytes_in / span / 1024,
            metrics_.pkts_out / span, metrics_.bytes_out / span / 1024, mem, metrics_.rejected, metrics_.kicked, metrics_.malformed);
  if (!cfg_.metrics_file.empty()) {
    std::ofstream f(cfg_.metrics_file, std::ios::app);
    f << std::format(R"({{"t":{:.1f},"players":{},"npcs":{},"tick_p50":{:.3f},"tick_p95":{:.3f},"tick_p99":{:.3f},"tick_max":{:.3f},)"
                     R"("pkts_in_s":{:.1f},"pkts_out_s":{:.1f},"kb_in_s":{:.2f},"kb_out_s":{:.2f},"rss_mb":{:.1f},"rejected":{},"kicked":{},"malformed":{},"final":{}}})",
                     now_, online_players(), npcs_->npcs().size(), p50, p95, p99, mx, metrics_.pkts_in / span,
                     metrics_.pkts_out / span, metrics_.bytes_in / span / 1024, metrics_.bytes_out / span / 1024, mem,
                     metrics_.rejected, metrics_.kicked, metrics_.malformed, final)
      << '\n';
  }
  metrics_.tick_ms.clear();
  metrics_.pkts_in = metrics_.pkts_out = metrics_.bytes_in = metrics_.bytes_out = 0;
}

void GameServer::post_command(std::string line) {
  std::scoped_lock l(cmd_mu_);
  cmds_.push_back(std::move(line));
}

void GameServer::command(const std::string& line) {
  std::istringstream is(line);
  std::string cmd;
  is >> cmd;
  if (cmd == "status" || cmd == "players") {
    log::info("console", "{} người chơi online, {} NPC, tick {}, giờ {:.2f} ngày {}, thời tiết {}", online_players(),
              npcs_->npcs().size(), tick_, wsim_->clock().hour(), wsim_->clock().day() + 1, sim::to_string(wsim_->weather()));
    if (cmd == "players")
      for (const auto& [id, p] : players_)
        log::info("console", "#{} user {} char {} {} pos ({:.1f},{:.1f}) hp {:.0f}", id, p.user_id, p.char_id,
                  p.online ? "online" : "offline", p.motor.pos.x, p.motor.pos.z, p.sv.hp);
  } else if (cmd == "kick") {
    uint32_t id = 0;
    is >> id;
    if (Player* p = player_by_id(id); p && p->online) kick(p->peer, "bị quản trị viên kick");
  } else if (cmd == "tp") {  // quản trị: dịch chuyển người chơi (hỗ trợ/kiểm thử)
    uint32_t id = 0;
    float x = 0, z = 0;
    is >> id >> x >> z;
    Player* p = player_by_id(id);
    if (!p || !is || x < 0 || z < 0 || x > world::to_m(world_.size_x) || z > world::to_m(world_.size_z)) {
      log::warn("console", "tp: cần 'tp <id> <x> <z>' trong world");
    } else {
      glm::vec2 sp{x, z};
      col_->resolve(sp, 0.5f, world_.height_at(sp.x, sp.y), 0.3f);
      p->motor.pos = {sp.x, world_.height_at(sp.x, sp.y), sp.y};
      p->motor.vel = {0, 0};
      log::info("console", "tp #{} -> ({:.1f}, {:.1f})", id, sp.x, sp.y);
    }
  } else if (cmd == "chatstats") {
    log::info("console", "chat gần đã phát: {}", chat_count_);
  } else if (!cmd.empty()) {
    log::info("console", "lệnh: status | players | kick <id> | tp <id> <x> <z> | chatstats | quit");
  }
}

int GameServer::run(std::atomic<bool>& stop) {
  const auto t0 = clock_t_::now();
  const double dt = 1.0 / 30.0;
  double next_tick = 0;
  next_metrics_ = cfg_.metrics_interval_s;
  while (!stop.load() && (cfg_.max_ticks < 0 || static_cast<long long>(tick_) < cfg_.max_ticks)) {
    now_ = std::chrono::duration<double>(clock_t_::now() - t0).count();
    // Mạng: xử lý tới khi tới giờ tick kế tiếp
    const int wait_ms = std::max(0, static_cast<int>((next_tick - now_) * 1000.0));
    ENetEvent ev;
    if (enet_host_service(host_, &ev, static_cast<enet_uint32>(std::min(wait_ms, 5))) > 0) {
      do {
        switch (ev.type) {
          case ENET_EVENT_TYPE_CONNECT:
            enet_peer_timeout(ev.peer, 0, 5000, 10000);  // timeout 10 s
            conns_[ev.peer] = Conn{ev.peer, now_};
            break;
          case ENET_EVENT_TYPE_RECEIVE:
            on_receive(ev.peer, ev.packet->data, ev.packet->dataLength);
            enet_packet_destroy(ev.packet);
            break;
          case ENET_EVENT_TYPE_DISCONNECT: {
            if (Conn* c = conn(ev.peer); c && c->player)
              if (Player* p = player_by_id(c->player); p && p->peer == ev.peer) {
                p->online = false;
                p->offline_since = now_;
                p->peer = nullptr;
                log::info("server", "người chơi #{} rớt mạng (giữ chỗ {:.0f} s)", p->id, cfg_.linger_s);
              }
            conns_.erase(ev.peer);
            break;
          }
          default: break;
        }
      } while (enet_host_check_events(host_, &ev) > 0);
    }
    now_ = std::chrono::duration<double>(clock_t_::now() - t0).count();
    // Kết nối không gửi Hello kịp
    for (auto& [peer, c] : conns_)
      if (c.player == 0 && now_ - c.connected_at > cfg_.hello_timeout_s && peer->state == ENET_PEER_STATE_CONNECTED) {
        c.connected_at = 1e18;
        kick(peer, "không gửi Hello đúng hạn");
      }
    if (now_ >= next_tick) {
      const auto ts = clock_t_::now();
      tick(dt);
      metrics_.tick_ms.push_back(std::chrono::duration<double, std::milli>(clock_t_::now() - ts).count());
      next_tick += dt;
      if (now_ - next_tick > 1.0) next_tick = now_;  // tụt quá xa (debugger) -> không đuổi bắt vô hạn
    }
    {
      std::deque<std::string> pending;
      {
        std::scoped_lock l(cmd_mu_);
        pending.swap(cmds_);
      }
      for (const auto& c : pending) command(c);
    }
    if (backend_.enabled()) {
      backend_pump();
      if (now_ >= next_heartbeat_) {
        send_heartbeat(false);
        next_heartbeat_ = now_ + cfg_.heartbeat_interval_s;
      }
    }
    netsim_.pump(now_);
    enet_host_flush(host_);
    if (now_ >= next_metrics_) {
      log_metrics(false);
      next_metrics_ += cfg_.metrics_interval_s;
    }
  }
  log_metrics(true);
  if (backend_.enabled()) shutdown_persist();
  log::info("server", "dừng sau {} tick", tick_);
  return 0;
}

// ============================================================================ Backend (Phase 11)
std::string GameServer::state_json(const Player& p) const {
  nlohmann::json inv = nlohmann::json::parse(p.inv.to_json())["slots"];
  return nlohmann::json{{"v", 1},
                        {"pos", {{"x", p.motor.pos.x}, {"y", p.motor.pos.y}, {"z", p.motor.pos.z}}},
                        {"yaw", p.motor.yaw},
                        {"vitals",
                         {{"hp", std::clamp(p.sv.hp, 0.0f, 100.0f)},
                          {"hunger", std::clamp(p.sv.hunger, 0.0f, 100.0f)},
                          {"thirst", std::clamp(p.sv.thirst, 0.0f, 100.0f)},
                          {"stamina", std::clamp(p.sv.stamina, 0.0f, 100.0f)},
                          {"rest", std::clamp(p.sv.rest, 0.0f, 100.0f)}}},
                        {"inventory", inv}}
      .dump();
}

void GameServer::request_save(Player& p, const char* reason) {
  if (!backend_.enabled() || !p.loaded || p.save_in_flight || p.save_conflict) return;
  // Khóa idempotency gắn với (server, nhân vật, version gốc, lượt lưu): gửi lại do mất mạng không ghi lặp
  const std::string key = std::format("{}:{}:{}:{}", cfg_.server_id, p.char_id, p.state_version, ++p.save_seq);
  const nlohmann::json body{{"expectedVersion", p.state_version},
                            {"idempotencyKey", key},
                            {"serverId", cfg_.server_id},
                            {"state", nlohmann::json::parse(state_json(p))}};
  p.save_in_flight = true;
  p.next_save = now_ + cfg_.autosave_interval_s;
  backend_.submit({BackendJob::Save, p.id, p.char_id, "PUT", std::format("/internal/characters/{}/state", p.char_id), body.dump()});
  log::debug("backend", "lưu nhân vật {} ({}) v{}", p.char_id, reason, p.state_version);
}

void GameServer::end_session(const std::string& jti, const char* reason) {
  if (!backend_.enabled() || jti.empty()) return;
  backend_.submit({BackendJob::EndSession, 0, 0, "POST", "/internal/sessions/" + jti + "/end", nlohmann::json{{"reason", reason}}.dump()});
}

void GameServer::send_heartbeat(bool draining) {
  const nlohmann::json body{{"id", cfg_.server_id},
                            {"region", cfg_.region},
                            {"address", cfg_.public_address},
                            {"port", cfg_.port},
                            {"capacity", cfg_.max_players},
                            {"currentPlayers", online_players()},
                            {"buildVersion", kVersionString},
                            {"status", draining ? "DRAINING" : "ONLINE"}};
  backend_.submit({BackendJob::Heartbeat, 0, 0, "POST", "/internal/servers/heartbeat", body.dump()});
}

void GameServer::finish_load(Player& p, const BackendResult& r) {
  auto fail = [&](const std::string& why) {
    log::error("backend", "không nạp được nhân vật {}: {}", p.char_id, why);
    if (p.online && p.peer) kick(p.peer, "không nạp được dữ liệu nhân vật, thử lại sau");
    p.online = false;
    p.offline_since = -1e18;  // gỡ ở tick kế (loaded=false -> không lưu đè dữ liệu thật)
    p.peer = nullptr;
  };
  if (r.status != 200) return fail(r.status == 0 ? r.error : std::format("HTTP {}", r.status));
  const auto j = nlohmann::json::parse(r.body, nullptr, false);
  if (j.is_discarded() || !j.contains("stateVersion") || !j["stateVersion"].is_number_integer()) return fail("phản hồi sai định dạng");
  p.state_version = j["stateVersion"].get<int>();
  const nlohmann::json st = j.contains("state") && j["state"].is_object() ? j["state"] : nlohmann::json::object();
  // Không tin mù dữ liệu đã lưu: kẹp chỉ số, kiểm tra vị trí trong world, inventory qua validate.
  if (st.contains("pos") && st["pos"].is_object()) {
    const float x = st["pos"].value("x", -1.0f), z = st["pos"].value("z", -1.0f);
    if (x >= 0 && z >= 0 && x <= world::to_m(world_.size_x) && z <= world::to_m(world_.size_z)) {
      glm::vec2 sp{x, z};
      col_->resolve(sp, 0.5f, world_.height_at(sp.x, sp.y), 0.3f);
      p.motor.pos = {sp.x, world_.height_at(sp.x, sp.y), sp.y};
      p.motor.yaw = st.value("yaw", 0.0f);
    } else {
      log::warn("backend", "nhân vật {}: vị trí đã lưu ngoài world -> về điểm spawn", p.char_id);
    }
  }
  if (st.contains("vitals") && st["vitals"].is_object()) {
    const auto& v = st["vitals"];
    auto get = [&](const char* k, float def) { return std::clamp(v.value(k, def), 0.0f, 100.0f); };
    p.sv.hp = std::max(1.0f, get("hp", 100));  // vào game lại luôn còn sống
    p.sv.hunger = get("hunger", 100);
    p.sv.thirst = get("thirst", 100);
    p.sv.stamina = get("stamina", 100);
    p.sv.rest = get("rest", 100);
  }
  if (st.contains("inventory") && st["inventory"].is_array()) {
    nlohmann::json slots = st["inventory"];
    while (slots.size() < static_cast<size_t>(gc_.inventory_slots)) slots.push_back(nullptr);
    std::string err;
    if (auto inv = sim::Inventory::from_json(gc_, nlohmann::json{{"version", 1}, {"slots", slots}}.dump(), err))
      p.inv = std::move(*inv);
    else
      log::warn("backend", "nhân vật {}: inventory đã lưu không hợp lệ ({}) -> túi rỗng", p.char_id, err);
  }
  apply_social(p, r.body);
  p.loaded = true;
  p.next_save = now_ + cfg_.autosave_interval_s;
  log::info("backend", "nạp nhân vật {} v{} ({})", p.char_id, p.state_version, st.empty() ? "mới" : "đã lưu");
  if (p.online && p.peer) welcome(p);
}

void GameServer::backend_pump() {
  auto describe = [](const BackendResult& r) { return r.status ? std::format("HTTP {}", r.status) : r.error; };
  for (auto& r : backend_.poll()) {
    switch (r.kind) {
      case BackendJob::Heartbeat:
        if (r.status != 200) log::warn("backend", "heartbeat lỗi: {}", describe(r));
        break;
      case BackendJob::Social:
        if (Player* p = player_by_id(r.player); p && p->char_id == r.char_id && r.status == 200) apply_social(*p, r.body);
        break;
      case BackendJob::EndSession:
        if (r.status != 200) log::warn("backend", "kết thúc phiên lỗi: {}", describe(r));
        break;
      case BackendJob::Load:
        if (Player* p = player_by_id(r.player); p && p->char_id == r.char_id && !p->loaded) finish_load(*p, r);
        break;
      case BackendJob::Save: {
        Player* p = player_by_id(r.player);
        if (!p || p->char_id != r.char_id) {
          if (r.status != 200) log::error("backend", "lưu nhân vật {} THẤT BẠI sau khi gỡ: {}", r.char_id, describe(r));
          break;
        }
        p->save_in_flight = false;
        if (r.status == 200) {
          const auto j = nlohmann::json::parse(r.body, nullptr, false);
          if (!j.is_discarded() && j.contains("stateVersion") && j["stateVersion"].is_number_integer())
            p->state_version = j["stateVersion"].get<int>();
        } else if (r.status == 409) {
          // Server khác đã ghi -> dừng lưu để không đè dữ liệu; buộc người chơi vào lại để nạp bản mới.
          p->save_conflict = true;
          log::error("backend", "nhân vật {}: xung đột stateVersion — dừng lưu, kick để nạp lại", p->char_id);
          if (p->online && p->peer) kick(p->peer, "dữ liệu nhân vật đã thay đổi ở nơi khác");
        } else {
          log::warn("backend", "lưu nhân vật {} lỗi ({} {}) — thử lại sau", p->char_id, describe(r), r.body.substr(0, 200));
          p->next_save = now_ + 5;
          p->final_save_sent = false;  // nếu là lượt cuối thì gửi lại
        }
        break;
      }
    }
  }
  for (auto& [id, p] : players_)
    if (p.online && p.loaded && now_ >= p.next_save) {
      request_save(p, "autosave");
      // Làm mới danh sách chặn + trạng thái cấm chat cùng nhịp autosave
      backend_.submit({BackendJob::Social, p.id, p.char_id, "GET", std::format("/internal/users/{}/blocks", p.user_id), ""});
    }
}

void GameServer::apply_social(Player& p, const std::string& body) {
  const auto j = nlohmann::json::parse(body, nullptr, false);
  if (j.is_discarded()) return;
  p.chat_muted_until = j.contains("chatMutedUntil") && j["chatMutedUntil"].is_number_integer() ? j["chatMutedUntil"].get<int64_t>() : 0;
  if (j.contains("blockedUserIds") && j["blockedUserIds"].is_array()) {
    p.blocked.clear();
    for (const auto& v : j["blockedUserIds"])
      if (v.is_string()) p.blocked.insert(std::strtoull(v.get<std::string>().c_str(), nullptr, 10));
  }
}

void GameServer::shutdown_persist() {
  log::info("backend", "đang lưu {} nhân vật trước khi tắt...", players_.size());
  send_heartbeat(true);
  const auto deadline = clock_t_::now() + std::chrono::seconds(15);
  for (;;) {
    bool busy = false;
    backend_pump();
    for (auto& [id, p] : players_) {
      if (!p.loaded || p.save_conflict) continue;
      if (p.save_in_flight) {
        busy = true;
      } else if (!p.final_save_sent) {
        request_save(p, "shutdown");
        p.final_save_sent = true;
        busy = true;
      }
    }
    if (!busy || clock_t_::now() > deadline) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  for (auto& [id, p] : players_) end_session(p.jti, "server_shutdown");
  backend_.stop();
  backend_pump();
  size_t unsaved = 0;
  for (auto& [id, p] : players_) unsaved += p.loaded && !p.save_conflict && (p.save_in_flight || !p.final_save_sent);
  if (unsaved) log::error("backend", "{} nhân vật CHƯA lưu được khi tắt", unsaved);
  else log::info("backend", "đã lưu xong");
}

// ============================================================================ Chat gần (Phase 12)
namespace {
// So khớp không phân biệt hoa/thường cho ASCII; ranh giới từ = không phải chữ/số ASCII và không phải byte UTF-8.
bool word_char(unsigned char c) { return std::isalnum(c) || c >= 0x80; }
std::string mask_words(std::string text, const std::vector<std::string>& words) {
  std::string low = text;
  for (auto& ch : low) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  for (const auto& w0 : words) {
    std::string w = w0;
    for (auto& ch : w) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    for (size_t pos = low.find(w); pos != std::string::npos; pos = low.find(w, pos + 1)) {
      const bool left = pos == 0 || !word_char(static_cast<unsigned char>(low[pos - 1]));
      const bool right = pos + w.size() >= low.size() || !word_char(static_cast<unsigned char>(low[pos + w.size()]));
      if (left && right) {
        for (size_t k = pos; k < pos + w.size(); ++k) {
          // giữ đúng ranh giới UTF-8: chỉ đặt '*' ở byte đầu mỗi ký tự, các byte tiếp theo bỏ
          text[k] = (static_cast<unsigned char>(text[k]) >> 6) == 2 ? '\x01' : '*';
        }
      }
    }
  }
  std::erase(text, '\x01');
  return text;
}
}  // namespace

void GameServer::handle_chat(Player& p, Conn& c, const net::ChatSay& m) {
  const std::string text = net::sanitize_chat(m.text);
  if (text.empty()) {
    violation(c, 0.5f, "chat rỗng/UTF-8 sai");
    return;
  }
  if (p.chat_muted_until > unix_now()) {
    send(p.peer, net::PacketType::Event, net::write(net::EventMsg{30, "chat: ban dang bi cam chat"}), true);
    return;
  }
  if (!p.chat.take(now_)) {
    send(p.peer, net::PacketType::Event, net::write(net::EventMsg{31, "chat: gui qua nhanh"}), true);
    violation(c, 0.1f, "chat spam");
    return;
  }
  const net::ChatMsg out{p.id, p.user_id, mask_words(text, banned_words_)};
  const auto bytes = net::write(out);
  // Chỉ người chơi trong bán kính nghe thấy (kể cả người gửi để xác nhận). Không log nội dung chat.
  for (auto& [id, o] : players_)
    if (o.online && o.loaded && o.peer && glm::distance(xz(o.motor.pos), xz(p.motor.pos)) <= cfg_.local_chat_radius_m &&
        !o.blocked.contains(p.user_id) && !p.blocked.contains(o.user_id))  // chặn hai chiều: không thấy tin của nhau
      send(o.peer, net::PacketType::ChatMsg, bytes, true);
  chat_count_++;
}

}  // namespace pb::server
