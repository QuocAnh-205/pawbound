// Phase 12: chat gần — làm sạch văn bản, gói ChatSay/ChatMsg, hiển thị tiếng Việt bỏ dấu.
#include <doctest/doctest.h>

#include "core/text.hpp"
#include "net/protocol.hpp"

using namespace pb;

TEST_CASE("chat: sanitize_chat làm sạch và từ chối UTF-8 sai") {
  CHECK(net::sanitize_chat("  xin   chào  ") == "xin chào");
  CHECK(net::sanitize_chat("a\nb\tc\x01" "d") == "a b c d");
  CHECK(net::sanitize_chat("abc\xE2\x80\xAE" "def") == "abc def");  // U+202E đảo chiều hiển thị
  CHECK(net::sanitize_chat("zero\xE2\x80\x8Bwidth") == "zero width");  // U+200B
  CHECK(net::sanitize_chat("").empty());
  CHECK(net::sanitize_chat(" \n\t ").empty());
  CHECK(net::sanitize_chat("\xC3").empty());              // cụt
  CHECK(net::sanitize_chat("\xC0\xAF").empty());          // mã hóa dài thừa
  CHECK(net::sanitize_chat("\xED\xA0\x80").empty());      // surrogate
  CHECK(net::sanitize_chat("\xF4\x90\x80\x80").empty());  // > U+10FFFF
  CHECK(net::sanitize_chat("\xFF").empty());
  CHECK(net::sanitize_chat(std::string(net::kMaxChatBytes, 'x')).size() == net::kMaxChatBytes);
  CHECK(net::sanitize_chat(std::string(net::kMaxChatBytes + 1, 'x')).empty());
  CHECK(net::sanitize_chat("Chó Mực 🐕") == "Chó Mực 🐕");
}

TEST_CASE("chat: ChatSay/ChatMsg mã hóa vòng; gói sai/quá dài bị từ chối") {
  net::ChatMsg m{42, 0x1234567890ULL, "gâu gâu"};
  net::ChatMsg back;
  REQUIRE(net::read(net::write(m), back));
  CHECK(back.from_entity == 42);
  CHECK(back.from_user == 0x1234567890ULL);
  CHECK(back.text == "gâu gâu");
  net::ChatSay s{"xin chào"}, s2;
  REQUIRE(net::read(net::write(s), s2));
  CHECK(s2.text == "xin chào");
  auto bytes = net::write(net::ChatSay{std::string(net::kMaxChatBytes + 10, 'a')});
  CHECK_FALSE(net::read(bytes, s2));
  bytes = net::write(s);
  bytes.push_back(0);  // dư byte
  CHECK_FALSE(net::read(bytes, s2));
}

TEST_CASE("text: ascii_fold bỏ dấu tiếng Việt cho font ASCII") {
  CHECK(ascii_fold("Chó Mực đi ăn") == "Cho Muc di an");
  CHECK(ascii_fold("ĐƯỜNG PHỐ Ồn Ào") == "DUONG PHO On Ao");
  CHECK(ascii_fold("Thời tiết: mưa") == "Thoi tiet: mua");
  CHECK(ascii_fold("ok 🐕") == "ok ?");
  CHECK(ascii_fold("\xC3") == "?");
}
