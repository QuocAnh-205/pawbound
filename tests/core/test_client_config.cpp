#include <doctest/doctest.h>

#include "core/client_config.hpp"

using pb::parse_client_config;

TEST_CASE("client config: JSON đầy đủ hợp lệ") {
  auto r = parse_client_config(R"({"window_width":1600,"window_height":900,"window_title":"T",
                                     "vsync":false,"log_level":"debug","log_file":""})");
  REQUIRE(r.ok());
  CHECK(r.warnings.empty());
  CHECK(r.config.window_width == 1600);
  CHECK(r.config.window_height == 900);
  CHECK(r.config.vsync == false);
  CHECK(r.config.log_level == "debug");
}

TEST_CASE("client config: thiếu field dùng mặc định và cảnh báo") {
  auto r = parse_client_config("{}");
  REQUIRE(r.ok());
  CHECK(r.config.window_width == 1280);
  CHECK(r.warnings.size() == 6);
}

TEST_CASE("client config: sai kiểu / ngoài khoảng / log_level sai là lỗi") {
  CHECK_FALSE(parse_client_config(R"({"window_width":"to"})").ok());
  CHECK_FALSE(parse_client_config(R"({"window_width":10})").ok());
  CHECK_FALSE(parse_client_config(R"({"log_level":"loud"})").ok());
  CHECK_FALSE(parse_client_config("{ không phải json").ok());
  CHECK_FALSE(parse_client_config("[1,2]").ok());
}
