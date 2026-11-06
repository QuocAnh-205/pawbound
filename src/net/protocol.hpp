#pragma once
// Giao thức PAWBOUND trên ENet (ADR-004). Mọi gói: header 16 byte + payload ≤ kMaxPayload.
// Đọc luôn kiểm tra biên; gói sai bị từ chối (decode trả false), KHÔNG crash.
// Client chỉ gửi Ý ĐỊNH (Input/Action) — không có gói nào cho client đặt vị trí, HP, item.
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace pb::net {

inline constexpr uint16_t kProtocolVersion = 1;
inline constexpr size_t kHeaderSize = 16;
inline constexpr size_t kMaxPayload = 1200;
inline constexpr uint16_t kDefaultPort = 27015;
inline constexpr int kMaxSnapshotEntities = 50;

enum class PacketType : uint16_t {
  Hello = 1, Welcome = 2, Reject = 3, Input = 4, Snapshot = 5, Event = 6, Ping = 7, Pong = 8, Bye = 9,
};

struct Header {
  uint16_t version = kProtocolVersion;
  PacketType type = PacketType::Ping;
  uint32_t sequence = 0;
  uint32_t ack = 0;
  uint32_t server_tick = 0;
};

// ---------------------------------------------------------------- bộ đọc/ghi little-endian
class Writer {
 public:
  void u8(uint8_t v) { b_.push_back(v); }
  void u16(uint16_t v) { put(v, 2); }
  void u32(uint32_t v) { put(v, 4); }
  void i16(int16_t v) { put(static_cast<uint16_t>(v), 2); }
  void i32(int32_t v) { put(static_cast<uint32_t>(v), 4); }
  void str(std::string_view s);  // u16 độ dài + byte
  [[nodiscard]] const std::vector<uint8_t>& bytes() const { return b_; }
  [[nodiscard]] size_t size() const { return b_.size(); }

 private:
  void put(uint64_t v, int n) {
    for (int i = 0; i < n; ++i) b_.push_back(static_cast<uint8_t>(v >> (8 * i)));
  }
  std::vector<uint8_t> b_;
};

class Reader {
 public:
  explicit Reader(std::span<const uint8_t> b) : b_(b) {}
  bool u8(uint8_t& v);
  bool u16(uint16_t& v);
  bool u32(uint32_t& v);
  bool i16(int16_t& v);
  bool i32(int32_t& v);
  bool str(std::string& s, size_t max_len);
  [[nodiscard]] bool done() const { return pos_ == b_.size(); }
  [[nodiscard]] size_t remaining() const { return b_.size() - pos_; }

 private:
  bool need(size_t n) const { return pos_ + n <= b_.size(); }
  std::span<const uint8_t> b_;
  size_t pos_ = 0;
};

// ---------------------------------------------------------------- thông điệp
struct Hello {
  uint16_t protocol_version = kProtocolVersion;
  std::string ticket;       // session ticket do Backend ký (Phase 11) hoặc ticket dev
  std::string world_hash;   // logical hash của world client đang có
  std::string client_build;
};
struct Welcome {
  uint32_t player_id = 0;
  uint32_t entity_id = 0;
  uint32_t server_tick = 0;
  uint16_t tick_rate = 30;
};
struct Reject {
  std::string reason;
};
// Nút (bitmask) trong Input
enum Button : uint16_t { kSprint = 1, kWalk = 2, kJump = 4, kInteract = 8, kAttack = 16, kDodge = 32, kSniff = 64, kForage = 128, kRest = 256 };
struct InputCmd {
  uint32_t seq = 0;
  int8_t move_x = 0, move_z = 0;  // hướng thế giới, -127..127 (server kẹp độ dài ≤ 1)
  uint16_t buttons = 0;
  uint16_t yaw = 0;  // 0..65535 = 0..2π (chỉ dùng để chọn hướng cắn khi đứng yên)
};
struct InputPacket {
  std::vector<InputCmd> cmds;  // 1..3 lệnh gần nhất (dư thừa chống mất gói)
};
enum EntityKind : uint8_t { kPlayer = 0, kNpcHuman = 1, kNpcDog = 2 };
struct EntityState {
  uint32_t id = 0;
  uint8_t kind = kPlayer;
  uint8_t flags = 0;  // bit0 sống, bit1 nước rút, 2-4 tính cách NPC, bit5 = người chơi này
  int32_t x_cm = 0, z_cm = 0;
  int16_t y_cm = 0;
  uint16_t yaw = 0;
  uint8_t gait = 0;
  uint8_t hp = 100;
};
struct SelfState {
  uint32_t last_input_seq = 0;
  uint8_t hp = 100, hunger = 100, thirst = 100, stamina = 100, rest = 100;
  uint8_t life = 0;
  uint16_t hour_x100 = 0;  // giờ thế giới × 100
  uint8_t weather = 0;
  uint32_t day = 0;
};
struct Snapshot {
  SelfState self;
  std::vector<EntityState> entities;
};
struct EventMsg {
  uint16_t code = 0;
  std::string text;
};

std::vector<uint8_t> encode(const Header& h, const std::vector<uint8_t>& payload);
// Tách header + payload; false nếu gói ngắn/quá cỡ/sai version.
bool decode_header(std::span<const uint8_t> pkt, Header& h, std::span<const uint8_t>& payload);

std::vector<uint8_t> write(const Hello& m);
std::vector<uint8_t> write(const Welcome& m);
std::vector<uint8_t> write(const Reject& m);
std::vector<uint8_t> write(const InputPacket& m);
std::vector<uint8_t> write(const Snapshot& m);
std::vector<uint8_t> write(const EventMsg& m);
bool read(std::span<const uint8_t> p, Hello& m);
bool read(std::span<const uint8_t> p, Welcome& m);
bool read(std::span<const uint8_t> p, Reject& m);
bool read(std::span<const uint8_t> p, InputPacket& m);
bool read(std::span<const uint8_t> p, Snapshot& m);
bool read(std::span<const uint8_t> p, EventMsg& m);

// Sequence 32-bit có quấn vòng: a "mới hơn" b.
constexpr bool seq_newer(uint32_t a, uint32_t b) { return static_cast<int32_t>(a - b) > 0; }

// ---------------------------------------------------------------- rate limit
class TokenBucket {
 public:
  TokenBucket(double rate_per_s = 120, double burst = 60) : rate_(rate_per_s), burst_(burst), tokens_(burst) {}
  bool take(double now_s, double cost = 1.0);

 private:
  double rate_, burst_, tokens_;
  double last_ = -1;
};

}  // namespace pb::net
