#pragma once
// Dedicated server PAWBOUND (authoritative). Headless: không link SDL video/Vulkan.
// Toàn bộ kết quả gameplay do server tính bằng pb_sim; client chỉ gửi ý định (InputCmd).
#include <atomic>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "net/netsim.hpp"
#include "net/protocol.hpp"
#include "net/ticket.hpp"
#include "server/backend_link.hpp"
#include "sim/advanced.hpp"
#include "sim/collision.hpp"
#include "sim/gameplay.hpp"
#include "sim/nav.hpp"
#include "sim/npc.hpp"
#include "sim/world_sim.hpp"
#include "world/world_types.hpp"

struct _ENetHost;
struct _ENetPeer;

namespace pb::server {

struct ServerConfig {
  uint16_t port = net::kDefaultPort;
  int max_players = 32;
  std::string server_id = "dev-1";
  std::filesystem::path world_dir = "out/world_demo";
  std::filesystem::path data_root = ".";
  std::filesystem::path ticket_pubkey = "config/dev_keys/ticket_pk.b64";
  std::filesystem::path advanced_config;  // rỗng = <data_root>/data/gameplay/advanced.json
  std::filesystem::path metrics_file;  // JSON lines (cho load test); rỗng = tắt
  int snapshot_hz = 20;
  double hello_timeout_s = 5;
  double linger_s = 60;  // giữ nhân vật khi rớt mạng để reconnect
  float aoi_radius_m = 60;
  int max_violations = 10;
  net::NetSimSettings netsim;  // mô phỏng mạng xấu cho gói gửi đi
  long long max_ticks = -1;
  double metrics_interval_s = 10;
  // Backend (Phase 11): rỗng = chế độ DEV (không lưu trạng thái). Token lấy từ biến môi trường PAWBOUND_SERVICE_TOKEN.
  std::string backend_url;
  std::string service_token;
  std::string region = "vn";
  std::string public_address = "127.0.0.1";
  double heartbeat_interval_s = 10;
  double autosave_interval_s = 60;
  float local_chat_radius_m = 30;
  // Phase 14 — lãnh thổ (MVP): bang có >= N thành viên online trong một khu, không có bang khác, đủ T giây -> xin chiếm
  int territory_min_members = 2;
  double territory_hold_s = 60;
  double territory_eval_interval_s = 5;
};

struct Metrics {
  std::vector<double> tick_ms;
  uint64_t pkts_in = 0, pkts_out = 0, bytes_in = 0, bytes_out = 0, rejected = 0, kicked = 0, malformed = 0;
};

class GameServer {
 public:
  explicit GameServer(ServerConfig cfg);
  ~GameServer();
  bool init(std::string& error);
  // Chạy tới khi stop = true hoặc đủ max_ticks. Trả exit code.
  int run(std::atomic<bool>& stop);
  // Thread-safe: thread console chỉ đẩy lệnh vào hàng đợi; vòng lặp chính xử lý (không truy cập trạng thái chéo thread).
  void post_command(std::string line);
  [[nodiscard]] size_t online_players() const;

 private:
  struct Player {
    uint32_t id = 0;
    uint64_t user_id = 0, char_id = 0;
    _ENetPeer* peer = nullptr;
    bool online = false;
    double offline_since = 0;
    sim::MotorState motor;
    sim::MovementStats stats;
    sim::SurvivalState sv;
    sim::CombatState cb;
    sim::Inventory inv{6};
    std::deque<net::InputCmd> inputs;
    net::InputCmd last_cmd;
    uint32_t last_seq = 0;
    bool have_seq = false;
    uint16_t prev_buttons = 0;
    int starved_ticks = 0;
    bool sprinting = false, resting = false;
    uint32_t out_seq = 0;
    double next_snapshot = 0;
    uint64_t snap_count = 0;
    // Lưu trữ (Backend)
    std::string jti;
    bool loaded = true;           // false: đang chờ Backend trả trạng thái -> chưa mô phỏng, chưa Welcome
    int state_version = 0;
    bool save_in_flight = false, save_conflict = false, final_save_sent = false;
    uint32_t save_seq = 0;
    double next_save = 0;
    // Chat gần
    net::TokenBucket chat{0.5, 3};  // 3 tin liền, sau đó 1 tin / 2 s
    int64_t chat_muted_until = 0;   // unix giây (từ Backend)
    std::unordered_set<uint64_t> blocked;  // user id số chặn/bị chặn (từ Backend, làm mới định kỳ)
    // Phase 14
    double joined_at = 0;                    // giờ server lúc vào phiên (bảo vệ người mới khỏi kẻ trộm)
    sim::CaptureStatus cap;
    sim::SkillSet skills;
    sim::QuestBook quests;
    bool has_companion = false;
    sim::Companion companion;
    glm::vec2 last_xp_pos{0.0f};
    bool interact_this_tick = false;
    std::unordered_set<uint64_t> teammates;  // user id số cùng party/bang (từ Backend)
    std::string guild_id;
  };
  struct Conn {
    _ENetPeer* peer = nullptr;
    double connected_at = 0;
    uint32_t player = 0;  // 0 = chưa xác thực
    float violations = 0;
    net::TokenBucket pkts{150, 90}, bytes{64000, 32000};
  };

