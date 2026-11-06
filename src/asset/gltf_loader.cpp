#include <cstring>
#include <format>
#include <functional>
#include <unordered_map>

#include <glm/geometric.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "asset/model.hpp"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace pb::asset {

glm::mat4 Model::node_world(int node) const {
  glm::mat4 m(1.0f);
  for (int n = node; n >= 0; n = nodes[static_cast<size_t>(n)].parent) m = nodes[static_cast<size_t>(n)].local.matrix() * m;
  return m;
}

size_t Model::triangle_count() const {
  size_t n = 0;
  for (const auto& m : meshes)
    for (const auto& p : m.primitives) n += p.mesh.triangle_count();
  return n;
}

const AnimationClip* Model::find_animation(const std::string& name) const {
  for (const auto& a : animations)
    if (a.name == name) return &a;
  return nullptr;
}

namespace {
const char* result_str(cgltf_result r) {
  switch (r) {
    case cgltf_result_data_too_short: return "dữ liệu bị cắt ngắn";
    case cgltf_result_unknown_format: return "không phải glTF/GLB";
    case cgltf_result_invalid_json: return "JSON không hợp lệ";
    case cgltf_result_invalid_gltf: return "glTF không hợp lệ";
    case cgltf_result_invalid_options: return "tùy chọn không hợp lệ";
    case cgltf_result_file_not_found: return "không tìm thấy file";
    case cgltf_result_io_error: return "lỗi I/O";
    case cgltf_result_out_of_memory: return "hết bộ nhớ";
    case cgltf_result_legacy_gltf: return "glTF 1.0 không được hỗ trợ";
    default: return "lỗi không xác định";
  }
}

struct DataGuard {
  cgltf_data* d = nullptr;
  ~DataGuard() {
    if (d) cgltf_free(d);
  }
};

Transform node_local(const cgltf_node& n) {
  Transform t;
  if (n.has_matrix) {
    // glTF: matrix không có shear; phân rã TRS.
    const glm::mat4 m = glm::make_mat4(n.matrix);
    t.t = glm::vec3(m[3]);
    t.s = {glm::length(glm::vec3(m[0])), glm::length(glm::vec3(m[1])), glm::length(glm::vec3(m[2]))};
    const glm::mat3 r{glm::vec3(m[0]) / t.s.x, glm::vec3(m[1]) / t.s.y, glm::vec3(m[2]) / t.s.z};
    t.r = glm::normalize(glm::quat_cast(r));
    return t;
  }
  if (n.has_translation) t.t = {n.translation[0], n.translation[1], n.translation[2]};
  if (n.has_rotation) t.r = glm::normalize(glm::quat(n.rotation[3], n.rotation[0], n.rotation[1], n.rotation[2]));
  if (n.has_scale) t.s = {n.scale[0], n.scale[1], n.scale[2]};
  return t;
}

}  // namespace

