#include <doctest/doctest.h>

#include "core/log.hpp"

using namespace pb::log;

TEST_CASE("log: parse_level chấp nhận tên hợp lệ, không phân biệt hoa thường") {
  CHECK(parse_level("INFO") == Level::Info);
  CHECK(parse_level("warning") == Level::Warn);
  CHECK(parse_level("off") == Level::Off);
  CHECK_FALSE(parse_level("verbose").has_value());
  CHECK_FALSE(parse_level("").has_value());
}

TEST_CASE("log: format_line có timestamp UTC, level và category") {
  using namespace std::chrono;
  const sys_days day = year{2026} / November / 6;
  const auto t = day + hours{10} + minutes{20} + seconds{30} + milliseconds{123};
  const auto line = format_line(Level::Warn, "net", "xin chào", t);
  CHECK(line == "2026-11-06T10:20:30.123Z [WARN ] [net] xin chào");
}

TEST_CASE("log: lọc theo level") {
  const auto old = level();
  set_level(Level::Warn);
  CHECK_FALSE(enabled(Level::Info));
  CHECK(enabled(Level::Error));
  CHECK_FALSE(enabled(Level::Off));
  set_level(old);
}
