// pawbound net keygen | ticket — công cụ DEV. Production: Backend (Phase 11) giữ secret key và ký ticket.
#include <charconv>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>

#include "net/ticket.hpp"
#include "tools/commands.hpp"

namespace pb::tools {
namespace {
using Args = std::vector<std::string_view>;
std::string_view opt(const Args& a, std::string_view name, std::string_view def = {}) {
  for (size_t i = 0; i + 1 < a.size(); ++i)
    if (a[i] == name) return a[i + 1];
  return def;
}
template <class T>
T num(std::string_view s, T def) {
  T v = def;
  std::from_chars(s.data(), s.data() + s.size(), v);
  return v;
}
}  // namespace

int net_command(std::string_view cmd, const std::vector<std::string_view>& a) {
  if (!net::crypto_init()) {
    std::fputs("libsodium init thất bại\n", stderr);
    return 2;
  }
  if (cmd == "keygen") {
    const std::filesystem::path out(std::string(opt(a, "--out", "config/dev_keys")));
    std::filesystem::create_directories(out);
    if (std::filesystem::exists(out / "ticket_sk.b64") && opt(a, "--force") != "yes") {
      std::fprintf(stderr, "%s đã có khóa (dùng --force yes để ghi đè)\n", out.string().c_str());
      return 1;
    }
    net::PublicKey pk;
    net::SecretKey sk;
    net::generate_keypair(pk, sk);
    std::ofstream(out / "ticket_pk.b64") << net::to_base64(pk.data(), pk.size()) << '\n';
    std::ofstream(out / "ticket_sk.b64") << net::to_base64(sk.data(), sk.size()) << '\n';
    std::printf("đã tạo cặp khóa DEV tại %s (ticket_sk.b64 là BÍ MẬT — không commit, không dùng cho production)\n",
                out.string().c_str());
    return 0;
  }
  if (cmd == "ticket") {
    std::ifstream f(std::string(opt(a, "--sk", "config/dev_keys/ticket_sk.b64")));
    std::string b64;
    f >> b64;
    net::SecretKey sk;
    if (!f || !net::from_base64(b64, sk.data(), sk.size())) {
      std::fputs("không đọc được secret key (chạy: pawbound net keygen)\n", stderr);
      return 2;
    }
    net::TicketClaims c;
    c.user_id = num<uint64_t>(opt(a, "--user", "1"), 1);
    c.character_id = num<uint64_t>(opt(a, "--char", "1"), 1);
    c.server_id = std::string(opt(a, "--server", "dev-1"));
    const auto ttl = num<int64_t>(opt(a, "--ttl", "120"), 120);
    c.expires_at = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count() + ttl;
    c.jti = net::random_jti();
    std::printf("%s\n", net::sign_ticket(c, sk).c_str());
    return 0;
  }
  if (cmd == "verify-ticket") {  // kiểm tra tương thích ticket do Backend (Node) ký
    std::ifstream f(std::string(opt(a, "--pk", "config/dev_keys/ticket_pk.b64")));
    std::string b64;
    f >> b64;
    net::PublicKey pk;
    if (!f || !net::from_base64(b64, pk.data(), pk.size())) {
      std::fputs("không đọc được public key\n", stderr);
      return 2;
    }
    const auto now =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    net::TicketClaims c;
    const auto e =
        net::verify_ticket(std::string(opt(a, "--ticket")), pk, std::string(opt(a, "--server", "dev-1")), now, nullptr, c);
    if (e != net::TicketError::Ok) {
      std::printf("INVALID: %s\n", net::to_string(e));
      return 1;
    }
    std::printf("OK user=%llu char=%llu srv=%s exp=%lld\n", static_cast<unsigned long long>(c.user_id),
                static_cast<unsigned long long>(c.character_id), c.server_id.c_str(), static_cast<long long>(c.expires_at));
    return 0;
  }
  std::fputs("net: lệnh không hợp lệ (keygen | ticket | verify-ticket)\n", stderr);
  return 2;
}

}  // namespace pb::tools