LoadResult load_gltf(const std::filesystem::path& path) {
  LoadResult res;
  Model& model = res.model;
  model.source = path;
  cgltf_options opts{};
  DataGuard g;
  const std::string spath = path.string();
  cgltf_result r = cgltf_parse_file(&opts, spath.c_str(), &g.d);
  if (r != cgltf_result_success) {
    res.error = std::format("parse {}: {}", spath, result_str(r));
    return res;
  }
  r = cgltf_load_buffers(&opts, g.d, spath.c_str());
  if (r != cgltf_result_success) {
    res.error = std::format("load buffers {}: {}", spath, result_str(r));
    return res;
  }
  r = cgltf_validate(g.d);
  if (r != cgltf_result_success) {
    res.error = std::format("validate {}: {}", spath, result_str(r));
    return res;
  }
  const cgltf_data& d = *g.d;

  // ---- images
  for (cgltf_size i = 0; i < d.images_count; ++i) {
    const cgltf_image& img = d.images[i];
    ModelImage mi;
    mi.name = img.name ? img.name : std::format("image{}", i);
    std::string err;
    if (img.buffer_view) {
      const auto* bytes = static_cast<const uint8_t*>(cgltf_buffer_view_data(img.buffer_view));
      mi.image = geom::decode_image({bytes, img.buffer_view->size}, err);
    } else if (img.uri && std::strncmp(img.uri, "data:", 5) != 0) {
      std::string uri = img.uri;
      cgltf_decode_uri(uri.data());
      uri.resize(std::strlen(uri.c_str()));
      mi.image = geom::load_image(path.parent_path() / uri, err);
    } else {
      err = "ảnh nhúng dạng data URI chưa được hỗ trợ (hãy dùng GLB)";
    }
    if (!mi.image.valid()) {
      res.error = std::format("ảnh '{}' lỗi: {}", mi.name, err);
      return res;
    }
    model.images.push_back(std::move(mi));
  }
  auto image_of = [&](const cgltf_texture_view& tv) -> int {
    if (!tv.texture || !tv.texture->image) return -1;
    return static_cast<int>(cgltf_image_index(&d, tv.texture->image));
  };

  // ---- materials
  for (cgltf_size i = 0; i < d.materials_count; ++i) {
    const cgltf_material& m = d.materials[i];
    ModelMaterial mm;
    mm.name = m.name ? m.name : std::format("material{}", i);
    if (m.has_pbr_metallic_roughness) {
      const auto& p = m.pbr_metallic_roughness;
      mm.base_color = {p.base_color_factor[0], p.base_color_factor[1], p.base_color_factor[2], p.base_color_factor[3]};
      mm.metallic = p.metallic_factor;
      mm.roughness = p.roughness_factor;
      mm.base_image = image_of(p.base_color_texture);
      mm.orm_image = image_of(p.metallic_roughness_texture);
    } else {
      model.warnings.push_back(std::format("material '{}' không dùng PBR metallic-roughness", mm.name));
    }
    mm.normal_image = image_of(m.normal_texture);
    mm.emissive = {m.emissive_factor[0], m.emissive_factor[1], m.emissive_factor[2]};
    mm.alpha_mask = m.alpha_mode == cgltf_alpha_mode_mask;
    mm.alpha_blend = m.alpha_mode == cgltf_alpha_mode_blend;
    mm.alpha_cutoff = m.alpha_cutoff;
    mm.double_sided = m.double_sided;
    if (mm.alpha_blend) model.warnings.push_back(std::format("material '{}': alpha blend chưa hỗ trợ, vẽ như mask", mm.name));
    if (mm.base_image >= 0) model.images[static_cast<size_t>(mm.base_image)].used_as_color = true;
    if (mm.normal_image >= 0) model.images[static_cast<size_t>(mm.normal_image)].used_as_data = true;
    if (mm.orm_image >= 0) model.images[static_cast<size_t>(mm.orm_image)].used_as_data = true;
    model.materials.push_back(mm);
  }

  // ---- meshes
  for (cgltf_size i = 0; i < d.meshes_count; ++i) {
    const cgltf_mesh& m = d.meshes[i];
    ModelMesh mm;
    mm.name = m.name ? m.name : std::format("mesh{}", i);
    for (cgltf_size p = 0; p < m.primitives_count; ++p) {
      const cgltf_primitive& prim = m.primitives[p];
      if (prim.type != cgltf_primitive_type_triangles) {
        model.warnings.push_back(std::format("mesh '{}' primitive {}: không phải triangle list, bỏ qua", mm.name, p));
        continue;
      }
      ModelPrimitive mp;
      const cgltf_accessor *pos = nullptr, *nrm = nullptr, *uv = nullptr, *tan = nullptr, *col = nullptr,
                           *jnt = nullptr, *wgt = nullptr;
      for (cgltf_size a = 0; a < prim.attributes_count; ++a) {
        const auto& at = prim.attributes[a];
        if (at.index != 0) continue;
        switch (at.type) {
          case cgltf_attribute_type_position: pos = at.data; break;
          case cgltf_attribute_type_normal: nrm = at.data; break;
          case cgltf_attribute_type_texcoord: uv = at.data; break;
          case cgltf_attribute_type_tangent: tan = at.data; break;
          case cgltf_attribute_type_color: col = at.data; break;
          case cgltf_attribute_type_joints: jnt = at.data; break;
          case cgltf_attribute_type_weights: wgt = at.data; break;
          default: break;
        }
      }
      if (!pos) {
        res.error = std::format("mesh '{}' primitive {} thiếu POSITION", mm.name, p);
        return res;
      }
      const cgltf_size n = pos->count;
      auto& verts = mp.mesh.vertices;
      verts.resize(n);
      for (cgltf_size v = 0; v < n; ++v) {
        float f[4] = {0, 0, 0, 1};
        cgltf_accessor_read_float(pos, v, f, 3);
        verts[v].position = {f[0], f[1], f[2]};
        if (nrm) {
          cgltf_accessor_read_float(nrm, v, f, 3);
          verts[v].normal = {f[0], f[1], f[2]};
        }
        if (uv) {
          cgltf_accessor_read_float(uv, v, f, 2);
          verts[v].uv = {f[0], f[1]};
        }
        if (tan) {
          cgltf_accessor_read_float(tan, v, f, 4);
          verts[v].tangent = {f[0], f[1], f[2], f[3]};
        }
        if (col) {
          float c[4] = {1, 1, 1, 1};
          cgltf_accessor_read_float(col, v, c, cgltf_num_components(col->type));
          verts[v].color = {c[0], c[1], c[2], c[3]};
        }
      }
      if (prim.indices) {
        mp.mesh.indices.resize(prim.indices->count);
        for (cgltf_size k = 0; k < prim.indices->count; ++k)
          mp.mesh.indices[k] = static_cast<uint32_t>(cgltf_accessor_read_index(prim.indices, k));
      } else {
        mp.mesh.indices.resize(n);
        for (cgltf_size k = 0; k < n; ++k) mp.mesh.indices[k] = static_cast<uint32_t>(k);
      }
      if (!nrm) {  // sinh normal phẳng theo tam giác (tích lũy)
        for (auto& v : verts) v.normal = glm::vec3(0.0f);
        for (size_t k = 0; k + 2 < mp.mesh.indices.size(); k += 3) {
          auto &a = verts[mp.mesh.indices[k]], &b = verts[mp.mesh.indices[k + 1]], &c = verts[mp.mesh.indices[k + 2]];
          const glm::vec3 fn = glm::cross(b.position - a.position, c.position - a.position);
          a.normal += fn;
          b.normal += fn;
          c.normal += fn;
        }
        for (auto& v : verts) v.normal = glm::length(v.normal) > 0 ? glm::normalize(v.normal) : glm::vec3(0, 1, 0);
        model.warnings.push_back(std::format("mesh '{}': thiếu NORMAL, đã sinh tự động", mm.name));
      }
      if (!tan) mp.mesh.generate_tangents();
      if (jnt && wgt) {
        mp.joints.resize(n);
        mp.weights.resize(n);
        for (cgltf_size v = 0; v < n; ++v) {
          cgltf_uint j[4] = {0, 0, 0, 0};
          cgltf_accessor_read_uint(jnt, v, j, 4);
          float w[4] = {0, 0, 0, 0};
          cgltf_accessor_read_float(wgt, v, w, 4);
          mp.joints[v] = {static_cast<uint16_t>(j[0]), static_cast<uint16_t>(j[1]), static_cast<uint16_t>(j[2]),
                          static_cast<uint16_t>(j[3])};
          glm::vec4 wv(w[0], w[1], w[2], w[3]);
          const float sum = wv.x + wv.y + wv.z + wv.w;
          mp.weights[v] = sum > 0 ? wv / sum : glm::vec4(1, 0, 0, 0);
        }
      }
      mp.material = prim.material ? static_cast<int>(cgltf_material_index(&d, prim.material)) : -1;
      mp.mesh.recompute_bounds();
      if (auto err = geom::validate_mesh(mp.mesh); !err.empty()) {
        res.error = std::format("mesh '{}' primitive {}: {}", mm.name, p, err);
        return res;
      }
      mm.primitives.push_back(std::move(mp));
    }
    model.meshes.push_back(std::move(mm));
  }

  // ---- nodes: sắp xếp parent trước child (DFS từ các root)
  std::vector<int> order;
  std::vector<int> remap(d.nodes_count, -1);
  std::function<void(const cgltf_node*)> visit = [&](const cgltf_node* n) {
    const auto idx = static_cast<int>(cgltf_node_index(&d, n));
    if (remap[static_cast<size_t>(idx)] >= 0) return;
    remap[static_cast<size_t>(idx)] = static_cast<int>(order.size());
    order.push_back(idx);
    for (cgltf_size c = 0; c < n->children_count; ++c) visit(n->children[c]);
  };
  for (cgltf_size i = 0; i < d.nodes_count; ++i)
    if (!d.nodes[i].parent) visit(&d.nodes[i]);
  for (int src : order) {
    const cgltf_node& n = d.nodes[static_cast<size_t>(src)];
    ModelNode mn;
    mn.name = n.name ? n.name : std::format("node{}", src);
    mn.parent = n.parent ? remap[cgltf_node_index(&d, n.parent)] : -1;
    mn.local = node_local(n);
    mn.mesh = n.mesh ? static_cast<int>(cgltf_mesh_index(&d, n.mesh)) : -1;
    mn.skin = n.skin ? static_cast<int>(cgltf_skin_index(&d, n.skin)) : -1;
    model.nodes.push_back(mn);
  }

  // ---- skins -> Skeleton (joint sắp theo thứ tự node đã sắp xếp)
  std::vector<std::unordered_map<int, int>> node_to_joint(d.skins_count);
  for (cgltf_size s = 0; s < d.skins_count; ++s) {
    const cgltf_skin& sk = d.skins[s];
    // joints theo thứ tự glTF (vertex JOINTS_0 tham chiếu thứ tự này) -> giữ nguyên thứ tự,
    // nhưng kiểm tra parent đứng trước; nếu không, báo lỗi rõ ràng.
    Skeleton skel;
    std::unordered_map<int, int> joint_of;  // model node index -> joint index
    for (cgltf_size j = 0; j < sk.joints_count; ++j)
      joint_of[remap[cgltf_node_index(&d, sk.joints[j])]] = static_cast<int>(j);
    for (cgltf_size j = 0; j < sk.joints_count; ++j) {
      const int node = remap[cgltf_node_index(&d, sk.joints[j])];
      const auto& mn = model.nodes[static_cast<size_t>(node)];
      skel.names.push_back(mn.name);
      skel.bind_local.push_back(mn.local);
      int parent = -1;
      for (int p = mn.parent; p >= 0; p = model.nodes[static_cast<size_t>(p)].parent) {
        if (auto it = joint_of.find(p); it != joint_of.end()) {
          parent = it->second;
          break;
        }
      }
      skel.parents.push_back(parent);
      glm::mat4 ib(1.0f);
      if (sk.inverse_bind_matrices) cgltf_accessor_read_float(sk.inverse_bind_matrices, j, glm::value_ptr(ib), 16);
      skel.inverse_bind.push_back(ib);
      if (parent < 0 && mn.parent >= 0) skel.root_transform = model.node_world(mn.parent);
    }
    skel.names.shrink_to_fit();
    if (auto err = validate_skeleton(skel); !err.empty()) {
      res.error = std::format("skin {}: {}", s, err);
      return res;
    }
    node_to_joint[s] = joint_of;
    model.skins.push_back(std::move(skel));
  }

  // ---- animations (gắn với skin 0)
  for (cgltf_size a = 0; a < d.animations_count; ++a) {
    const cgltf_animation& an = d.animations[a];
    AnimationClip clip;
    clip.name = an.name ? an.name : std::format("anim{}", a);
    for (cgltf_size c = 0; c < an.channels_count; ++c) {
      const auto& ch = an.channels[c];
      if (!ch.target_node || !ch.sampler) continue;
      if (ch.target_path == cgltf_animation_path_type_weights) {
        model.warnings.push_back(std::format("animation '{}': morph weights chưa hỗ trợ", clip.name));
        continue;
      }
      const int node = remap[cgltf_node_index(&d, ch.target_node)];
      if (node_to_joint.empty() || !node_to_joint[0].contains(node)) continue;
      Track tr;
      tr.joint = node_to_joint[0][node];
      tr.path = ch.target_path == cgltf_animation_path_type_translation ? Path::Translation
                : ch.target_path == cgltf_animation_path_type_rotation  ? Path::Rotation
                                                                        : Path::Scale;
      tr.interp = ch.sampler->interpolation == cgltf_interpolation_type_step           ? Interp::Step
                  : ch.sampler->interpolation == cgltf_interpolation_type_cubic_spline ? Interp::CubicSpline
                                                                                       : Interp::Linear;
      const cgltf_accessor* in = ch.sampler->input;
      const cgltf_accessor* out = ch.sampler->output;
      tr.times.resize(in->count);
      cgltf_accessor_unpack_floats(in, tr.times.data(), in->count);
      const int comps = tr.path == Path::Rotation ? 4 : 3;
      tr.values.resize(out->count);
      for (cgltf_size k = 0; k < out->count; ++k) {
        float f[4] = {0, 0, 0, 0};
        cgltf_accessor_read_float(out, k, f, static_cast<cgltf_size>(comps));
        tr.values[k] = {f[0], f[1], f[2], f[3]};
      }
      const size_t expected = tr.times.size() * (tr.interp == Interp::CubicSpline ? 3 : 1);
      if (tr.values.size() != expected) {
        res.error = std::format("animation '{}' kênh {}: số giá trị {} != {}", clip.name, c, tr.values.size(), expected);
        return res;
      }
      if (!tr.times.empty()) clip.duration = std::max(clip.duration, tr.times.back());
      clip.tracks.push_back(std::move(tr));
    }
    model.animations.push_back(std::move(clip));
  }

  // ---- bounds (bind pose)
  for (size_t i = 0; i < model.nodes.size(); ++i) {
    const auto& n = model.nodes[i];
    if (n.mesh < 0) continue;
    // Mesh skinned: vertex ở không gian bind của skeleton (glTF bỏ qua transform của node mesh).
    const glm::mat4 w = n.skin >= 0 ? glm::mat4(1.0f) : model.node_world(static_cast<int>(i));
    for (const auto& p : model.meshes[static_cast<size_t>(n.mesh)].primitives) model.bounds.expand(p.mesh.bounds.transformed(w));
  }
  return res;
}

}  // namespace pb::asset
