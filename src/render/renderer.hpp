#pragma once
// Vulkan 1.3 renderer cho PAWBOUND 2.5D.
// Pass: shadow map (directional) → scene HDR (PBR metallic-roughness, cutaway) → tonemap (ACES) → swapchain sRGB.
// Dùng dynamic rendering + synchronization2 (core 1.3), bộ nhớ qua VMA.
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include "core/resource.hpp"
#include "geom/image.hpp"
#include "geom/mesh_data.hpp"
#include "render/overlay.hpp"

struct SDL_Window;

namespace pb::render {

struct GpuMesh;
using MeshHandle = Handle<GpuMesh>;

// Chỉ số texture trong mảng descriptor. Các texture mặc định luôn có sẵn:
inline constexpr uint32_t kTexWhite = 0;       // base color / ORM mặc định
inline constexpr uint32_t kTexFlatNormal = 1;  // normal map phẳng
inline constexpr uint32_t kMaxTextures = 48;   // khớp MAX_TEXTURES trong shaders/common.glsl

struct Material {
  glm::vec4 base_color{1.0f};
  float metallic = 0.0f;
  float roughness = 0.8f;
  float emissive = 0.0f;
  uint32_t base_tex = kTexWhite;
  uint32_t normal_tex = kTexFlatNormal;
  uint32_t orm_tex = kTexWhite;
  bool cutaway = true;  // bị làm mờ khi che nhân vật
  bool unlit = false;
};

struct DrawItem {
  MeshHandle mesh;
  glm::mat4 transform{1.0f};
  Material material;
  bool cast_shadow = true;
  // Mesh skinned: ma trận skinning (model-space, đã nhân inverse bind). Phải sống tới khi draw_frame trả về.
  const std::vector<glm::mat4>* joints = nullptr;
};

inline constexpr uint32_t kMaxJointMatricesPerFrame = 8192;
inline constexpr uint32_t kMaxOverlayVertices = 65536;

struct PointLight {
  glm::vec3 position{0.0f};
  float radius = 8.0f;
  glm::vec3 color{1.0f, 0.85f, 0.6f};
  float intensity = 20.0f;
};

struct SceneParams {
  glm::mat4 view{1.0f};
  glm::mat4 proj{1.0f};
  glm::vec3 camera_back_dir{0, 1, 0};
  glm::vec3 sun_dir{0.4f, 0.8f, 0.3f};  // hướng tới mặt trời
  glm::vec3 sun_color{1.0f, 0.96f, 0.9f};
  float sun_intensity = 3.0f;
  glm::vec3 sky_color{0.55f, 0.65f, 0.8f};
  glm::vec3 ground_color{0.25f, 0.22f, 0.2f};
  float ambient_intensity = 0.6f;
  glm::vec3 fog_color{0.7f, 0.75f, 0.8f};
  float fog_density = 0.0f;
  glm::vec3 focus{0.0f};       // vị trí nhân vật (cutaway + tâm shadow)
  float cutaway_radius = 0.0f; // 0 = tắt
  float shadow_radius = 30.0f; // bán kính vùng phủ shadow quanh focus
  float exposure = 1.0f;
  float vignette = 0.25f;
  float time = 0.0f;
  glm::vec4 clear_color{0.5f, 0.6f, 0.75f, 1.0f};
  std::vector<PointLight> point_lights;  // tối đa 8 (gần focus nhất)
};

struct RendererSettings {
  bool validation = false;
  bool vsync = true;
  std::string gpu_preference = "discrete";  // "discrete" | "integrated" | chuỗi con tên GPU
  uint32_t shadow_map_size = 2048;
};

struct RenderStats {
  uint32_t draw_calls = 0;
  uint32_t shadow_draw_calls = 0;
  uint32_t culled = 0;
  uint64_t triangles = 0;
  double cpu_ms = 0;
  double gpu_ms = 0;  // từ timestamp query (trễ 1–2 frame)
  uint32_t validation_errors = 0;
  uint32_t validation_warnings = 0;
};

class Renderer {
 public:
  static std::unique_ptr<Renderer> create(SDL_Window* window, const RendererSettings& settings, std::string& error);
  ~Renderer();
  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;

  MeshHandle upload_mesh(const geom::MeshData& mesh, std::string_view name);
  // Mesh có skin: 4 joint + 4 weight mỗi vertex (cùng số phần tử với mesh.vertices).
  MeshHandle upload_skinned_mesh(const geom::MeshData& mesh, std::span<const std::array<uint16_t, 4>> joints,
                                 std::span<const glm::vec4> weights, std::string_view name);
  // Trả chỉ số texture (dùng trong Material); kTexWhite nếu lỗi hoặc hết slot.
  uint32_t upload_texture(const geom::ImageRGBA8& image, bool srgb, std::string_view name);
  [[nodiscard]] const geom::Aabb* mesh_bounds(MeshHandle h) const;
  // Giải phóng mesh. Hủy thật sự bị hoãn tới khi các frame đang bay không còn dùng tới.
  void release_mesh(MeshHandle h);
  [[nodiscard]] size_t mesh_count() const;

  // Vẽ một frame. Trả false nếu bỏ qua (cửa sổ thu nhỏ / swapchain đang tạo lại).
  bool draw_frame(const SceneParams& scene, std::span<const DrawItem> items, const Overlay* overlay = nullptr);
  void notify_resized();
  // Chụp frame kế tiếp ra PNG (đường dẫn tương đối theo thư mục làm việc).
  void request_screenshot(const std::filesystem::path& path);
  [[nodiscard]] bool screenshot_pending() const;

  [[nodiscard]] const RenderStats& stats() const;
  [[nodiscard]] std::string device_name() const;
  [[nodiscard]] std::string api_version() const;
  void wait_idle();

  struct Impl;

 private:
  explicit Renderer(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

}  // namespace pb::render
