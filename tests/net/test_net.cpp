#include <doctest/doctest.h>

#include <random>

#include "net/netsim.hpp"
#include "net/protocol.hpp"
#include "net/ticket.hpp"

using namespace pb::net;

TEST_CASE("net: header + mọi loại thông điệp mã hóa/giải mã vòng") {
  Header h{kProtocolVersion, PacketType::Snapshot, 77, 70, 12345};
  Snapshot s;
  s.self = {42, 90, 80, 70, 60, 50, 0, 1234, 2, 3};
  for (uint32_t i = 0; i < 40; ++i) s.entities.push_back({i, kNpcDog, 1, -5000 + static_cast<int32_t>(i) * 100, 9000 - static_cast<int32_t>(i) * 37, -12, 65535, 3, 77});
  const auto pkt = encode(h, write(s));
  CHECK(pkt.size() <= kHeaderSize + kMaxPayload);
  Header h2;
  std::span<const uint8_t> pl;
  REQUIRE(decode_header(pkt, h2, pl));
  CHECK(h2.type == PacketType::Snapshot);
  CHECK(h2.sequence == 77);
  CHECK(h2.server_tick == 12345);
  Snapshot s2;
  REQUIRE(read(pl, s2));
  CHECK(s2.self.last_input_seq == 42);
  CHECK(s2.self.hour_x100 == 1234);
  REQUIRE(s2.entities.size() == 40);
  CHECK(s2.entities[5].x_cm == -4500);
  CHECK(s2.entities[39].z_cm == 9000 - 39 * 37);
  CHECK(s2.entities[5].y_cm == -12);
  CHECK(s2.entities[5].yaw == 65535);

  Hello he{kProtocolVersion, "abc.def", "hash", "build-1"}, he2;
  REQUIRE(read(write(he), he2));
  CHECK(he2.ticket == "abc.def");
  InputPacket ip{{{5, -127, 127, kSprint | kAttack, 100}, {6, 0, 0, 0, 0}}}, ip2;
  REQUIRE(read(write(ip), ip2));
  CHECK(ip2.cmds[0].move_x == -127);
  CHECK(ip2.cmds[0].buttons == (kSprint | kAttack));
  Welcome w{1, 2, 3, 30}, w2;
  REQUIRE(read(write(w), w2));
  CHECK(w2.tick_rate == 30);
}

TEST_CASE("net: gói sai bị từ chối an toàn (ngắn, quá cỡ, sai version, loại lạ, dư byte, bit lạ)") {
  Header h;
  std::span<const uint8_t> pl;
  std::vector<uint8_t> tiny(5, 0);
  CHECK_FALSE(decode_header(tiny, h, pl));
  std::vector<uint8_t> huge(kHeaderSize + kMaxPayload + 1, 0);
  CHECK_FALSE(decode_header(huge, h, pl));
  auto good = encode({kProtocolVersion, PacketType::Ping, 1, 0, 0}, {});
  auto badv = good;
  badv[0] = 99;
  CHECK_FALSE(decode_header(badv, h, pl));
  auto badt = good;
  badt[2] = 200;
  CHECK_FALSE(decode_header(badt, h, pl));
  badt[2] = static_cast<uint8_t>(PacketType::ChatMsg) + 1;  // ngay sau loại cuối cùng
  CHECK_FALSE(decode_header(badt, h, pl));
  // Mọi loại gói đã khai báo đều qua được header (lỗi thật đã gặp: thêm ChatSay nhưng quên nới giới hạn)
  for (uint16_t t = 1; t <= static_cast<uint16_t>(PacketType::ChatMsg); ++t) {
    auto ok = encode({kProtocolVersion, static_cast<PacketType>(t), 1, 0, 0}, {});
    CHECK(decode_header(ok, h, pl));
  }
  // Input có bit nút lạ hoặc số lệnh sai
  InputPacket ip{{{1, 0, 0, 0, 0}}};
  auto bytes = write(ip);
  bytes[7] = 0xFF;  // byte cao của buttons
  bytes[8] = 0xFF;
  InputPacket out;
  CHECK_FALSE(read(bytes, out));
  std::vector<uint8_t> zero_cmds{0};
  CHECK_FALSE(read(zero_cmds, out));
  auto extra = write(InputPacket{{{1, 0, 0, 0, 0}}});
  extra.push_back(1);
  CHECK_FALSE(read(extra, out));
  // Snapshot khai báo nhiều entity hơn dữ liệu thật
  Snapshot s;
  auto sb = write(s);
  sb[sb.size() - 9] = 50;  // byte đếm entity (sau nó là 8 byte gốc tọa độ)
  Snapshot so;
  CHECK_FALSE(read(sb, so));
}

