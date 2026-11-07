#pragma once
// Điện thoại (Android): lần chạy đầu chép asset/data/config từ APK vào bộ nhớ riêng của app, sinh thế giới
// theo seed (cùng hash với PC/server), chdir vào đó và bổ sung tham số mặc định. Trên PC: không làm gì.
#include <string>
#include <vector>

namespace pb::client {

// Trả danh sách tham số (args[0] = tên chương trình) đã bổ sung cho nền tảng di động.
std::vector<std::string> mobile_bootstrap(std::vector<std::string> args, std::string& error);
[[nodiscard]] bool is_mobile();

}  // namespace pb::client
