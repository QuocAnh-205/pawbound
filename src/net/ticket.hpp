#pragma once
// Session ticket (ADR-004): Backend ký Ed25519, game server chỉ cần PUBLIC KEY để xác minh (không gọi DB).
// Định dạng: base64url(payload JSON) "." base64url(chữ ký 64 byte của chuỗi payload base64url).
// Payload: {"v":1,"sub":user_id,"chr":character_id,"srv":server_id,"exp":unix_s,"jti":"id ngẫu nhiên"}
// Crypto: libsodium (crypto_sign_*), KHÔNG tự cài đặt thuật toán.
#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <unordered_set>

namespace pb::net {

struct TicketClaims {
  uint64_t user_id = 0;
  uint64_t character_id = 0;
  std::string server_id;
  int64_t expires_at = 0;  // unix giây
  std::string jti;
};

using PublicKey = std::array<uint8_t, 32>;
using SecretKey = std::array<uint8_t, 64>;

bool crypto_init();  // sodium_init; gọi một lần lúc khởi động
void generate_keypair(PublicKey& pk, SecretKey& sk);
std::string to_base64(const uint8_t* data, size_t n);
bool from_base64(const std::string& s, uint8_t* out, size_t n);
std::string random_jti();

// Chỉ dùng cho tool dev/test và Backend (Phase 11 dùng Node crypto tương thích).
std::string sign_ticket(const TicketClaims& c, const SecretKey& sk);

enum class TicketError { Ok, Malformed, BadSignature, Expired, WrongServer, Replayed };
const char* to_string(TicketError e);

// Chống dùng lại: ghi nhớ jti đã dùng tới khi ticket hết hạn.
class ReplayCache {
 public:
  bool seen_or_add(const std::string& jti, int64_t expires_at, int64_t now);
  [[nodiscard]] size_t size() const { return set_.size(); }

 private:
  std::unordered_set<std::string> set_;
  std::deque<std::pair<int64_t, std::string>> order_;
};

TicketError verify_ticket(const std::string& ticket, const PublicKey& pk, const std::string& server_id, int64_t now,
                          ReplayCache* replay, TicketClaims& out);

}  // namespace pb::net