TEST_CASE("net: fuzz 20.000 gói ngẫu nhiên không crash, không đọc tràn") {
  std::mt19937 rng(1234);
  int accepted = 0;
  for (int i = 0; i < 20000; ++i) {
    std::vector<uint8_t> pkt(rng() % 1300);
    for (auto& b : pkt) b = static_cast<uint8_t>(rng());
    if (i % 3 == 0 && pkt.size() >= 4) {  // header hợp lệ, payload rác
      pkt[0] = kProtocolVersion & 0xFF;
      pkt[1] = 0;
      pkt[2] = static_cast<uint8_t>(1 + rng() % 9);
      pkt[3] = 0;
    }
    Header h;
    std::span<const uint8_t> pl;
    if (!decode_header(pkt, h, pl)) continue;
    Hello a;
    Snapshot b;
    InputPacket c;
    EventMsg d;
    Welcome e;
    Reject f;
    accepted += read(pl, a) + read(pl, b) + read(pl, c) + read(pl, d) + read(pl, e) + read(pl, f);
  }
  CHECK(accepted < 200);  // gần như mọi gói rác đều bị từ chối
  // Cắt cụt mọi độ dài của một gói hợp lệ
  Snapshot s;
  s.entities.resize(10);
  const auto full = write(s);
  for (size_t n = 0; n < full.size(); ++n) {
    Snapshot o;
    CHECK_FALSE(read(std::span(full.data(), n), o));
  }
}

TEST_CASE("net: sequence quấn vòng") {
  CHECK(seq_newer(5, 4));
  CHECK_FALSE(seq_newer(4, 5));
  CHECK(seq_newer(2, 0xFFFFFFF0u));
  CHECK_FALSE(seq_newer(7, 7));
}

TEST_CASE("net: ticket Ed25519 — hợp lệ, giả mạo, sai khóa, hết hạn, sai server, dùng lại") {
  REQUIRE(crypto_init());
  PublicKey pk, pk2;
  SecretKey sk, sk2;
  generate_keypair(pk, sk);
  generate_keypair(pk2, sk2);
  TicketClaims c{7, 42, "dev-1", 1000, random_jti()};
  const auto t = sign_ticket(c, sk);
  TicketClaims out;
  ReplayCache rc;
  CHECK(verify_ticket(t, pk, "dev-1", 900, nullptr, out) == TicketError::Ok);
  CHECK(out.user_id == 7);
  CHECK(out.character_id == 42);
  CHECK(verify_ticket(t, pk2, "dev-1", 900, nullptr, out) == TicketError::BadSignature);
  auto tampered = t;
  tampered[3] = tampered[3] == 'A' ? 'B' : 'A';  // sửa payload
  CHECK(verify_ticket(tampered, pk, "dev-1", 900, nullptr, out) == TicketError::BadSignature);
  CHECK(verify_ticket(t, pk, "dev-1", 1001, nullptr, out) == TicketError::Expired);
  CHECK(verify_ticket(t, pk, "dev-2", 900, nullptr, out) == TicketError::WrongServer);
  CHECK(verify_ticket(t, pk, "dev-1", 900, &rc, out) == TicketError::Ok);
  CHECK(verify_ticket(t, pk, "dev-1", 901, &rc, out) == TicketError::Replayed);
  CHECK(verify_ticket("khong-co-cham", pk, "dev-1", 900, nullptr, out) == TicketError::Malformed);
  CHECK(verify_ticket(".", pk, "dev-1", 900, nullptr, out) == TicketError::Malformed);
  CHECK(verify_ticket(std::string(5000, 'x') + ".y", pk, "dev-1", 900, nullptr, out) == TicketError::Malformed);
}

TEST_CASE("net: token bucket giới hạn tần suất") {
  TokenBucket b(10, 5);
  int ok = 0;
  for (int i = 0; i < 20; ++i) ok += b.take(0.0);
  CHECK(ok == 5);  // burst
  ok = 0;
  for (int i = 0; i < 20; ++i) ok += b.take(1.0);
  CHECK(ok == 5);  // nạp lại tới burst sau 1 s
}

TEST_CASE("net: NetSim mất gói ~đúng tỉ lệ, trễ đúng thứ tự thời gian") {
  NetSim sim({0.05, 50, 10}, 99);
  int delivered = 0;
  double last_due = 0;
  for (int i = 0; i < 10000; ++i) sim.push(0.0, {1}, [&](const std::vector<uint8_t>&) { ++delivered; });
  sim.pump(0.03);
  CHECK(delivered == 0);  // trễ ≥ 40 ms
  sim.pump(0.1);
  CHECK(delivered == doctest::Approx(9500).epsilon(0.02));
  CHECK(sim.dropped() == static_cast<uint64_t>(10000 - delivered));
  (void)last_due;
}
