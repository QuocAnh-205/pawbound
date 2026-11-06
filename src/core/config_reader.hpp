#pragma once
// Đọc config JSON có validation và giá trị mặc định.
// Thiếu key -> giữ mặc định + warning. Sai kiểu / ngoài khoảng -> error (giữ mặc định).
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace pb {

struct ConfigDiagnostics {
  std::vector<std::string> errors;
  std::vector<std::string> warnings;
  [[nodiscard]] bool ok() const { return errors.empty(); }
};

// Parse text JSON (cho phép comment). Lỗi parse -> diag.errors, trả object rỗng.
nlohmann::json parse_json_config(std::string_view text, ConfigDiagnostics& diag);
// Đọc file. Không mở được: nếu required -> error, ngược lại warning; trả object rỗng.
nlohmann::json load_json_config(const std::filesystem::path& path, ConfigDiagnostics& diag, bool required);

class ConfigReader {
 public:
  ConfigReader(const nlohmann::json& obj, std::string path, ConfigDiagnostics& diag);

  void read(const char* key, int& out, int min, int max);
  void read(const char* key, double& out, double min, double max);
  void read(const char* key, float& out, double min, double max);
  void read(const char* key, bool& out);
  void read(const char* key, std::string& out);
  void read(const char* key, std::vector<std::string>& out);
  // Mảng 2 số, vd [min, max].
  void read_range(const char* key, int& lo, int& hi, int min, int max);
  // Object con. Thiếu -> warning, trả reader trên object rỗng (mọi giá trị giữ mặc định).
  [[nodiscard]] ConfigReader child(const char* key);
  [[nodiscard]] bool has(const char* key) const;
  [[nodiscard]] const nlohmann::json& raw() const { return obj_; }
  [[nodiscard]] ConfigDiagnostics& diagnostics() { return diag_; }
  [[nodiscard]] std::string key_path(const char* key) const;

 private:
  const nlohmann::json* find(const char* key);
  const nlohmann::json& obj_;
  std::string path_;
  ConfigDiagnostics& diag_;
};

}  // namespace pb
