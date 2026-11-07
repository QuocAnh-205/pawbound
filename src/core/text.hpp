#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace pb {
// Bỏ dấu tiếng Việt để hiển thị bằng font chỉ có ASCII ("Chó Mực" -> "Cho Muc"); ký tự khác ngoài ASCII -> '?'.
std::string ascii_fold(std::string_view utf8);
// Tham số dòng lệnh dạng UTF-8. Windows: argv của MinGW theo mã ANSI -> đọc lại bằng GetCommandLineW.
std::vector<std::string> utf8_args(int argc, char** argv);
}  // namespace pb
