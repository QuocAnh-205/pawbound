#include "core/text.hpp"

#include <algorithm>
#include <unordered_map>
#include <utility>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif

namespace pb {
namespace {
// Font bitmap chỉ có ASCII: chữ tiếng Việt được BỎ DẤU để vẫn đọc được ("Chó Mực" -> "Cho Muc").
// Ký tự ngoài bảng -> '?'.
char32_t decode_utf8(std::string_view s, size_t& i) {
  const auto c = static_cast<unsigned char>(s[i]);
  const size_t len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
  char32_t cp = len == 1 ? c : c & (0x7F >> len);
  size_t k = 1;
  for (; k < len && i + k < s.size() && (static_cast<unsigned char>(s[i + k]) & 0xC0) == 0x80; ++k)
    cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
  i += k;
  return k == len ? cp : U'?';
}

const std::unordered_map<char32_t, char>& fold_table() {
  static const std::unordered_map<char32_t, char> t = [] {
    std::unordered_map<char32_t, char> m;
    const std::pair<char, std::string_view> groups[] = {
        {'a', "àáảãạăằắẳẵặâầấẩẫậ"}, {'A', "ÀÁẢÃẠĂẰẮẲẴẶÂẦẤẨẪẬ"}, {'e', "èéẻẽẹêềếểễệ"}, {'E', "ÈÉẺẼẸÊỀẾỂỄỆ"},
        {'i', "ìíỉĩị"}, {'I', "ÌÍỈĨỊ"}, {'o', "òóỏõọôồốổỗộơờớởỡợ"}, {'O', "ÒÓỎÕỌÔỒỐỔỖỘƠỜỚỞỠỢ"},
        {'u', "ùúủũụưừứửữự"}, {'U', "ÙÚỦŨỤƯỪỨỬỮỰ"}, {'y', "ỳýỷỹỵ"}, {'Y', "ỲÝỶỸỴ"}, {'d', "đ"}, {'D', "Đ"}};
    for (const auto& [base, chars] : groups)
      for (size_t i = 0; i < chars.size();) m[decode_utf8(chars, i)] = base;
    return m;
  }();
  return t;
}

}  // namespace

std::string ascii_fold(std::string_view s) {
  std::string out;
  for (size_t i = 0; i < s.size();) {
    const char32_t cp = decode_utf8(s, i);
    if (cp < 0x80) {
      out.push_back(static_cast<char>(cp));
      continue;
    }
    const auto it = fold_table().find(cp);
    out.push_back(it == fold_table().end() ? '?' : it->second);
  }
  return out;
}

std::vector<std::string> utf8_args(int argc, char** argv) {
  std::vector<std::string> out;
#if defined(_WIN32)
  int n = 0;
  if (wchar_t** w = CommandLineToArgvW(GetCommandLineW(), &n)) {
    for (int i = 0; i < n; ++i) {
      const int len = WideCharToMultiByte(CP_UTF8, 0, w[i], -1, nullptr, 0, nullptr, nullptr);
      std::string s(static_cast<size_t>(std::max(len, 1)) - 1, '\0');
      if (len > 1) WideCharToMultiByte(CP_UTF8, 0, w[i], -1, s.data(), len, nullptr, nullptr);
      out.push_back(std::move(s));
    }
    LocalFree(w);
    if (static_cast<int>(out.size()) == argc) return out;
    out.clear();
  }
#endif
  for (int i = 0; i < argc; ++i) out.emplace_back(argv[i]);
  return out;
}

}  // namespace pb
