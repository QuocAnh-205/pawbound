#include "net/protocol.hpp"

#include <algorithm>

namespace pb::net {

void Writer::str(std::string_view s) {
  const auto n = static_cast<uint16_t>(std::min<size_t>(s.size(), 0xFFFF));
  u16(n);
  b_.insert(b_.end(), s.begin(), s.begin() + n);
}

bool Reader::u8(uint8_t& v) {
  if (!need(1)) return false;
  v = b_[pos_++];
  return true;
}
bool Reader::u16(uint16_t& v) {
  if (!need(2)) return false;
  v = static_cast<uint16_t>(b_[pos_] | (b_[pos_ + 1] << 8));
  pos_ += 2;
  return true;
}
bool Reader::u32(uint32_t& v) {
  if (!need(4)) return false;
  v = static_cast<uint32_t>(b_[pos_]) | (static_cast<uint32_t>(b_[pos_ + 1]) << 8) |
      (static_cast<uint32_t>(b_[pos_ + 2]) << 16) | (static_cast<uint32_t>(b_[pos_ + 3]) << 24);
  pos_ += 4;
  return true;
}
bool Reader::i16(int16_t& v) {
  uint16_t u;
  if (!u16(u)) return false;
  v = static_cast<int16_t>(u);
  return true;
}
bool Reader::i32(int32_t& v) {
  uint32_t u;
  if (!u32(u)) return false;
  v = static_cast<int32_t>(u);
  return true;
}
bool Reader::str(std::string& s, size_t max_len) {
  uint16_t n;
  if (!u16(n) || n > max_len || !need(n)) return false;
  s.assign(reinterpret_cast<const char*>(b_.data() + pos_), n);
  pos_ += n;
  return true;
}

std::vector<uint8_t> encode(const Header& h, const std::vector<uint8_t>& payload) {
  Writer w;
  w.u16(h.version);
  w.u16(static_cast<uint16_t>(h.type));
  w.u32(h.sequence);
  w.u32(h.ack);
  w.u32(h.server_tick);
  std::vector<uint8_t> out = w.bytes();
  out.insert(out.end(), payload.begin(), payload.end());
  return out;
}

bool decode_header(std::span<const uint8_t> pkt, Header& h, std::span<const uint8_t>& payload) {
  if (pkt.size() < kHeaderSize || pkt.size() > kHeaderSize + kMaxPayload) return false;
  Reader r(pkt.subspan(0, kHeaderSize));
  uint16_t type = 0;
  if (!r.u16(h.version) || !r.u16(type) || !r.u32(h.sequence) || !r.u32(h.ack) || !r.u32(h.server_tick)) return false;
  if (h.version != kProtocolVersion) return false;
  if (type < 1 || type > static_cast<uint16_t>(PacketType::ChatMsg)) return false;  // cập nhật khi thêm loại gói
  h.type = static_cast<PacketType>(type);
  payload = pkt.subspan(kHeaderSize);
  return true;
}

// ---------------------------------------------------------------- Hello / Welcome / Reject / Event
std::vector<uint8_t> write(const Hello& m) {
  Writer w;
  w.u16(m.protocol_version);
  w.str(m.ticket);
  w.str(m.world_hash);
  w.str(m.client_build);
  return w.bytes();
}
bool read(std::span<const uint8_t> p, Hello& m) {
  Reader r(p);
  return r.u16(m.protocol_version) && r.str(m.ticket, 600) && r.str(m.world_hash, 80) && r.str(m.client_build, 64) && r.done();
}
std::vector<uint8_t> write(const Welcome& m) {
  Writer w;
  w.u32(m.player_id);
  w.u32(m.entity_id);
  w.u32(m.server_tick);
  w.u16(m.tick_rate);
  return w.bytes();
}
bool read(std::span<const uint8_t> p, Welcome& m) {
  Reader r(p);
  return r.u32(m.player_id) && r.u32(m.entity_id) && r.u32(m.server_tick) && r.u16(m.tick_rate) && r.done();
}
std::vector<uint8_t> write(const Reject& m) {
  Writer w;
  w.str(m.reason);
  return w.bytes();
}
bool read(std::span<const uint8_t> p, Reject& m) {
  Reader r(p);
  return r.str(m.reason, 256) && r.done();
}
std::vector<uint8_t> write(const EventMsg& m) {
  Writer w;
  w.u16(m.code);
  w.str(m.text);
  return w.bytes();
}
bool read(std::span<const uint8_t> p, EventMsg& m) {
  Reader r(p);
  return r.u16(m.code) && r.str(m.text, 256) && r.done();
}

// ---------------------------------------------------------------- Chat
std::vector<uint8_t> write(const ChatSay& m) {
  Writer w;
  w.str(m.text);
  return w.bytes();
}
bool read(std::span<const uint8_t> p, ChatSay& m) {
  Reader r(p);
  return r.str(m.text, kMaxChatBytes) && r.done();
}
std::vector<uint8_t> write(const ChatMsg& m) {
  Writer w;
  w.u32(m.from_entity);
  w.u32(static_cast<uint32_t>(m.from_user));
  w.u32(static_cast<uint32_t>(m.from_user >> 32));
  w.str(m.text);
  return w.bytes();
}
bool read(std::span<const uint8_t> p, ChatMsg& m) {
  Reader r(p);
  uint32_t lo = 0, hi = 0;
  if (!(r.u32(m.from_entity) && r.u32(lo) && r.u32(hi) && r.str(m.text, kMaxChatBytes) && r.done())) return false;
  m.from_user = (static_cast<uint64_t>(hi) << 32) | lo;
  return true;
}

std::string sanitize_chat(std::string_view raw) {
  std::string out;
  out.reserve(raw.size());
  bool space = true;  // bỏ khoảng trắng đầu
  for (size_t i = 0; i < raw.size();) {
    const auto c = static_cast<unsigned char>(raw[i]);
    size_t len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
    if (len == 0 || i + len > raw.size()) return {};
    uint32_t cp = len == 1 ? c : c & (0x7F >> len);
    for (size_t k = 1; k < len; ++k) {
      const auto cc = static_cast<unsigned char>(raw[i + k]);
      if ((cc >> 6) != 2) return {};
      cp = (cp << 6) | (cc & 0x3F);
    }
    // Từ chối mã hóa dài thừa / surrogate / ngoài Unicode
    if ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000) || cp > 0x10FFFF ||
        (cp >= 0xD800 && cp <= 0xDFFF))
      return {};
    // Ký tự điều khiển C0/C1 và ký tự định dạng hai chiều/vô hình hay dùng để giả mạo hiển thị
    const bool control = cp < 0x20 || cp == 0x7F || (cp >= 0x80 && cp < 0xA0) || (cp >= 0x200B && cp <= 0x200F) ||
                         (cp >= 0x202A && cp <= 0x202E) || (cp >= 0x2066 && cp <= 0x2069) || cp == 0xFEFF;
    if (control || cp == ' ') {
      if (!space) out += ' ';
      space = true;
    } else {
      out.append(raw.substr(i, len));
      space = false;
    }
    i += len;
  }
  while (!out.empty() && out.back() == ' ') out.pop_back();
  if (out.size() > kMaxChatBytes) return {};
  return out;
}

