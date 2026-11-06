#include "asset/manifest.hpp"

#include <format>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "asset/model.hpp"
#include "asset/sha256.hpp"

namespace pb::asset {

const ManifestEntry* Manifest::find(const std::string& id) const {
  for (const auto& e : entries)
    if (e.id == id) return &e;
  return nullptr;
}

Manifest load_manifest(const std::filesystem::path& path, ConfigDiagnostics& diag) {
  Manifest m;
  m.base_dir = path.parent_path();
  auto j = load_json_config(path, diag, /*required=*/true);
  if (!diag.ok()) return m;
  if (!j.contains("assets") || !j["assets"].is_array()) {
    diag.errors.emplace_back("manifest phải có mảng 'assets'");
    return m;
  }
  size_t i = 0;
  for (const auto& a : j["assets"]) {
    ManifestEntry e;
    ConfigDiagnostics local;
    ConfigReader r(a, std::format("assets[{}]", i++), local);
    r.read("id", e.id);
    r.read("path", e.path);
    r.read("type", e.type);
    r.read("quality", e.quality);
    r.read("source", e.source);
    r.read("author", e.author);
    r.read("license", e.license);
    r.read("scale", e.scale, 1e-6, 1e6);
    if (r.has("sha256")) r.read("sha256", e.sha256);
    if (r.has("height_m")) r.read("height_m", e.height_m, 0.0, 1000.0);
    if (r.has("tags")) r.read("tags", e.tags);
    if (r.has("required_animations")) r.read("required_animations", e.required_animations);
    if (a.contains("footprint_m") && a["footprint_m"].is_array() && a["footprint_m"].size() == 2)
      e.footprint_m = {a["footprint_m"][0].get<float>(), a["footprint_m"][1].get<float>()};
    // Thiếu metadata bắt buộc là lỗi (không phải cảnh báo).
    for (auto& w : local.warnings) diag.errors.push_back(w);
    for (auto& er : local.errors) diag.errors.push_back(er);
    m.entries.push_back(std::move(e));
  }
  return m;
}

bool save_manifest(const std::filesystem::path& path, const Manifest& m) {
  nlohmann::ordered_json arr = nlohmann::ordered_json::array();
  for (const auto& e : m.entries) {
    nlohmann::ordered_json a;
    a["id"] = e.id;
    a["path"] = e.path;
    a["type"] = e.type;
    a["quality"] = e.quality;
    a["source"] = e.source;
    a["author"] = e.author;
    a["license"] = e.license;
    a["scale"] = e.scale;
    a["sha256"] = e.sha256;
    if (e.height_m > 0) a["height_m"] = e.height_m;
    if (e.footprint_m.x > 0) a["footprint_m"] = {e.footprint_m.x, e.footprint_m.y};
    if (!e.tags.empty()) a["tags"] = e.tags;
    if (!e.required_animations.empty()) a["required_animations"] = e.required_animations;
    arr.push_back(a);
  }
  nlohmann::ordered_json root;
  root["format_version"] = 1;
  root["assets"] = arr;
  std::ofstream out(path, std::ios::binary);
  if (!out) return false;
  out << root.dump(2) << '\n';
  return out.good();
}

bool license_allowed(const std::string& expr) {
  static const char* allowed[] = {"CC0-1.0", "CC-BY-4.0", "MIT", "Proprietary-Owned", "PAWBOUND-Generated"};
  if (expr.empty()) return false;
  std::string rest = expr;
  for (;;) {
    const auto pos = rest.find(" AND ");
    const std::string part = rest.substr(0, pos);
    bool ok = false;
    for (const char* a : allowed) ok |= part == a;
    if (!ok) return false;
    if (pos == std::string::npos) return true;
    rest = rest.substr(pos + 5);
  }
}

namespace {
struct TypeLimits {
  const char* type;
  size_t max_tris;
  double min_h, max_h;
};
constexpr TypeLimits kLimits[] = {
    {"character", 40000, 0.2, 2.0}, {"building", 30000, 2.0, 80.0}, {"prop", 8000, 0.05, 12.0},
    {"vegetation", 15000, 0.1, 30.0}, {"terrain", 200000, 0.0, 1000.0},
};
}  // namespace

std::vector<Finding> validate_entry(const Manifest& m, const ManifestEntry& e) {
  std::vector<Finding> f;
  auto err = [&](std::string s) { f.push_back({true, e.id, std::move(s)}); };
  auto warn = [&](std::string s) { f.push_back({false, e.id, std::move(s)}); };

  if (e.id.empty()) err("thiếu id");
  if (!license_allowed(e.license)) err(std::format("license '{}' không có hoặc không nằm trong danh sách cho phép", e.license));
  if (e.source.empty()) err("thiếu source (nguồn gốc asset)");
  if (e.author.empty()) err("thiếu author");
  if (e.quality != "placeholder" && e.quality != "prototype" && e.quality != "production")
    err(std::format("quality '{}' phải là placeholder|prototype|production", e.quality));
  const TypeLimits* lim = nullptr;
  for (const auto& l : kLimits)
    if (e.type == l.type) lim = &l;
  if (!lim) err(std::format("type '{}' không hợp lệ", e.type));

  const auto file = m.base_dir / e.path;
  if (!std::filesystem::exists(file)) {
    err("không tìm thấy file " + file.string());
    return f;
  }
  if (e.sha256.empty()) {
    err("thiếu sha256 (chạy 'pawbound asset manifest --update')");
  } else if (auto h = sha256_file_hex(file); !h || *h != e.sha256) {
    err(std::format("sha256 không khớp (file đã bị thay đổi): manifest {} != thực tế {}", e.sha256, h.value_or("?")));
  }

  auto lr = load_gltf(file);
  if (!lr.ok()) {
    err("không đọc được glTF: " + lr.error);
    return f;
  }
  const Model& model = lr.model;
  for (const auto& w : model.warnings) warn(w);
  if (model.meshes.empty()) err("không có mesh");
  for (size_t i = 0; i < model.materials.size(); ++i) {
    const auto& mat = model.materials[i];
    if (mat.base_image < 0 && mat.base_color == glm::vec4(1.0f))
      warn(std::format("material '{}' không có texture màu và base color trắng", mat.name));
  }
  for (const auto& mesh : model.meshes)
    for (const auto& p : mesh.primitives)
      if (p.material < 0) warn(std::format("mesh '{}' có primitive không có material", mesh.name));
  for (const auto& s : model.skins)
    if (auto se = validate_skeleton(s); !se.empty()) err("skeleton: " + se);
  for (const auto& req : e.required_animations)
    if (!model.find_animation(req)) err(std::format("thiếu animation bắt buộc '{}'", req));

  if (lim) {
    const size_t tris = model.triangle_count();
    if (tris == 0) err("0 tam giác");
    if (tris > lim->max_tris) err(std::format("{} tam giác vượt giới hạn {} của loại {}", tris, lim->max_tris, e.type));
    if (model.bounds.valid()) {
      const double h = (model.bounds.max.y - model.bounds.min.y) * e.scale;
      if (h < lim->min_h || h > lim->max_h)
        err(std::format("chiều cao sau scale {:.3f} m ngoài khoảng [{}, {}] của loại {} (scale sai?)", h, lim->min_h,
                        lim->max_h, e.type));
      if (e.height_m > 0 && std::abs(h - e.height_m) > e.height_m * 0.15)
        warn(std::format("chiều cao {:.2f} m lệch hơn 15% so với khai báo {:.2f} m", h, e.height_m));
    }
  }
  return f;
}

}  // namespace pb::asset
