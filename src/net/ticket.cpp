#include "net/ticket.hpp"

#include <algorithm>
#include <cstring>
#include <format>

#include <nlohmann/json.hpp>
#include <sodium.h>

namespace pb::net {

bool crypto_init() { return sodium_init() >= 0; }

void generate_keypair(PublicKey& pk, SecretKey& sk) { crypto_sign_keypair(pk.data(), sk.data()); }

std::string to_base64(const uint8_t* data, size_t n) {
  std::string out(sodium_base64_encoded_len(n, sodium_base64_VARIANT_URLSAFE_NO_PADDING), '\0');
  sodium_bin2base64(out.data(), out.size(), data, n, sodium_base64_VARIANT_URLSAFE_NO_PADDING);
  out.resize(std::strlen(out.c_str()));
  return out;
}

bool from_base64(const std::string& s, uint8_t* out, size_t n) {
  size_t len = 0;
  return sodium_base642bin(out, n, s.data(), s.size(), nullptr, &len, nullptr, sodium_base64_VARIANT_URLSAFE_NO_PADDING) == 0 &&
         len == n;
}

std::string random_jti() {
  uint8_t b[16];
  randombytes_buf(b, sizeof b);
  return to_base64(b, sizeof b);
}

std::string sign_ticket(const TicketClaims& c, const SecretKey& sk) {
  const nlohmann::json j = {{"v", 1}, {"sub", c.user_id}, {"chr", c.character_id}, {"srv", c.server_id}, {"exp", c.expires_at}, {"jti", c.jti}};
  const std::string payload = j.dump();
  const std::string p64 = to_base64(reinterpret_cast<const uint8_t*>(payload.data()), payload.size());
  uint8_t sig[crypto_sign_BYTES];
  crypto_sign_detached(sig, nullptr, reinterpret_cast<const uint8_t*>(p64.data()), p64.size(), sk.data());
  return p64 + "." + to_base64(sig, sizeof sig);
}

const char* to_string(TicketError e) {
  switch (e) {
    case TicketError::Ok: return "ok";
    case TicketError::Malformed: return "ticket sai định dạng";
    case TicketError::BadSignature: return "chữ ký ticket không hợp lệ";
    case TicketError::Expired: return "ticket hết hạn";
    case TicketError::WrongServer: return "ticket không dành cho server này";
    case TicketError::Replayed: return "ticket đã được dùng";
  }
  return "?";
}

bool ReplayCache::seen_or_add(const std::string& jti, int64_t exp, int64_t now) {
  while (!order_.empty() && order_.front().first < now) {  // bỏ jti đã hết hạn (ticket hết hạn thì tự bị từ chối)
    set_.erase(order_.front().second);
    order_.pop_front();
  }
  if (set_.contains(jti)) return true;
  set_.insert(jti);
  order_.emplace_back(exp, jti);
  std::sort(order_.begin(), order_.end());
  return false;
}

TicketError verify_ticket(const std::string& t, const PublicKey& pk, const std::string& server_id, int64_t now,
                          ReplayCache* replay, TicketClaims& out) {
  const auto dot = t.find('.');
  if (dot == std::string::npos || dot == 0 || t.size() > 600) return TicketError::Malformed;
  const std::string p64 = t.substr(0, dot), s64 = t.substr(dot + 1);
  uint8_t sig[crypto_sign_BYTES];
  if (!from_base64(s64, sig, sizeof sig)) return TicketError::Malformed;
  // Xác minh chữ ký TRƯỚC khi tin bất kỳ trường nào trong payload.
  if (crypto_sign_verify_detached(sig, reinterpret_cast<const uint8_t*>(p64.data()), p64.size(), pk.data()) != 0)
    return TicketError::BadSignature;
  std::string payload(p64.size(), '\0');
  size_t len = 0;
  if (sodium_base642bin(reinterpret_cast<uint8_t*>(payload.data()), payload.size(), p64.data(), p64.size(), nullptr, &len,
                        nullptr, sodium_base64_VARIANT_URLSAFE_NO_PADDING) != 0)
    return TicketError::Malformed;
  payload.resize(len);
  const auto j = nlohmann::json::parse(payload, nullptr, false);
  if (j.is_discarded() || !j.is_object() || j.value("v", 0) != 1) return TicketError::Malformed;
  try {
    out.user_id = j.at("sub").get<uint64_t>();
    out.character_id = j.at("chr").get<uint64_t>();
    out.server_id = j.at("srv").get<std::string>();
    out.expires_at = j.at("exp").get<int64_t>();
    out.jti = j.at("jti").get<std::string>();
  } catch (...) {
    return TicketError::Malformed;
  }
  if (out.expires_at < now) return TicketError::Expired;
  if (out.server_id != server_id) return TicketError::WrongServer;
  if (replay && replay->seen_or_add(out.jti, out.expires_at, now)) return TicketError::Replayed;
  return TicketError::Ok;
}

}  // namespace pb::net
