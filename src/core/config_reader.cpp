#include "core/config_reader.hpp"

#include <format>
#include <fstream>
#include <sstream>

namespace pb {
namespace {
const nlohmann::json& empty_object() {
  static const nlohmann::json e = nlohmann::json::object();
  return e;
}
}  // namespace

nlohmann::json parse_json_config(std::string_view text, ConfigDiagnostics& diag) {
  auto j = nlohmann::json::parse(text, nullptr, /*allow_exceptions=*/false, /*ignore_comments=*/true);
  if (j.is_discarded()) {
    diag.errors.emplace_back("JSON không hợp lệ");
    return nlohmann::json::object();
  }
  if (!j.is_object()) {
    diag.errors.emplace_back("gốc JSON phải là object");
    return nlohmann::json::object();
  }
  return j;
}

nlohmann::json load_json_config(const std::filesystem::path& path, ConfigDiagnostics& diag, bool required) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    auto msg = std::format("không mở được {}", path.string());
    if (required) diag.errors.push_back(msg);
    else diag.warnings.push_back(msg + ", dùng toàn bộ mặc định");
    return nlohmann::json::object();
  }
  std::stringstream ss;
  ss << in.rdbuf();
  return parse_json_config(ss.str(), diag);
}

ConfigReader::ConfigReader(const nlohmann::json& obj, std::string path, ConfigDiagnostics& diag)
    : obj_(obj.is_object() ? obj : empty_object()), path_(std::move(path)), diag_(diag) {}

std::string ConfigReader::key_path(const char* key) const {
  return path_.empty() ? std::string(key) : path_ + "." + key;
}

bool ConfigReader::has(const char* key) const { return obj_.contains(key); }

const nlohmann::json* ConfigReader::find(const char* key) {
  auto it = obj_.find(key);
  if (it == obj_.end()) {
    diag_.warnings.push_back(std::format("thiếu '{}', dùng mặc định", key_path(key)));
    return nullptr;
  }
  return &*it;
}

void ConfigReader::read(const char* key, int& out, int min, int max) {
  const auto* v = find(key);
  if (!v) return;
  if (!v->is_number_integer()) {
    diag_.errors.push_back(std::format("'{}' phải là số nguyên", key_path(key)));
    return;
  }
  const auto x = v->get<long long>();
  if (x < min || x > max) {
    diag_.errors.push_back(std::format("'{}'={} ngoài khoảng [{}, {}]", key_path(key), x, min, max));
    return;
  }
  out = static_cast<int>(x);
}

void ConfigReader::read(const char* key, double& out, double min, double max) {
  const auto* v = find(key);
  if (!v) return;
  if (!v->is_number()) {
    diag_.errors.push_back(std::format("'{}' phải là số", key_path(key)));
    return;
  }
  const double x = v->get<double>();
  if (!(x >= min && x <= max)) {
    diag_.errors.push_back(std::format("'{}'={} ngoài khoảng [{}, {}]", key_path(key), x, min, max));
    return;
  }
  out = x;
}

void ConfigReader::read(const char* key, float& out, double min, double max) {
  double v = out;
  read(key, v, min, max);
  out = static_cast<float>(v);
}

void ConfigReader::read(const char* key, bool& out) {
  const auto* v = find(key);
  if (!v) return;
  if (!v->is_boolean()) {
    diag_.errors.push_back(std::format("'{}' phải là true/false", key_path(key)));
    return;
  }
  out = v->get<bool>();
}

void ConfigReader::read(const char* key, std::string& out) {
  const auto* v = find(key);
  if (!v) return;
  if (!v->is_string()) {
    diag_.errors.push_back(std::format("'{}' phải là chuỗi", key_path(key)));
    return;
  }
  out = v->get<std::string>();
}

void ConfigReader::read(const char* key, std::vector<std::string>& out) {
  const auto* v = find(key);
  if (!v) return;
  if (!v->is_array()) {
    diag_.errors.push_back(std::format("'{}' phải là mảng chuỗi", key_path(key)));
    return;
  }
  std::vector<std::string> tmp;
  for (const auto& e : *v) {
    if (!e.is_string()) {
      diag_.errors.push_back(std::format("'{}' phải là mảng chuỗi", key_path(key)));
      return;
    }
    tmp.push_back(e.get<std::string>());
  }
  out = std::move(tmp);
}

void ConfigReader::read_range(const char* key, int& lo, int& hi, int min, int max) {
  const auto* v = find(key);
  if (!v) return;
  if (!v->is_array() || v->size() != 2 || !(*v)[0].is_number_integer() || !(*v)[1].is_number_integer()) {
    diag_.errors.push_back(std::format("'{}' phải là mảng 2 số nguyên [min, max]", key_path(key)));
    return;
  }
  const auto a = (*v)[0].get<long long>(), b = (*v)[1].get<long long>();
  if (a > b || a < min || b > max) {
    diag_.errors.push_back(std::format("'{}'=[{}, {}] không hợp lệ (cần {} ≤ min ≤ max ≤ {})", key_path(key), a, b,
                                       min, max));
    return;
  }
  lo = static_cast<int>(a);
  hi = static_cast<int>(b);
}

ConfigReader ConfigReader::child(const char* key) {
  auto it = obj_.find(key);
  if (it == obj_.end()) {
    diag_.warnings.push_back(std::format("thiếu object '{}', dùng mặc định", key_path(key)));
    return ConfigReader(empty_object(), key_path(key), diag_);
  }
  if (!it->is_object()) {
    diag_.errors.push_back(std::format("'{}' phải là object", key_path(key)));
    return ConfigReader(empty_object(), key_path(key), diag_);
  }
  return ConfigReader(*it, key_path(key), diag_);
}

}  // namespace pb
