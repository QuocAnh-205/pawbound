// PAWBOUND CLI tools: pawbound <nhóm> <lệnh> ...
// Exit code: 0 = thành công/sạch, 1 = kiểm tra thất bại, 2 = lỗi sử dụng/I-O.
#include <algorithm>
#include <cstdio>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include "asset/manifest.hpp"
#include "asset/model.hpp"
#include "asset/placeholders.hpp"
#include "asset/sha256.hpp"
#include "core/version.hpp"
#include "tools/commands.hpp"

namespace {
using Args = std::vector<std::string_view>;

void usage() {
  std::puts(
      "pawbound — công cụ dòng lệnh PAWBOUND\n"
      "  pawbound --version\n"
      "  pawbound asset inspect <file.glb>\n"
      "  pawbound asset validate [--manifest assets/manifest.json] [id ...]\n"
      "  pawbound asset manifest [--manifest assets/manifest.json] --update | --scan <thư mục>\n"
      "  pawbound asset make-placeholders [--out assets/placeholder]\n"
      "  pawbound world generate --seed <n> --config <file> --output <dir>\n"
      "  pawbound world validate <dir>\n"
      "  pawbound world inspect <dir>\n"
      "  pawbound net keygen [--out config/dev_keys]\n"
      "  pawbound net ticket [--sk file] [--user n] [--char n] [--server id] [--ttl s]");
}

std::string_view opt(const Args& a, std::string_view name, std::string_view def) {
  for (size_t i = 0; i + 1 < a.size(); ++i)
    if (a[i] == name) return a[i + 1];
  return def;
}
bool flag(const Args& a, std::string_view name) {
  for (auto x : a)
    if (x == name) return true;
  return false;
}

void print(const std::string& s) { std::fputs(s.c_str(), stdout); }

int asset_inspect(const Args& a) {
  if (a.empty()) return 2;
  auto r = pb::asset::load_gltf(std::string(a[0]));
  if (!r.ok()) {
    std::fprintf(stderr, "LỖI: %s\n", r.error.c_str());
    return 2;
  }
  const auto& m = r.model;
  print(std::format("file: {}\nnodes: {}  meshes: {}  materials: {}  images: {}  skins: {}  animations: {}\n",
                    std::string(a[0]), m.nodes.size(), m.meshes.size(), m.materials.size(), m.images.size(),
                    m.skins.size(), m.animations.size()));
  print(std::format("bounds: min({:.3f}, {:.3f}, {:.3f}) max({:.3f}, {:.3f}, {:.3f})  cao {:.3f}\n", m.bounds.min.x,
                    m.bounds.min.y, m.bounds.min.z, m.bounds.max.x, m.bounds.max.y, m.bounds.max.z,
                    m.bounds.max.y - m.bounds.min.y));
  for (const auto& mesh : m.meshes)
    for (const auto& p : mesh.primitives)
      print(std::format("  mesh '{}': {} vertex, {} tam giác, material {}, skinned {}\n", mesh.name,
                        p.mesh.vertices.size(), p.mesh.triangle_count(), p.material, !p.joints.empty()));
  for (const auto& mat : m.materials)
    print(std::format("  material '{}': base({:.2f},{:.2f},{:.2f},{:.2f}) metal {:.2f} rough {:.2f} tex base {} normal {} orm {}\n",
                      mat.name, mat.base_color.r, mat.base_color.g, mat.base_color.b, mat.base_color.a, mat.metallic,
                      mat.roughness, mat.base_image, mat.normal_image, mat.orm_image));
  for (const auto& img : m.images) print(std::format("  image '{}': {}x{}\n", img.name, img.image.width, img.image.height));
  for (const auto& s : m.skins) {
    print(std::format("  skin: {} joint (root: {})\n", s.size(), s.names.empty() ? "-" : s.names[0]));
  }
  for (const auto& an : m.animations)
    print(std::format("  animation '{}': {:.3f} s, {} track\n", an.name, an.duration, an.tracks.size()));
  for (const auto& w : m.warnings) print(std::format("  CẢNH BÁO: {}\n", w));
  return 0;
}

int asset_validate(const Args& a) {
  const std::string path(opt(a, "--manifest", "assets/manifest.json"));
  pb::ConfigDiagnostics diag;
  auto m = pb::asset::load_manifest(path, diag);
  for (const auto& e : diag.errors) std::fprintf(stderr, "LỖI manifest: %s\n", e.c_str());
  if (m.entries.empty() && !diag.ok()) return 2;
  std::vector<std::string_view> ids;
  for (size_t i = 0; i < a.size(); ++i) {
    if (a[i] == "--manifest") {
      ++i;
      continue;
    }
    ids.push_back(a[i]);
  }
  int errors = static_cast<int>(diag.errors.size()), warnings = 0, checked = 0;
  for (const auto& e : m.entries) {
    if (!ids.empty() && std::find(ids.begin(), ids.end(), e.id) == ids.end()) continue;
    ++checked;
    const auto findings = pb::asset::validate_entry(m, e);
    int ee = 0;
    for (const auto& f : findings) {
      print(std::format("  [{}] {}: {}\n", f.error ? "LỖI" : "cảnh báo", f.asset_id, f.message));
      (f.error ? ee : warnings)++;
    }
    errors += ee;
    print(std::format("{} {} ({}, {})\n", ee ? "FAIL" : "OK  ", e.id, e.type, e.quality));
  }
  print(std::format("Đã kiểm tra {} asset: {} lỗi, {} cảnh báo\n", checked, errors, warnings));
  return errors ? 1 : 0;
}

int asset_manifest(const Args& a) {
  const std::filesystem::path path(std::string(opt(a, "--manifest", "assets/manifest.json")));
  pb::ConfigDiagnostics diag;
  auto m = std::filesystem::exists(path) ? pb::asset::load_manifest(path, diag) : pb::asset::Manifest{};
  m.base_dir = path.parent_path();
  if (flag(a, "--update")) {
    for (auto& e : m.entries) {
      if (auto h = pb::asset::sha256_file_hex(m.base_dir / e.path)) {
        if (e.sha256 != *h) print(std::format("cập nhật sha256 {}\n", e.id));
        e.sha256 = *h;
      } else {
        std::fprintf(stderr, "không đọc được %s\n", e.path.c_str());
      }
    }
  }
  if (auto dir = opt(a, "--scan", ""); !dir.empty()) {
    for (const auto& f : std::filesystem::recursive_directory_iterator(std::string(dir))) {
      if (f.path().extension() != ".glb" && f.path().extension() != ".gltf") continue;
      const auto rel = std::filesystem::relative(f.path(), m.base_dir).generic_string();
      bool known = false;
      for (const auto& e : m.entries) known |= e.path == rel;
      if (known) continue;
      pb::asset::ManifestEntry e;
      e.id = f.path().stem().string();
      e.path = rel;
      e.type = "prop";
      e.quality = "prototype";
      e.license = "UNKNOWN";  // người nhập phải điền; validator sẽ FAIL tới khi có license hợp lệ
      e.sha256 = pb::asset::sha256_file_hex(f.path()).value_or("");
      print(std::format("thêm mục mới '{}' (license UNKNOWN — cần điền tay)\n", e.id));
      m.entries.push_back(e);
    }
  }
  if (!pb::asset::save_manifest(path, m)) return 2;
  print(std::format("đã ghi {} ({} asset)\n", path.string(), m.entries.size()));
  return 0;
}

int asset_placeholders(const Args& a) {
  const std::filesystem::path out(std::string(opt(a, "--out", "assets/placeholder")));
  pb::asset::PlaceholderDog dog;
  pb::asset::make_placeholder_dog(dog);
  std::string err;
  if (!pb::asset::write_glb(out / "placeholder_dog.glb", dog.desc, err)) {
    std::fprintf(stderr, "LỖI: %s\n", err.c_str());
    return 2;
  }
  print(std::format("đã ghi {}\n", (out / "placeholder_dog.glb").string()));
  return 0;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    usage();
    return 2;
  }
  const std::string_view g = argv[1];
  if (g == "--version") {
    std::printf("pawbound %s\n", pb::kVersionString);
    return 0;
  }
  if (g == "help" || g == "--help") {
    usage();
    return 0;
  }
  if (argc < 3) {
    usage();
    return 2;
  }
  const std::string_view cmd = argv[2];
  Args rest;
  for (int i = 3; i < argc; ++i) rest.emplace_back(argv[i]);
  if (g == "asset") {
    if (cmd == "inspect") return asset_inspect(rest);
    if (cmd == "validate") return asset_validate(rest);
    if (cmd == "manifest") return asset_manifest(rest);
    if (cmd == "make-placeholders") return asset_placeholders(rest);
  }
  if (g == "world") return pb::tools::world_command(cmd, rest);
  if (g == "net") return pb::tools::net_command(cmd, rest);
  std::fprintf(stderr, "lệnh không hợp lệ: %s %s\n", argv[1], argv[2]);
  usage();
  return 2;
}