// ---------------------------------------------------------------- Input
std::vector<uint8_t> write(const InputPacket& m) {
  Writer w;
  w.u8(static_cast<uint8_t>(std::min<size_t>(m.cmds.size(), 3)));
  for (size_t i = 0; i < m.cmds.size() && i < 3; ++i) {
    const auto& c = m.cmds[i];
    w.u32(c.seq);
    w.u8(static_cast<uint8_t>(c.move_x));
    w.u8(static_cast<uint8_t>(c.move_z));
    w.u16(c.buttons);
    w.u16(c.yaw);
  }
  return w.bytes();
}
bool read(std::span<const uint8_t> p, InputPacket& m) {
  Reader r(p);
  uint8_t n;
  if (!r.u8(n) || n == 0 || n > 3) return false;
  m.cmds.resize(n);
  for (auto& c : m.cmds) {
    uint8_t mx, mz;
    if (!r.u32(c.seq) || !r.u8(mx) || !r.u8(mz) || !r.u16(c.buttons) || !r.u16(c.yaw)) return false;
    c.move_x = static_cast<int8_t>(mx);
    c.move_z = static_cast<int8_t>(mz);
    if (c.buttons & ~0x1FFu) return false;  // bit lạ -> gói không hợp lệ
  }
  return r.done();
}

// ---------------------------------------------------------------- Snapshot
std::vector<uint8_t> write(const Snapshot& m) {
  Writer w;
  const auto& s = m.self;
  w.u32(s.last_input_seq);
  for (uint8_t v : {s.hp, s.hunger, s.thirst, s.stamina, s.rest, s.life}) w.u8(v);
  w.u16(s.hour_x100);
  w.u8(s.weather);
  w.u32(s.day);
  const auto n = static_cast<uint8_t>(std::min<size_t>(m.entities.size(), kMaxSnapshotEntities));
  w.u8(n);
  // Vị trí: gốc tuyệt đối (int32 cm) + mỗi entity lệch int16 cm so với gốc (AOI 60 m << ±327 m) -> 16 byte/entity.
  const int32_t ox = n ? m.entities[0].x_cm : 0, oz = n ? m.entities[0].z_cm : 0;
  w.i32(ox);
  w.i32(oz);
  for (size_t i = 0; i < n; ++i) {
    const auto& e = m.entities[i];
    w.u32(e.id);
    w.u8(e.kind);
    w.u8(e.flags);
    w.i16(static_cast<int16_t>(std::clamp(e.x_cm - ox, -32767, 32767)));
    w.i16(static_cast<int16_t>(std::clamp(e.z_cm - oz, -32767, 32767)));
    w.i16(e.y_cm);
    w.u16(e.yaw);
    w.u8(e.gait);
    w.u8(e.hp);
  }
  return w.bytes();
}
bool read(std::span<const uint8_t> p, Snapshot& m) {
  Reader r(p);
  auto& s = m.self;
  if (!r.u32(s.last_input_seq) || !r.u8(s.hp) || !r.u8(s.hunger) || !r.u8(s.thirst) || !r.u8(s.stamina) || !r.u8(s.rest) ||
      !r.u8(s.life) || !r.u16(s.hour_x100) || !r.u8(s.weather) || !r.u32(s.day))
    return false;
  uint8_t n;
  int32_t ox = 0, oz = 0;
  if (!r.u8(n) || n > kMaxSnapshotEntities || !r.i32(ox) || !r.i32(oz)) return false;
  m.entities.resize(n);
  for (auto& e : m.entities) {
    int16_t dx = 0, dz = 0;
    if (!r.u32(e.id) || !r.u8(e.kind) || !r.u8(e.flags) || !r.i16(dx) || !r.i16(dz) || !r.i16(e.y_cm) || !r.u16(e.yaw) ||
        !r.u8(e.gait) || !r.u8(e.hp) || e.kind > kNpcDog)
      return false;
    e.x_cm = ox + dx;
    e.z_cm = oz + dz;
  }
  return r.done();
}

bool TokenBucket::take(double now, double cost) {
  if (last_ < 0) last_ = now;
  tokens_ = std::min(burst_, tokens_ + (now - last_) * rate_);
  last_ = now;
  if (tokens_ < cost) return false;
  tokens_ -= cost;
  return true;
}

}  // namespace pb::net
