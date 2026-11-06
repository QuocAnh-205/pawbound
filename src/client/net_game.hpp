#pragma once
// Chế độ online (Phase 10): client chỉ gửi Ý ĐỊNH; mọi trạng thái hiển thị đến từ snapshot của server.
// Người chơi khác + NPC: nội suy với độ trễ 100 ms. Bản thân: snapshot mới nhất (chưa có client prediction).
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "client/dog_actor.hpp"
#include "client/model_library.hpp"
#include "client/npc_view.hpp"
#include "net/protocol.hpp"
#include "render/overlay.hpp"

struct _ENetHost;
struct _ENetPeer;

namespace pb::client {

class NetGame {
 public:
  NetGame(render::Renderer& r, const sim::DogCatalog& cat, const std::filesystem::path& asset_root);
  ~NetGame();
  bool connect(const std::string& host, uint16_t port, const std::string& ticket, const std::string& world_hash,
               std::string& error);
  void fixed_update(const InputState& in, const geom::Camera25D& cam, float dt);
  void update_visual(float dt);
  void append_draws(bool debug, std::vector<render::DrawItem>& out);
  void draw_hud(render::Overlay& o, int w, int h) const;
  [[nodiscard]] bool connected() const { return welcomed_ && !lost_; }
  [[nodiscard]] glm::vec3 focus() const { return self_pos_; }
  [[nodiscard]] float hour() const { return static_cast<float>(self_.hour_x100) / 100.0f; }
  [[nodiscard]] uint8_t weather() const { return self_.weather; }
  [[nodiscard]] std::string status() const;

 private:
  struct Timed {
    double t;
    net::Snapshot s;
  };
  void poll();
  void send(net::PacketType t, const std::vector<uint8_t>& payload, bool reliable);
  static bool sample(const std::deque<Timed>& buf, double t, uint32_t id, net::EntityState& out);

  render::Renderer& r_;
  const sim::DogCatalog& cat_;
  std::unique_ptr<ModelLibrary> lib_;
  std::unique_ptr<NpcView> npc_view_;
  std::map<uint32_t, std::unique_ptr<DogActor>> dogs_;  // người chơi (kể cả bản thân)
  std::vector<sim::Npc> npc_frame_;
  _ENetHost* host_ = nullptr;
  _ENetPeer* peer_ = nullptr;
  bool welcomed_ = false, lost_ = false;
  uint32_t player_id_ = 0, seq_ = 0, out_seq_ = 0;
  std::deque<net::InputCmd> recent_;
  std::deque<Timed> snaps_;
  net::SelfState self_{};
  glm::vec3 self_pos_{0.0f};
  double clock_ = 0, last_rx_ = 0;
  std::vector<double> rtt_;
  std::map<uint32_t, double> pings_;
  double next_ping_ = 0;
  std::deque<std::pair<double, std::string>> log_;
  std::string reject_;
};

}  // namespace pb::client