  void on_receive(_ENetPeer* peer, const uint8_t* data, size_t len);
  void handle_hello(Conn& c, const net::Hello& h);
  void violation(Conn& c, float amount, const char* why);
  void kick(_ENetPeer* peer, const std::string& reason);
  void send(_ENetPeer* peer, net::PacketType t, const std::vector<uint8_t>& payload, bool reliable, uint32_t seq = 0);
  void tick(double dt);
  void tick_player(Player& p, double dt);
  void send_snapshots();
  void log_metrics(bool final);
  void command(const std::string& line);
  // Backend
  void backend_pump();
  void request_save(Player& p, const char* reason);
  void end_session(const std::string& jti, const char* reason);
  void send_heartbeat(bool draining);
  void finish_load(Player& p, const BackendResult& r);
  void welcome(Player& p);
  [[nodiscard]] std::string state_json(const Player& p) const;
  void shutdown_persist();
  void handle_chat(Player& p, Conn& c, const net::ChatSay& m);
  void apply_social(Player& p, const std::string& body);
  // Phase 14 (advanced_server.cpp)
  void init_advanced();
  void tick_advanced(double dt);
  void on_capture_event(const sim::CaptureEvent& e);
  void quest_event(Player& p, sim::QuestObjective obj, float amount);
  void grant_bones(Player& p, int amount, const std::string& reason, const std::string& ref_key);
  void try_recruit_companion(Player& p);
  bool attack_thief(Player& p, float range);
  void tick_territory();
  void notify_teammates(const Player& victim, const std::string& text);
  void event(Player& p, uint16_t code, const std::string& text);
  Player* player_by_id(uint32_t id);
  Conn* conn(_ENetPeer* peer);

  ServerConfig cfg_;
  _ENetHost* host_ = nullptr;
  world::WorldData world_;
  std::string world_hash_;
  sim::GameplayConfig gc_;
  sim::NpcConfig nc_;
  sim::WorldSimConfig wc_;
  std::unique_ptr<sim::CollisionWorld> col_;
  std::unique_ptr<sim::NavGraph> nav_;
  std::unique_ptr<sim::NpcManager> npcs_;
  std::unique_ptr<sim::WorldSim> wsim_;
  std::vector<sim::ResourceNode> resources_;
  std::vector<sim::Noise> noises_;
  sim::MovementStats dog_stats_;
  net::PublicKey ticket_pk_{};
  net::ReplayCache replay_;
  std::unordered_map<uint32_t, Player> players_;
  std::unordered_map<_ENetPeer*, Conn> conns_;
  uint32_t next_player_ = 1;
  uint64_t tick_ = 0;
  double now_ = 0, sim_time_ = 0;
  Metrics metrics_;
  double next_metrics_ = 0;
  net::NetSim netsim_;
  glm::vec2 spawn_{0.0f};
  std::mutex cmd_mu_;
  std::deque<std::string> cmds_;
  BackendLink backend_;
  sim::AdvancedConfig adv_;
  std::unique_ptr<sim::CaptureSystem> capture_;
  double next_territory_eval_ = 0;
  std::unordered_map<std::string, double> territory_hold_;  // "zone:guild" -> giây giữ liên tục
  std::unordered_map<int, double> territory_cooldown_;      // zone -> giờ server được thử lại
  std::vector<std::string> banned_words_;  // data/social/social.json (cùng danh sách với Backend)
  uint64_t chat_count_ = 0;
  double next_heartbeat_ = 0;
};

}  // namespace pb::server
