#include "asset/glb_writer.hpp"

#include <cstring>
#include <fstream>

#include <glm/gtc/type_ptr.hpp>
#include <nlohmann/json.hpp>

namespace pb::asset {
namespace {
using nlohmann::json;

struct Bin {
  std::vector<uint8_t> data;
  json views = json::array();
  json accessors = json::array();

  size_t add_view(const void* p, size_t bytes, int target = 0) {
    while (data.size() % 4) data.push_back(0);
    json v = {{"buffer", 0}, {"byteOffset", data.size()}, {"byteLength", bytes}};
    if (target) v["target"] = target;
    const auto* b = static_cast<const uint8_t*>(p);
    data.insert(data.end(), b, b + bytes);
    views.push_back(v);
    return views.size() - 1;
  }
  size_t add_accessor(size_t view, int comp_type, size_t count, const char* type, json extra = json::object()) {
    json a = {{"bufferView", view}, {"componentType", comp_type}, {"count", count}, {"type", type}};
    a.update(extra);
    accessors.push_back(a);
    return accessors.size() - 1;
  }
};
constexpr int kFloat = 5126, kUInt = 5125, kUShort = 5123;
constexpr int kArray = 34962, kElementArray = 34963;
}  // namespace

bool write_glb(const std::filesystem::path& path, const GlbDesc& d, std::string& error) {
  const auto& m = d.mesh;
  if (m.vertices.empty() || m.indices.empty()) {
    error = "mesh rỗng";
    return false;
  }
  const bool skinned = d.skeleton != nullptr;
  if (skinned && (d.joints.size() != m.vertices.size() || d.weights.size() != m.vertices.size())) {
    error = "số joints/weights không khớp số vertex";
    return false;
  }
  Bin bin;
  const size_t n = m.vertices.size();
  std::vector<float> pos, nrm, uv;
  pos.reserve(n * 3);
  for (const auto& v : m.vertices) {
    pos.insert(pos.end(), {v.position.x, v.position.y, v.position.z});
    nrm.insert(nrm.end(), {v.normal.x, v.normal.y, v.normal.z});
    uv.insert(uv.end(), {v.uv.x, v.uv.y});
  }
  geom::Aabb b;
  for (const auto& v : m.vertices) b.expand(v.position);
  json attrs;
  attrs["POSITION"] = bin.add_accessor(bin.add_view(pos.data(), pos.size() * 4, kArray), kFloat, n, "VEC3",
                                       {{"min", {b.min.x, b.min.y, b.min.z}}, {"max", {b.max.x, b.max.y, b.max.z}}});
  attrs["NORMAL"] = bin.add_accessor(bin.add_view(nrm.data(), nrm.size() * 4, kArray), kFloat, n, "VEC3");
  attrs["TEXCOORD_0"] = bin.add_accessor(bin.add_view(uv.data(), uv.size() * 4, kArray), kFloat, n, "VEC2");
  const size_t idx = bin.add_accessor(bin.add_view(m.indices.data(), m.indices.size() * 4, kElementArray), kUInt,
                                      m.indices.size(), "SCALAR");

  json gltf;
  gltf["asset"] = {{"version", "2.0"}, {"generator", "pawbound glb_writer"}};
  json mat = {{"name", d.name + "_mat"},
              {"pbrMetallicRoughness",
               {{"baseColorFactor", {d.base_color.r, d.base_color.g, d.base_color.b, d.base_color.a}},
                {"metallicFactor", d.metallic},
                {"roughnessFactor", d.roughness}}}};
  if (d.base_texture) {
    const auto png = geom::encode_png(*d.base_texture);
    if (png.empty()) {
      error = "không mã hóa được texture PNG";
      return false;
    }
    const size_t view = bin.add_view(png.data(), png.size());
    gltf["images"] = json::array({{{"bufferView", view}, {"mimeType", "image/png"}}});
    gltf["samplers"] = json::array({json::object()});
    gltf["textures"] = json::array({{{"source", 0}, {"sampler", 0}}});
    mat["pbrMetallicRoughness"]["baseColorTexture"] = {{"index", 0}};
  }
  gltf["materials"] = json::array({mat});

  json nodes = json::array();
  json mesh_node = {{"name", d.name}, {"mesh", 0}};
  if (skinned) {
    const Skeleton& s = *d.skeleton;
    std::vector<uint16_t> jdata;
    std::vector<float> wdata;
    for (size_t i = 0; i < n; ++i) {
      jdata.insert(jdata.end(), d.joints[i].begin(), d.joints[i].end());
      wdata.insert(wdata.end(), {d.weights[i].x, d.weights[i].y, d.weights[i].z, d.weights[i].w});
    }
    attrs["JOINTS_0"] = bin.add_accessor(bin.add_view(jdata.data(), jdata.size() * 2, kArray), kUShort, n, "VEC4");
    attrs["WEIGHTS_0"] = bin.add_accessor(bin.add_view(wdata.data(), wdata.size() * 4, kArray), kFloat, n, "VEC4");
    // node 0 = mesh, node 1.. = joint
    std::vector<json> jn(s.size());
    json joints = json::array();
    for (size_t j = 0; j < s.size(); ++j) {
      const auto& t = s.bind_local[j];
      jn[j] = {{"name", s.names[j]},
               {"translation", {t.t.x, t.t.y, t.t.z}},
               {"rotation", {t.r.x, t.r.y, t.r.z, t.r.w}},
               {"scale", {t.s.x, t.s.y, t.s.z}}};
      joints.push_back(1 + j);
    }
    for (size_t j = 0; j < s.size(); ++j)
      if (s.parents[j] >= 0) jn[static_cast<size_t>(s.parents[j])]["children"].push_back(1 + j);
    std::vector<float> ib;
    for (const auto& mtx : s.inverse_bind) ib.insert(ib.end(), glm::value_ptr(mtx), glm::value_ptr(mtx) + 16);
    const size_t ibacc = bin.add_accessor(bin.add_view(ib.data(), ib.size() * 4), kFloat, s.size(), "MAT4");
    gltf["skins"] = json::array({{{"joints", joints}, {"inverseBindMatrices", ibacc}}});
    mesh_node["skin"] = 0;
    nodes.push_back(mesh_node);
    for (auto& j : jn) nodes.push_back(j);
    json scene_nodes = json::array({0});
    for (size_t j = 0; j < s.size(); ++j)
      if (s.parents[j] < 0) scene_nodes.push_back(1 + j);
    gltf["scenes"] = json::array({{{"nodes", scene_nodes}}});

    json anims = json::array();
    for (const auto& clip : d.animations) {
      json samplers = json::array(), channels = json::array();
      for (const auto& tr : clip.tracks) {
        if (tr.interp == Interp::CubicSpline) {
          error = "glb_writer chưa hỗ trợ CubicSpline";
          return false;
        }
        const int comps = tr.path == Path::Rotation ? 4 : 3;
        std::vector<float> vals;
        for (const auto& v : tr.values)
          for (int c = 0; c < comps; ++c) vals.push_back(v[c]);
        const auto tmin = tr.times.front(), tmax = tr.times.back();
        const size_t in = bin.add_accessor(bin.add_view(tr.times.data(), tr.times.size() * 4), kFloat, tr.times.size(),
                                           "SCALAR", {{"min", {tmin}}, {"max", {tmax}}});
        const size_t out = bin.add_accessor(bin.add_view(vals.data(), vals.size() * 4), kFloat, tr.values.size(),
                                            comps == 4 ? "VEC4" : "VEC3");
        samplers.push_back({{"input", in}, {"output", out}, {"interpolation", tr.interp == Interp::Step ? "STEP" : "LINEAR"}});
        const char* path_name = tr.path == Path::Rotation ? "rotation" : tr.path == Path::Scale ? "scale" : "translation";
        channels.push_back({{"sampler", samplers.size() - 1}, {"target", {{"node", 1 + tr.joint}, {"path", path_name}}}});
      }
      anims.push_back({{"name", clip.name}, {"samplers", samplers}, {"channels", channels}});
    }
    if (!anims.empty()) gltf["animations"] = anims;
  } else {
    nodes.push_back(mesh_node);
    gltf["scenes"] = json::array({{{"nodes", {0}}}});
  }
  gltf["scene"] = 0;
  gltf["nodes"] = nodes;
  gltf["meshes"] = json::array({{{"name", d.name}, {"primitives", {{{"attributes", attrs}, {"indices", idx}, {"material", 0}}}}}});
  while (bin.data.size() % 4) bin.data.push_back(0);
  gltf["buffers"] = json::array({{{"byteLength", bin.data.size()}}});
  gltf["bufferViews"] = bin.views;
  gltf["accessors"] = bin.accessors;

  std::string js = gltf.dump();
  while (js.size() % 4) js.push_back(' ');
  std::error_code ec;
  if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    error = "không mở được " + path.string();
    return false;
  }
  auto w32 = [&](uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
  const auto total = static_cast<uint32_t>(12 + 8 + js.size() + 8 + bin.data.size());
  w32(0x46546C67);  // "glTF"
  w32(2);
  w32(total);
  w32(static_cast<uint32_t>(js.size()));
  w32(0x4E4F534A);  // "JSON"
  out.write(js.data(), static_cast<std::streamsize>(js.size()));
  w32(static_cast<uint32_t>(bin.data.size()));
  w32(0x004E4942);  // "BIN\0"
  out.write(reinterpret_cast<const char*>(bin.data.data()), static_cast<std::streamsize>(bin.data.size()));
  return out.good();
}

}  // namespace pb::asset
