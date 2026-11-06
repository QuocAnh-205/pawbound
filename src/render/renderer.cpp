#include "render/renderer.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>
#include <functional>
#include <stdexcept>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec4.hpp>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include "core/log.hpp"
#include "geom/camera.hpp"

namespace pb::render {
namespace {

// SPIR-V nhúng lúc build (glslc -mfmt=c), không biên dịch shader lúc chạy.
const uint32_t kMeshVert[] =
#include "mesh.vert.inc"
    ;
const uint32_t kMeshFrag[] =
#include "mesh.frag.inc"
    ;
const uint32_t kShadowVert[] =
#include "shadow.vert.inc"
    ;
const uint32_t kTonemapVert[] =
#include "tonemap.vert.inc"
    ;
const uint32_t kTonemapFrag[] =
#include "tonemap.frag.inc"
    ;
const uint32_t kOverlayVert[] =
#include "overlay.vert.inc"
    ;
const uint32_t kOverlayFrag[] =
#include "overlay.frag.inc"
    ;
const uint32_t kMeshSkinnedVert[] =
#include "mesh_skinned.vert.inc"
    ;
const uint32_t kShadowSkinnedVert[] =
#include "shadow_skinned.vert.inc"
    ;

struct SkinVertex {
  uint16_t joints[4];
  float weights[4];
};
static_assert(sizeof(SkinVertex) == 24);

constexpr uint32_t kFramesInFlight = 2;
constexpr VkFormat kHdrFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;
constexpr uint32_t kMaxPointLights = 8;

const char* vk_str(VkResult r) {
  switch (r) {
    case VK_SUCCESS: return "VK_SUCCESS";
    case VK_NOT_READY: return "VK_NOT_READY";
    case VK_TIMEOUT: return "VK_TIMEOUT";
    case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
    default: return "VkResult(khác)";
  }
}

#define VK_CHECK(expr)                                                                                   \
  do {                                                                                                   \
    const VkResult r_ = (expr);                                                                          \
    if (r_ != VK_SUCCESS) throw std::runtime_error(std::format("{} -> {} (dòng {})", #expr, vk_str(r_), __LINE__)); \
  } while (0)

struct FrameUniforms {
  glm::mat4 view_proj;
  glm::mat4 light_view_proj;
  glm::vec4 camera_back_dir, sun_dir, sun_color, sky_color, ground_color, fog_color, focus, params;
  glm::vec4 point_pos[kMaxPointLights];
  glm::vec4 point_color[kMaxPointLights];
};
static_assert(sizeof(FrameUniforms) == 128 + 8 * 16 + 2 * kMaxPointLights * 16, "khớp std140 trong common.glsl");

struct DrawPush {
  glm::mat4 model;
  glm::vec4 base_color;
  glm::vec4 mat_params;
  glm::uvec4 tex;
};
static_assert(sizeof(DrawPush) == 112, "push constant ≤ 128 byte");

struct Buffer {
  VkBuffer buffer = VK_NULL_HANDLE;
  VmaAllocation alloc = nullptr;
  void* mapped = nullptr;
  VkDeviceSize size = 0;
};

struct Image {
  VkImage image = VK_NULL_HANDLE;
  VmaAllocation alloc = nullptr;
  VkImageView view = VK_NULL_HANDLE;
  VkFormat format = VK_FORMAT_UNDEFINED;
  VkExtent2D extent{};
  uint32_t mips = 1;
};

VkImageSubresourceRange range(VkImageAspectFlags aspect, uint32_t mips = 1, uint32_t base_mip = 0) {
  return VkImageSubresourceRange{aspect, base_mip, mips, 0, 1};
}

void image_barrier(VkCommandBuffer cmd, VkImage img, VkImageSubresourceRange rng, VkImageLayout from,
                   VkImageLayout to, VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                   VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access) {
  VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
  b.srcStageMask = src_stage;
  b.srcAccessMask = src_access;
  b.dstStageMask = dst_stage;
  b.dstAccessMask = dst_access;
  b.oldLayout = from;
  b.newLayout = to;
  b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.image = img;
  b.subresourceRange = rng;
  VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
  dep.imageMemoryBarrierCount = 1;
  dep.pImageMemoryBarriers = &b;
  vkCmdPipelineBarrier2(cmd, &dep);
}

}  // namespace

struct GpuMesh {
  Buffer vb, ib;
  Buffer sb;  // SkinVertex (chỉ mesh skinned)
  bool skinned = false;
  uint32_t index_count = 0;
  geom::Aabb bounds;
};

struct Renderer::Impl {
  RendererSettings settings;
  SDL_Window* window = nullptr;

  VkInstance instance = VK_NULL_HANDLE;
  VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  VkPhysicalDevice gpu = VK_NULL_HANDLE;
  VkPhysicalDeviceProperties props{};
  VkDevice device = VK_NULL_HANDLE;
  uint32_t queue_family = 0;
  VkQueue queue = VK_NULL_HANDLE;
  VmaAllocator allocator = nullptr;
  bool anisotropy = false;

  VkSwapchainKHR swapchain = VK_NULL_HANDLE;
  VkFormat swap_format = VK_FORMAT_UNDEFINED;
  VkExtent2D extent{};
  std::vector<VkImage> swap_images;
  std::vector<VkImageView> swap_views;
  std::vector<VkSemaphore> render_done;  // một semaphore cho mỗi swapchain image
  bool swap_dirty = false;

  Image hdr, depth, shadow;
  VkSampler tex_sampler = VK_NULL_HANDLE, shadow_sampler = VK_NULL_HANDLE, hdr_sampler = VK_NULL_HANDLE;
  VkDescriptorSetLayout scene_dsl = VK_NULL_HANDLE, tonemap_dsl = VK_NULL_HANDLE;
  VkDescriptorPool desc_pool = VK_NULL_HANDLE;
  VkPipelineLayout mesh_layout = VK_NULL_HANDLE, tonemap_layout = VK_NULL_HANDLE;
  VkPipeline mesh_pipeline = VK_NULL_HANDLE, shadow_pipeline = VK_NULL_HANDLE, tonemap_pipeline = VK_NULL_HANDLE;
  VkPipeline skinned_pipeline = VK_NULL_HANDLE, shadow_skinned_pipeline = VK_NULL_HANDLE;
  VkPipeline overlay_pipeline = VK_NULL_HANDLE;
  VkPipelineLayout overlay_layout = VK_NULL_HANDLE;

  struct Frame {
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkSemaphore acquired = VK_NULL_HANDLE;
    Buffer ubo;
    Buffer joints;  // SSBO ma trận skinning
    Buffer overlay;  // đỉnh HUD
    VkDescriptorSet scene_set = VK_NULL_HANDLE, tonemap_set = VK_NULL_HANDLE;
    bool timestamps_written = false;
  };
  std::array<Frame, kFramesInFlight> frames;
  uint32_t frame_index = 0;
  VkCommandPool cmd_pool = VK_NULL_HANDLE;
  VkQueryPool query_pool = VK_NULL_HANDLE;
  bool timestamps_supported = false;

  ResourceRegistry<GpuMesh> meshes;
  std::vector<MeshHandle> mesh_list;
  struct PendingDelete {
    GpuMesh mesh;
    uint64_t safe_after;
  };
  std::vector<PendingDelete> pending_deletes;
  uint64_t frame_counter = 0;

  void collect_garbage(bool force) {
    for (auto it = pending_deletes.begin(); it != pending_deletes.end();) {
      if (force || it->safe_after <= frame_counter) {
        destroy(it->mesh.vb);
        destroy(it->mesh.ib);
        destroy(it->mesh.sb);
        it = pending_deletes.erase(it);
      } else {
        ++it;
      }
    }
  }
  std::vector<Image> textures;

  RenderStats stats;
  std::filesystem::path screenshot_path;
  bool screenshot_requested = false;

  // ---------------------------------------------------------------- khởi tạo
  static VKAPI_ATTR VkBool32 VKAPI_CALL debug_cb(VkDebugUtilsMessageSeverityFlagBitsEXT sev,
                                                 VkDebugUtilsMessageTypeFlagsEXT,
                                                 const VkDebugUtilsMessengerCallbackDataEXT* data, void* user) {
    auto* self = static_cast<Impl*>(user);
    if (sev & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
      ++self->stats.validation_errors;
      log::error("vulkan", "{}", data->pMessage);
    } else if (sev & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
      ++self->stats.validation_warnings;
      log::warn("vulkan", "{}", data->pMessage);
    }
    return VK_FALSE;
  }

  void create_instance() {
    uint32_t sdl_count = 0;
    const char* const* sdl_exts = SDL_Vulkan_GetInstanceExtensions(&sdl_count);
    if (!sdl_exts) throw std::runtime_error(std::string("SDL_Vulkan_GetInstanceExtensions: ") + SDL_GetError());
    std::vector<const char*> exts(sdl_exts, sdl_exts + sdl_count);
    std::vector<const char*> layers;
    if (settings.validation) {
      uint32_t n = 0;
      vkEnumerateInstanceLayerProperties(&n, nullptr);
      std::vector<VkLayerProperties> avail(n);
      vkEnumerateInstanceLayerProperties(&n, avail.data());
      const bool has = std::any_of(avail.begin(), avail.end(), [](const VkLayerProperties& l) {
        return std::strcmp(l.layerName, "VK_LAYER_KHRONOS_validation") == 0;
      });
      if (has) {
        layers.push_back("VK_LAYER_KHRONOS_validation");
        exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
      } else {
        log::warn("render", "không có VK_LAYER_KHRONOS_validation — chạy không validation");
        settings.validation = false;
      }
    }
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "PAWBOUND";
    app.pEngineName = "pawbound";
    app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = static_cast<uint32_t>(exts.size());
    ci.ppEnabledExtensionNames = exts.data();
    ci.enabledLayerCount = static_cast<uint32_t>(layers.size());
    ci.ppEnabledLayerNames = layers.data();
    VK_CHECK(vkCreateInstance(&ci, nullptr, &instance));

    if (settings.validation) {
      VkDebugUtilsMessengerCreateInfoEXT mi{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
      mi.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
      mi.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
      mi.pfnUserCallback = &Impl::debug_cb;
      mi.pUserData = this;
      auto fn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
          vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
      if (fn) VK_CHECK(fn(instance, &mi, nullptr, &messenger));
    }
    if (!SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface))
      throw std::runtime_error(std::string("SDL_Vulkan_CreateSurface: ") + SDL_GetError());
  }

  void pick_gpu() {
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(instance, &n, nullptr);
    std::vector<VkPhysicalDevice> devs(n);
    vkEnumeratePhysicalDevices(instance, &n, devs.data());
    std::string pref = settings.gpu_preference;
    if (const char* env = std::getenv("PB_GPU")) pref = env;
    int best_score = -1;
    for (auto d : devs) {
      VkPhysicalDeviceProperties p;
      vkGetPhysicalDeviceProperties(d, &p);
      if (p.apiVersion < VK_API_VERSION_1_3) continue;
      VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
      VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
      f2.pNext = &f13;
      vkGetPhysicalDeviceFeatures2(d, &f2);
      if (!f13.dynamicRendering || !f13.synchronization2 || !f13.shaderDemoteToHelperInvocation) continue;
      if (p.limits.maxPerStageDescriptorSamplers < kMaxTextures + 1 ||
          p.limits.maxPerStageDescriptorSampledImages < kMaxTextures + 1)
        continue;  // mảng texture + shadow map phải vừa giới hạn mỗi stage
      uint32_t qn = 0;
      vkGetPhysicalDeviceQueueFamilyProperties(d, &qn, nullptr);
      std::vector<VkQueueFamilyProperties> qs(qn);
      vkGetPhysicalDeviceQueueFamilyProperties(d, &qn, qs.data());
      int family = -1;
      for (uint32_t i = 0; i < qn; ++i) {
        VkBool32 present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(d, i, surface, &present);
        if ((qs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
          family = static_cast<int>(i);
          break;
        }
      }
      if (family < 0) continue;
      int score = 1;
      const bool discrete = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
      const bool integrated = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
      if (pref == "discrete" && discrete) score += 100;
      else if (pref == "integrated" && integrated) score += 100;
      else if (!pref.empty() && std::string(p.deviceName).find(pref) != std::string::npos) score += 200;
      if (discrete) score += 10;
      if (score > best_score) {
        best_score = score;
        gpu = d;
        props = p;
        queue_family = static_cast<uint32_t>(family);
        anisotropy = f2.features.samplerAnisotropy == VK_TRUE;
        timestamps_supported = qs[static_cast<size_t>(family)].timestampValidBits > 0 &&
                               p.limits.timestampComputeAndGraphics;
      }
    }
    if (!gpu) throw std::runtime_error("không tìm thấy GPU hỗ trợ Vulkan 1.3 + dynamic rendering + sync2 + present");
    log::info("render", "GPU: {} (Vulkan {}.{}.{}, driver {:#x})", props.deviceName,
              VK_API_VERSION_MAJOR(props.apiVersion), VK_API_VERSION_MINOR(props.apiVersion),
              VK_API_VERSION_PATCH(props.apiVersion), props.driverVersion);
  }

  void create_device() {
    const float prio = 1.0f;
    VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qi.queueFamilyIndex = queue_family;
    qi.queueCount = 1;
    qi.pQueuePriorities = &prio;
    VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    f13.dynamicRendering = VK_TRUE;
    f13.synchronization2 = VK_TRUE;
    f13.shaderDemoteToHelperInvocation = VK_TRUE;  // glslc (vulkan1.3) dịch `discard` thành demote; bắt buộc có ở 1.3
    VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    f2.pNext = &f13;
    f2.features.samplerAnisotropy = anisotropy ? VK_TRUE : VK_FALSE;
    const char* exts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo ci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    ci.pNext = &f2;
    ci.queueCreateInfoCount = 1;
    ci.pQueueCreateInfos = &qi;
    ci.enabledExtensionCount = 1;
    ci.ppEnabledExtensionNames = exts;
    VK_CHECK(vkCreateDevice(gpu, &ci, nullptr, &device));
    vkGetDeviceQueue(device, queue_family, 0, &queue);

    VmaAllocatorCreateInfo ai{};
    ai.vulkanApiVersion = VK_API_VERSION_1_3;
    ai.physicalDevice = gpu;
    ai.device = device;
    ai.instance = instance;
    VK_CHECK(vmaCreateAllocator(&ai, &allocator));

    VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pi.queueFamilyIndex = queue_family;
    VK_CHECK(vkCreateCommandPool(device, &pi, nullptr, &cmd_pool));

    if (timestamps_supported) {
      VkQueryPoolCreateInfo qp{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
      qp.queryType = VK_QUERY_TYPE_TIMESTAMP;
      qp.queryCount = 2 * kFramesInFlight;
      VK_CHECK(vkCreateQueryPool(device, &qp, nullptr, &query_pool));
    }
  }

  // ---------------------------------------------------------------- tài nguyên
  Buffer create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, bool host_write, bool host_read = false) {
    Buffer b;
    b.size = size;
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = size;
    bi.usage = usage;
    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_AUTO;
    if (host_write) ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    if (host_read) ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo info{};
    VK_CHECK(vmaCreateBuffer(allocator, &bi, &ai, &b.buffer, &b.alloc, &info));
    b.mapped = info.pMappedData;
    return b;
  }
  void destroy(Buffer& b) {
    if (b.buffer) vmaDestroyBuffer(allocator, b.buffer, b.alloc);
    b = {};
  }

  Image create_image(VkExtent2D ext, VkFormat fmt, VkImageUsageFlags usage, VkImageAspectFlags aspect,
                     uint32_t mips = 1) {
    Image img;
    img.format = fmt;
    img.extent = ext;
    img.mips = mips;
    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = fmt;
    ii.extent = {ext.width, ext.height, 1};
    ii.mipLevels = mips;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = usage;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_AUTO;
    VK_CHECK(vmaCreateImage(allocator, &ii, &ai, &img.image, &img.alloc, nullptr));
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = img.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = fmt;
    vi.subresourceRange = range(aspect, mips);
    VK_CHECK(vkCreateImageView(device, &vi, nullptr, &img.view));
    return img;
  }
  void destroy(Image& img) {
    if (img.view) vkDestroyImageView(device, img.view, nullptr);
    if (img.image) vmaDestroyImage(allocator, img.image, img.alloc);
    img = {};
  }

  void immediate(const std::function<void(VkCommandBuffer)>& fn) {
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = cmd_pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    VK_CHECK(vkAllocateCommandBuffers(device, &ai, &cmd));
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &bi));
    fn(cmd);
    VK_CHECK(vkEndCommandBuffer(cmd));
    VkCommandBufferSubmitInfo cbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cbi.commandBuffer = cmd;
    VkSubmitInfo2 si{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    si.commandBufferInfoCount = 1;
    si.pCommandBufferInfos = &cbi;
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence;
    VK_CHECK(vkCreateFence(device, &fi, nullptr, &fence));
    VK_CHECK(vkQueueSubmit2(queue, 1, &si, fence));
    VK_CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));
    vkDestroyFence(device, fence, nullptr);
    vkFreeCommandBuffers(device, cmd_pool, 1, &cmd);
  }

  // ---------------------------------------------------------------- swapchain
  void create_swapchain() {
    VkSurfaceCapabilitiesKHR caps;
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &caps));
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    if (caps.currentExtent.width != UINT32_MAX) extent = caps.currentExtent;
    else extent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h)};
    extent.width = std::clamp(extent.width, caps.minImageExtent.width, caps.maxImageExtent.width);
    extent.height = std::clamp(extent.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    if (extent.width == 0 || extent.height == 0) return;  // thu nhỏ

    uint32_t fn = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &fn, nullptr);
    std::vector<VkSurfaceFormatKHR> fmts(fn);
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &fn, fmts.data());
    VkSurfaceFormatKHR chosen = fmts[0];
    for (const auto& f : fmts) {
      if ((f.format == VK_FORMAT_B8G8R8A8_SRGB || f.format == VK_FORMAT_R8G8B8A8_SRGB) &&
          f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        chosen = f;
        break;
      }
    }
    swap_format = chosen.format;

    uint32_t pn = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &pn, nullptr);
    std::vector<VkPresentModeKHR> modes(pn);
    vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &pn, modes.data());
    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
    if (!settings.vsync) {
      for (auto m : modes)
        if (m == VK_PRESENT_MODE_MAILBOX_KHR) mode = m;
      if (mode == VK_PRESENT_MODE_FIFO_KHR)
        for (auto m : modes)
          if (m == VK_PRESENT_MODE_IMMEDIATE_KHR) mode = m;
    }

    uint32_t count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0) count = std::min(count, caps.maxImageCount);
    VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    ci.surface = surface;
    ci.minImageCount = count;
    ci.imageFormat = chosen.format;
    ci.imageColorSpace = chosen.colorSpace;
    ci.imageExtent = extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                    (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode = mode;
    ci.clipped = VK_TRUE;
    ci.oldSwapchain = swapchain;
    VkSwapchainKHR new_swap;
    VK_CHECK(vkCreateSwapchainKHR(device, &ci, nullptr, &new_swap));
    destroy_swapchain_views();
    if (swapchain) vkDestroySwapchainKHR(device, swapchain, nullptr);
    swapchain = new_swap;

    uint32_t n = 0;
    vkGetSwapchainImagesKHR(device, swapchain, &n, nullptr);
    swap_images.resize(n);
    vkGetSwapchainImagesKHR(device, swapchain, &n, swap_images.data());
    for (auto img : swap_images) {
      VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
      vi.image = img;
      vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
      vi.format = swap_format;
      vi.subresourceRange = range(VK_IMAGE_ASPECT_COLOR_BIT);
      VkImageView v;
      VK_CHECK(vkCreateImageView(device, &vi, nullptr, &v));
      swap_views.push_back(v);
      VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
      VkSemaphore s;
      VK_CHECK(vkCreateSemaphore(device, &si, nullptr, &s));
      render_done.push_back(s);
    }
    hdr = create_image(extent, kHdrFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                       VK_IMAGE_ASPECT_COLOR_BIT);
    depth = create_image(extent, kDepthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
    for (auto& f : frames) write_tonemap_set(f.tonemap_set);
    swap_dirty = false;
    log::info("render", "swapchain {}x{}, {} ảnh, present mode {}", extent.width, extent.height, n,
              static_cast<int>(mode));
  }

  void destroy_swapchain_views() {
    for (auto v : swap_views) vkDestroyImageView(device, v, nullptr);
    for (auto s : render_done) vkDestroySemaphore(device, s, nullptr);
    swap_views.clear();
    render_done.clear();
    swap_images.clear();
    destroy(hdr);
    destroy(depth);
  }

  bool recreate_swapchain() {
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    if (w == 0 || h == 0) return false;
    vkDeviceWaitIdle(device);
    create_swapchain();
    return swapchain != VK_NULL_HANDLE && !swap_views.empty();
  }

  // ---------------------------------------------------------------- descriptor & pipeline
  void create_samplers_and_layouts() {
    VkSamplerCreateInfo s{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    s.magFilter = s.minFilter = VK_FILTER_LINEAR;
    s.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    s.addressModeU = s.addressModeV = s.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    s.maxLod = VK_LOD_CLAMP_NONE;
    s.anisotropyEnable = anisotropy ? VK_TRUE : VK_FALSE;
    s.maxAnisotropy = anisotropy ? std::min(8.0f, props.limits.maxSamplerAnisotropy) : 1.0f;
    VK_CHECK(vkCreateSampler(device, &s, nullptr, &tex_sampler));

    VkSamplerCreateInfo sh{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sh.magFilter = sh.minFilter = VK_FILTER_LINEAR;
    sh.addressModeU = sh.addressModeV = sh.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sh.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    sh.compareEnable = VK_TRUE;
    sh.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    VK_CHECK(vkCreateSampler(device, &sh, nullptr, &shadow_sampler));

    VkSamplerCreateInfo hs{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    hs.magFilter = hs.minFilter = VK_FILTER_LINEAR;
    hs.addressModeU = hs.addressModeV = hs.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VK_CHECK(vkCreateSampler(device, &hs, nullptr, &hdr_sampler));

    VkDescriptorSetLayoutBinding b[4]{};
    b[0] = {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    b[1] = {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    b[2] = {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxTextures, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    b[3] = {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    li.bindingCount = 4;
    li.pBindings = b;
    VK_CHECK(vkCreateDescriptorSetLayout(device, &li, nullptr, &scene_dsl));
    VkDescriptorSetLayoutBinding tb{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
                                    nullptr};
    li.bindingCount = 1;
    li.pBindings = &tb;
    VK_CHECK(vkCreateDescriptorSetLayout(device, &li, nullptr, &tonemap_dsl));

    VkDescriptorPoolSize sizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kFramesInFlight},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kFramesInFlight},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kFramesInFlight * (2 + kMaxTextures)},
    };
    VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pi.maxSets = kFramesInFlight * 2;
    pi.poolSizeCount = 3;
    pi.pPoolSizes = sizes;
    VK_CHECK(vkCreateDescriptorPool(device, &pi, nullptr, &desc_pool));

    for (auto& f : frames) {
      VkDescriptorSetLayout layouts[] = {scene_dsl, tonemap_dsl};
      VkDescriptorSet sets[2];
      VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
      ai.descriptorPool = desc_pool;
      ai.descriptorSetCount = 2;
      ai.pSetLayouts = layouts;
      VK_CHECK(vkAllocateDescriptorSets(device, &ai, sets));
      f.scene_set = sets[0];
      f.tonemap_set = sets[1];
      f.ubo = create_buffer(sizeof(FrameUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true);
      f.joints = create_buffer(sizeof(glm::mat4) * kMaxJointMatricesPerFrame, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
      f.overlay = create_buffer(sizeof(OverlayVertex) * kMaxOverlayVertices, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true);

      VkCommandBufferAllocateInfo ci{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
      ci.commandPool = cmd_pool;
      ci.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
      ci.commandBufferCount = 1;
      VK_CHECK(vkAllocateCommandBuffers(device, &ci, &f.cmd));
      VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
      fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
      VK_CHECK(vkCreateFence(device, &fi, nullptr, &f.fence));
      VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
      VK_CHECK(vkCreateSemaphore(device, &si, nullptr, &f.acquired));
    }

    VkPushConstantRange pc{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(DrawPush)};
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &scene_dsl;
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges = &pc;
    VK_CHECK(vkCreatePipelineLayout(device, &pl, nullptr, &mesh_layout));
    VkPushConstantRange opc{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::vec2)};
    VkPipelineLayoutCreateInfo ol{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    ol.pushConstantRangeCount = 1;
    ol.pPushConstantRanges = &opc;
    VK_CHECK(vkCreatePipelineLayout(device, &ol, nullptr, &overlay_layout));
    VkPushConstantRange tpc{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::vec4)};
    pl.pSetLayouts = &tonemap_dsl;
    pl.pPushConstantRanges = &tpc;
    VK_CHECK(vkCreatePipelineLayout(device, &pl, nullptr, &tonemap_layout));
  }

  void write_scene_set(VkDescriptorSet set, const Buffer& ubo, const Buffer& joints) {
    VkDescriptorBufferInfo bi{ubo.buffer, 0, sizeof(FrameUniforms)};
    VkDescriptorBufferInfo ji{joints.buffer, 0, VK_WHOLE_SIZE};
    VkDescriptorImageInfo si{shadow_sampler, shadow.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    std::array<VkDescriptorImageInfo, kMaxTextures> ti{};
    for (uint32_t i = 0; i < kMaxTextures; ++i) {
      const Image& img = i < textures.size() ? textures[i] : textures[kTexWhite];
      ti[i] = {tex_sampler, img.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    }
    VkWriteDescriptorSet w[4]{};
    for (auto& x : w) {
      x.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      x.dstSet = set;
      x.descriptorCount = 1;
      x.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    }
    w[0].dstBinding = 0;
    w[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w[0].pBufferInfo = &bi;
    w[1].dstBinding = 1;
    w[1].pImageInfo = &si;
    w[2].dstBinding = 2;
    w[2].descriptorCount = kMaxTextures;
    w[2].pImageInfo = ti.data();
    w[3].dstBinding = 3;
    w[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w[3].pBufferInfo = &ji;
    vkUpdateDescriptorSets(device, 4, w, 0, nullptr);
  }

  void write_tonemap_set(VkDescriptorSet set) {
    if (!set || !hdr.view) return;
    VkDescriptorImageInfo ii{hdr_sampler, hdr.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = set;
    w.dstBinding = 0;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo = &ii;
    vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
  }

  VkShaderModule shader(const uint32_t* code, size_t bytes) {
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = bytes;
    ci.pCode = code;
    VkShaderModule m;
    VK_CHECK(vkCreateShaderModule(device, &ci, nullptr, &m));
    return m;
  }

  struct PipelineDesc {
    VkShaderModule vs = VK_NULL_HANDLE, fs = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    bool vertex_input = true;
    bool position_only = false;
    bool skinned = false;
    bool overlay = false;  // đỉnh 2D (vec2 pos + vec4 color), alpha blend
    VkFormat color_format = VK_FORMAT_UNDEFINED;
    VkFormat depth_format = VK_FORMAT_UNDEFINED;
    VkCullModeFlags cull = VK_CULL_MODE_BACK_BIT;
    bool depth_bias = false;
  };

  VkPipeline create_pipeline(const PipelineDesc& d) {
    VkPipelineShaderStageCreateInfo stages[2]{};
    uint32_t stage_count = 1;
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, d.vs,
                 "main", nullptr};
    if (d.fs) {
      stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, d.fs,
                   "main", nullptr};
      stage_count = 2;
    }
    VkVertexInputBindingDescription bind{0, sizeof(geom::Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrs[] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(geom::Vertex, position))},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(geom::Vertex, normal))},
        {2, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<uint32_t>(offsetof(geom::Vertex, uv))},
        {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<uint32_t>(offsetof(geom::Vertex, tangent))},
        {4, 0, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<uint32_t>(offsetof(geom::Vertex, color))},
    };
    VkVertexInputBindingDescription binds[2] = {bind, {1, sizeof(SkinVertex), VK_VERTEX_INPUT_RATE_VERTEX}};
    std::vector<VkVertexInputAttributeDescription> av(attrs, attrs + (d.position_only ? 1 : 5));
    if (d.skinned) {
      av.push_back({5, 1, VK_FORMAT_R16G16B16A16_UINT, static_cast<uint32_t>(offsetof(SkinVertex, joints))});
      av.push_back({6, 1, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<uint32_t>(offsetof(SkinVertex, weights))});
    }
    VkVertexInputBindingDescription obind{0, sizeof(OverlayVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription oattr[] = {
        {0, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<uint32_t>(offsetof(OverlayVertex, pos))},
        {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<uint32_t>(offsetof(OverlayVertex, color))}};
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    if (d.overlay) {
      vi.vertexBindingDescriptionCount = 1;
      vi.pVertexBindingDescriptions = &obind;
      vi.vertexAttributeDescriptionCount = 2;
      vi.pVertexAttributeDescriptions = oattr;
    } else if (d.vertex_input) {
      vi.vertexBindingDescriptionCount = d.skinned ? 2 : 1;
      vi.pVertexBindingDescriptions = binds;
      vi.vertexAttributeDescriptionCount = static_cast<uint32_t>(av.size());
      vi.pVertexAttributeDescriptions = av.data();
    }
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = d.cull;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;
    rs.depthBiasEnable = d.depth_bias ? VK_TRUE : VK_FALSE;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = d.depth_format != VK_FORMAT_UNDEFINED;
    ds.depthWriteEnable = ds.depthTestEnable;
    ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                         VK_COLOR_COMPONENT_A_BIT;
    if (d.overlay) {
      cba.blendEnable = VK_TRUE;
      cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
      cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
      cba.colorBlendOp = VK_BLEND_OP_ADD;
      cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
      cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
      cba.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = d.color_format != VK_FORMAT_UNDEFINED ? 1 : 0;
    cb.pAttachments = &cba;
    std::vector<VkDynamicState> dyn = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    if (d.depth_bias) dyn.push_back(VK_DYNAMIC_STATE_DEPTH_BIAS);
    VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dy.dynamicStateCount = static_cast<uint32_t>(dyn.size());
    dy.pDynamicStates = dyn.data();
    VkPipelineRenderingCreateInfo ri{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    ri.colorAttachmentCount = cb.attachmentCount;
    ri.pColorAttachmentFormats = &d.color_format;
    ri.depthAttachmentFormat = d.depth_format;
    VkGraphicsPipelineCreateInfo pi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pi.pNext = &ri;
    pi.stageCount = stage_count;
    pi.pStages = stages;
    pi.pVertexInputState = &vi;
    pi.pInputAssemblyState = &ia;
    pi.pViewportState = &vp;
    pi.pRasterizationState = &rs;
    pi.pMultisampleState = &ms;
    pi.pDepthStencilState = &ds;
    pi.pColorBlendState = &cb;
    pi.pDynamicState = &dy;
    pi.layout = d.layout;
    VkPipeline p;
    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pi, nullptr, &p));
    return p;
  }

  void create_pipelines() {
    VkShaderModule mv = shader(kMeshVert, sizeof(kMeshVert)), mf = shader(kMeshFrag, sizeof(kMeshFrag));
    VkShaderModule sv = shader(kShadowVert, sizeof(kShadowVert));
    VkShaderModule tv = shader(kTonemapVert, sizeof(kTonemapVert)), tf = shader(kTonemapFrag, sizeof(kTonemapFrag));
    PipelineDesc m;
    m.vs = mv;
    m.fs = mf;
    m.layout = mesh_layout;
    m.color_format = kHdrFormat;
    m.depth_format = kDepthFormat;
    mesh_pipeline = create_pipeline(m);
    VkShaderModule msv = shader(kMeshSkinnedVert, sizeof(kMeshSkinnedVert));
    VkShaderModule ssv = shader(kShadowSkinnedVert, sizeof(kShadowSkinnedVert));
    PipelineDesc ms = m;
    ms.vs = msv;
    ms.skinned = true;
    skinned_pipeline = create_pipeline(ms);
    PipelineDesc s;
    s.vs = sv;
    s.layout = mesh_layout;
    s.position_only = true;
    s.depth_format = kDepthFormat;
    s.cull = VK_CULL_MODE_NONE;
    s.depth_bias = true;
    shadow_pipeline = create_pipeline(s);
    PipelineDesc ss = s;
    ss.vs = ssv;
    ss.skinned = true;
    shadow_skinned_pipeline = create_pipeline(ss);
    PipelineDesc t;
    t.vs = tv;
    t.fs = tf;
    t.layout = tonemap_layout;
    t.vertex_input = false;
    t.color_format = swap_format;
    t.cull = VK_CULL_MODE_NONE;
    tonemap_pipeline = create_pipeline(t);
    VkShaderModule ov = shader(kOverlayVert, sizeof(kOverlayVert)), of = shader(kOverlayFrag, sizeof(kOverlayFrag));
    PipelineDesc o;
    o.vs = ov;
    o.fs = of;
    o.layout = overlay_layout;
    o.overlay = true;
    o.color_format = swap_format;
    o.cull = VK_CULL_MODE_NONE;
    overlay_pipeline = create_pipeline(o);
    vkDestroyShaderModule(device, ov, nullptr);
    vkDestroyShaderModule(device, of, nullptr);
    for (auto mod : {mv, mf, sv, tv, tf, msv, ssv}) vkDestroyShaderModule(device, mod, nullptr);
  }

  // ---------------------------------------------------------------- texture/mesh
  uint32_t upload_texture(const geom::ImageRGBA8& src, bool srgb) {
    if (!src.valid()) throw std::runtime_error("ảnh không hợp lệ");
    const VkFormat fmt = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    VkFormatProperties fp;
    vkGetPhysicalDeviceFormatProperties(gpu, fmt, &fp);
    const bool can_blit = (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) &&
                          (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT) &&
                          (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT);
    const uint32_t mips = can_blit ? static_cast<uint32_t>(std::floor(std::log2(std::max(src.width, src.height)))) + 1 : 1;
    const VkExtent2D ext{static_cast<uint32_t>(src.width), static_cast<uint32_t>(src.height)};
    Image img = create_image(ext, fmt,
                             VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                             VK_IMAGE_ASPECT_COLOR_BIT, mips);
    Buffer staging = create_buffer(src.pixels.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
    std::memcpy(staging.mapped, src.pixels.data(), src.pixels.size());
    vmaFlushAllocation(allocator, staging.alloc, 0, VK_WHOLE_SIZE);
    immediate([&](VkCommandBuffer cmd) {
      image_barrier(cmd, img.image, range(VK_IMAGE_ASPECT_COLOR_BIT, mips), VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, 0, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    VK_ACCESS_2_TRANSFER_WRITE_BIT);
      VkBufferImageCopy copy{};
      copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
      copy.imageExtent = {ext.width, ext.height, 1};
      vkCmdCopyBufferToImage(cmd, staging.buffer, img.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
      int32_t w = src.width, h = src.height;
      for (uint32_t i = 1; i < mips; ++i) {
        image_barrier(cmd, img.image, range(VK_IMAGE_ASPECT_COLOR_BIT, 1, i - 1), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                      VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
        VkImageBlit blit{};
        blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 0, 1};
        blit.srcOffsets[1] = {w, h, 1};
        w = std::max(1, w / 2);
        h = std::max(1, h / 2);
        blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i, 0, 1};
        blit.dstOffsets[1] = {w, h, 1};
        vkCmdBlitImage(cmd, img.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, img.image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
        image_barrier(cmd, img.image, range(VK_IMAGE_ASPECT_COLOR_BIT, 1, i - 1), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                      VK_ACCESS_2_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                      VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
      }
      image_barrier(cmd, img.image, range(VK_IMAGE_ASPECT_COLOR_BIT, 1, mips - 1), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    });
    destroy(staging);
    textures.push_back(img);
    return static_cast<uint32_t>(textures.size() - 1);
  }

  void refresh_texture_descriptors() {
    vkDeviceWaitIdle(device);  // descriptor set có thể đang được frame trước dùng
    for (auto& f : frames) write_scene_set(f.scene_set, f.ubo, f.joints);
  }

  // ---------------------------------------------------------------- frame
  void read_timestamps(Frame& f, uint32_t idx) {
    if (!timestamps_supported || !f.timestamps_written) return;
    uint64_t ts[2] = {};
    if (vkGetQueryPoolResults(device, query_pool, idx * 2, 2, sizeof(ts), ts, sizeof(uint64_t),
                              VK_QUERY_RESULT_64_BIT) == VK_SUCCESS) {
      stats.gpu_ms = static_cast<double>(ts[1] - ts[0]) * props.limits.timestampPeriod / 1e6;
    }
  }

  glm::mat4 light_matrix(const SceneParams& sp) const {
    const glm::vec3 L = glm::normalize(sp.sun_dir);
    const glm::vec3 up = std::abs(L.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
    const glm::mat4 lv = glm::lookAt(glm::vec3(0.0f), -L, up);
    const float r = sp.shadow_radius;
    glm::vec3 c = glm::vec3(lv * glm::vec4(sp.focus, 1.0f));
    const float texel = 2.0f * r / static_cast<float>(settings.shadow_map_size);
    c.x = std::floor(c.x / texel) * texel;  // bám lưới texel -> bóng không rung khi camera di chuyển
    c.y = std::floor(c.y / texel) * texel;
    const glm::mat4 lp = glm::orthoRH_ZO(c.x - r, c.x + r, c.y - r, c.y + r, -c.z - 4.0f * r, -c.z + 4.0f * r);
    return lp * lv;
  }

  void push_draw(VkCommandBuffer cmd, const DrawItem& it, int joint_offset) {
    DrawPush p;
    p.model = it.transform;
    p.base_color = it.material.base_color;
    p.mat_params = glm::vec4(it.material.metallic, it.material.roughness, it.material.emissive,
                             static_cast<float>(std::max(joint_offset, 0)));
    const auto clamp_tex = [](uint32_t t) { return t < kMaxTextures ? t : kTexWhite; };
    uint32_t flags = (it.material.cutaway ? 1u : 0u) | (it.material.unlit ? 2u : 0u);
    p.tex = glm::uvec4(clamp_tex(it.material.base_tex), clamp_tex(it.material.normal_tex),
                       clamp_tex(it.material.orm_tex), flags);
    vkCmdPushConstants(cmd, mesh_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(p), &p);
  }

  bool draw(const SceneParams& sp, std::span<const DrawItem> items, const Overlay* overlay) {
    const auto t0 = std::chrono::steady_clock::now();
    if (swap_dirty || !swapchain || swap_views.empty()) {
      if (!recreate_swapchain()) return false;
    }
    Frame& f = frames[frame_index];
    VK_CHECK(vkWaitForFences(device, 1, &f.fence, VK_TRUE, UINT64_MAX));
    read_timestamps(f, frame_index);
    ++frame_counter;
    collect_garbage(false);

    uint32_t image_index = 0;
    VkResult ar = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, f.acquired, VK_NULL_HANDLE, &image_index);
    if (ar == VK_ERROR_OUT_OF_DATE_KHR) {
      swap_dirty = true;
      return false;
    }
    if (ar != VK_SUCCESS && ar != VK_SUBOPTIMAL_KHR) VK_CHECK(ar);
    VK_CHECK(vkResetFences(device, 1, &f.fence));

    // ---- uniforms
    const glm::mat4 view_proj = sp.proj * sp.view;
    const glm::mat4 light_vp = light_matrix(sp);
    FrameUniforms u{};
    u.view_proj = view_proj;
    u.light_view_proj = light_vp;
    u.camera_back_dir = glm::vec4(glm::normalize(sp.camera_back_dir), 0.0f);
    u.sun_dir = glm::vec4(glm::normalize(sp.sun_dir), sp.sun_intensity);
    u.sun_color = glm::vec4(sp.sun_color, 0.0f);
    u.sky_color = glm::vec4(sp.sky_color, sp.ambient_intensity);
    u.ground_color = glm::vec4(sp.ground_color, 0.0f);
    u.fog_color = glm::vec4(sp.fog_color, sp.fog_density);
    u.focus = glm::vec4(sp.focus, sp.cutaway_radius);
    std::vector<PointLight> lights = sp.point_lights;
    std::sort(lights.begin(), lights.end(), [&](const PointLight& a, const PointLight& b) {
      const glm::vec3 da = a.position - sp.focus, db = b.position - sp.focus;
      return glm::dot(da, da) < glm::dot(db, db);
    });
    const uint32_t nl = std::min<uint32_t>(kMaxPointLights, static_cast<uint32_t>(lights.size()));
    for (uint32_t i = 0; i < nl; ++i) {
      u.point_pos[i] = glm::vec4(lights[i].position, lights[i].radius);
      u.point_color[i] = glm::vec4(lights[i].color * lights[i].intensity, 0.0f);
    }
    u.params = glm::vec4(sp.exposure, 1.0f / static_cast<float>(settings.shadow_map_size), sp.time,
                         static_cast<float>(nl));
    std::memcpy(f.ubo.mapped, &u, sizeof(u));
    vmaFlushAllocation(allocator, f.ubo.alloc, 0, VK_WHOLE_SIZE);

    // ---- culling
    const auto cam_frustum = geom::Frustum::from_matrix(view_proj);
    const auto light_frustum = geom::Frustum::from_matrix(light_vp);
    std::vector<const DrawItem*> visible, casters;
    visible.reserve(items.size());
    stats.culled = 0;
    for (const auto& it : items) {
      const GpuMesh* m = meshes.valid(it.mesh) ? meshes.get(it.mesh) : nullptr;
      if (!m) continue;
      const geom::Aabb wb = m->bounds.transformed(it.transform);
      if (cam_frustum.intersects(wb.min, wb.max)) visible.push_back(&it);
      else ++stats.culled;
      if (it.cast_shadow && light_frustum.intersects(wb.min, wb.max)) casters.push_back(&it);
    }

    // ---- ma trận skinning vào SSBO của frame
    std::vector<int> joint_off(items.size(), -1);
    {
      uint32_t used = 0;
      auto* dst = static_cast<glm::mat4*>(f.joints.mapped);
      for (size_t i = 0; i < items.size(); ++i) {
        const auto* js = items[i].joints;
        if (!js || js->empty()) continue;
        const auto n = static_cast<uint32_t>(js->size());
        if (used + n > kMaxJointMatricesPerFrame) {
          log::warn("render", "vượt {} ma trận joint/frame, bỏ skinning cho draw {}", kMaxJointMatricesPerFrame, i);
          continue;
        }
        std::memcpy(dst + used, js->data(), n * sizeof(glm::mat4));
        joint_off[i] = static_cast<int>(used);
        used += n;
      }
      if (used) vmaFlushAllocation(allocator, f.joints.alloc, 0, used * sizeof(glm::mat4));
    }
    auto offset_of = [&](const DrawItem* it) { return joint_off[static_cast<size_t>(it - items.data())]; };

    // ---- record
    VkCommandBuffer cmd = f.cmd;
    VK_CHECK(vkResetCommandBuffer(cmd, 0));
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &bi));
    if (timestamps_supported) {
      vkCmdResetQueryPool(cmd, query_pool, frame_index * 2, 2);
      vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, query_pool, frame_index * 2);
    }
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mesh_layout, 0, 1, &f.scene_set, 0, nullptr);

    auto bind_mesh = [&](const GpuMesh& m) {
      VkDeviceSize off = 0;
      vkCmdBindVertexBuffers(cmd, 0, 1, &m.vb.buffer, &off);
      if (m.skinned) vkCmdBindVertexBuffers(cmd, 1, 1, &m.sb.buffer, &off);
      vkCmdBindIndexBuffer(cmd, m.ib.buffer, 0, VK_INDEX_TYPE_UINT32);
    };
    // Skinned chỉ khi mesh có skin VÀ draw có ma trận; nếu không, vẽ bind pose bằng pipeline thường.
    auto use_skin = [&](const DrawItem* it, const GpuMesh& m) { return m.skinned && offset_of(it) >= 0; };

    // Shadow pass
    image_barrier(cmd, shadow.image, range(VK_IMAGE_ASPECT_DEPTH_BIT), VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0,
                  VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    {
      VkRenderingAttachmentInfo da{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
      da.imageView = shadow.view;
      da.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
      da.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
      da.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
      da.clearValue.depthStencil = {1.0f, 0};
      VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
      ri.renderArea = {{0, 0}, shadow.extent};
      ri.layerCount = 1;
      ri.pDepthAttachment = &da;
      vkCmdBeginRendering(cmd, &ri);
      VkViewport vp{0, 0, static_cast<float>(shadow.extent.width), static_cast<float>(shadow.extent.height), 0, 1};
      VkRect2D sc{{0, 0}, shadow.extent};
      vkCmdSetViewport(cmd, 0, 1, &vp);
      vkCmdSetScissor(cmd, 0, 1, &sc);
      vkCmdSetDepthBias(cmd, 1.25f, 0.0f, 1.75f);
      int cur = -1;
      for (const DrawItem* it : casters) {
        const GpuMesh& m = *meshes.get(it->mesh);
        const int want = use_skin(it, m) ? 1 : 0;
        if (want != cur) {
          vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, want ? shadow_skinned_pipeline : shadow_pipeline);
          cur = want;
        }
        bind_mesh(m);
        push_draw(cmd, *it, offset_of(it));
        vkCmdDrawIndexed(cmd, m.index_count, 1, 0, 0, 0);
      }
      vkCmdEndRendering(cmd);
    }
    image_barrier(cmd, shadow.image, range(VK_IMAGE_ASPECT_DEPTH_BIT), VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                  VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

    // Scene pass (HDR)
    image_barrier(cmd, hdr.image, range(VK_IMAGE_ASPECT_COLOR_BIT), VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0,
                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    image_barrier(cmd, depth.image, range(VK_IMAGE_ASPECT_DEPTH_BIT), VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, 0,
                  VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    stats.draw_calls = 0;
    stats.shadow_draw_calls = static_cast<uint32_t>(casters.size());
    stats.triangles = 0;
    {
      VkRenderingAttachmentInfo ca{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
      ca.imageView = hdr.view;
      ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      ca.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
      ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
      ca.clearValue.color = {{sp.clear_color.r, sp.clear_color.g, sp.clear_color.b, 1.0f}};
      VkRenderingAttachmentInfo da{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
      da.imageView = depth.view;
      da.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
      da.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
      da.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
      da.clearValue.depthStencil = {1.0f, 0};
      VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
      ri.renderArea = {{0, 0}, extent};
      ri.layerCount = 1;
      ri.colorAttachmentCount = 1;
      ri.pColorAttachments = &ca;
      ri.pDepthAttachment = &da;
      vkCmdBeginRendering(cmd, &ri);
      VkViewport vp{0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height), 0, 1};
      VkRect2D sc{{0, 0}, extent};
      vkCmdSetViewport(cmd, 0, 1, &vp);
      vkCmdSetScissor(cmd, 0, 1, &sc);
      const GpuMesh* bound = nullptr;
      int cur = -1;
      for (const DrawItem* it : visible) {
        const GpuMesh* m = meshes.get(it->mesh);
        const int want = use_skin(it, *m) ? 1 : 0;
        if (want != cur) {
          vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, want ? skinned_pipeline : mesh_pipeline);
          cur = want;
        }
        if (m != bound) {
          bind_mesh(*m);
          bound = m;
        }
        push_draw(cmd, *it, offset_of(it));
        vkCmdDrawIndexed(cmd, m->index_count, 1, 0, 0, 0);
        ++stats.draw_calls;
        stats.triangles += m->index_count / 3;
      }
      vkCmdEndRendering(cmd);
    }
    image_barrier(cmd, hdr.image, range(VK_IMAGE_ASPECT_COLOR_BIT), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                  VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

    // Tonemap → swapchain
    VkImage swap_img = swap_images[image_index];
    image_barrier(cmd, swap_img, range(VK_IMAGE_ASPECT_COLOR_BIT), VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    {
      VkRenderingAttachmentInfo ca{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
      ca.imageView = swap_views[image_index];
      ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      ca.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
      ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
      VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
      ri.renderArea = {{0, 0}, extent};
      ri.layerCount = 1;
      ri.colorAttachmentCount = 1;
      ri.pColorAttachments = &ca;
      vkCmdBeginRendering(cmd, &ri);
      VkViewport vp{0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height), 0, 1};
      VkRect2D sc{{0, 0}, extent};
      vkCmdSetViewport(cmd, 0, 1, &vp);
      vkCmdSetScissor(cmd, 0, 1, &sc);
      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, tonemap_pipeline);
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, tonemap_layout, 0, 1, &f.tonemap_set, 0, nullptr);
      const glm::vec4 tp(sp.exposure, sp.vignette, 0, 0);
      vkCmdPushConstants(cmd, tonemap_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(tp), &tp);
      vkCmdDraw(cmd, 3, 1, 0, 0);
      if (overlay && !overlay->vertices().empty()) {
        const auto& ov = overlay->vertices();
        const auto n = static_cast<uint32_t>(std::min<size_t>(ov.size(), kMaxOverlayVertices));
        std::memcpy(f.overlay.mapped, ov.data(), n * sizeof(OverlayVertex));
        vmaFlushAllocation(allocator, f.overlay.alloc, 0, n * sizeof(OverlayVertex));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, overlay_pipeline);
        const glm::vec2 screen(static_cast<float>(extent.width), static_cast<float>(extent.height));
        vkCmdPushConstants(cmd, overlay_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(screen), &screen);
        VkDeviceSize off = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &f.overlay.buffer, &off);
        vkCmdDraw(cmd, n, 1, 0, 0);
      }
      vkCmdEndRendering(cmd);
    }

    // Screenshot (tùy chọn)
    Buffer readback;
    const bool capture = screenshot_requested;
    if (capture) {
      readback = create_buffer(static_cast<VkDeviceSize>(extent.width) * extent.height * 4,
                               VK_BUFFER_USAGE_TRANSFER_DST_BIT, false, true);
      image_barrier(cmd, swap_img, range(VK_IMAGE_ASPECT_COLOR_BIT), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    VK_ACCESS_2_TRANSFER_READ_BIT);
      VkBufferImageCopy copy{};
      copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
      copy.imageExtent = {extent.width, extent.height, 1};
      vkCmdCopyImageToBuffer(cmd, swap_img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 1, &copy);
      image_barrier(cmd, swap_img, range(VK_IMAGE_ASPECT_COLOR_BIT), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                    VK_PIPELINE_STAGE_2_NONE, 0);
    } else {
      image_barrier(cmd, swap_img, range(VK_IMAGE_ASPECT_COLOR_BIT), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_NONE, 0);
    }
    if (timestamps_supported) {
      vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, query_pool, frame_index * 2 + 1);
      f.timestamps_written = true;
    }
    VK_CHECK(vkEndCommandBuffer(cmd));

    // ---- submit + present
    VkSemaphoreSubmitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    wait.semaphore = f.acquired;
    wait.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSemaphoreSubmitInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    signal.semaphore = render_done[image_index];
    signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkCommandBufferSubmitInfo cbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cbi.commandBuffer = cmd;
    VkSubmitInfo2 si{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    si.waitSemaphoreInfoCount = 1;
    si.pWaitSemaphoreInfos = &wait;
    si.commandBufferInfoCount = 1;
    si.pCommandBufferInfos = &cbi;
    si.signalSemaphoreInfoCount = 1;
    si.pSignalSemaphoreInfos = &signal;
    VK_CHECK(vkQueueSubmit2(queue, 1, &si, f.fence));

    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &render_done[image_index];
    pi.swapchainCount = 1;
    pi.pSwapchains = &swapchain;
    pi.pImageIndices = &image_index;
    const VkResult pr = vkQueuePresentKHR(queue, &pi);
    if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR) swap_dirty = true;
    else if (pr != VK_SUCCESS) VK_CHECK(pr);

    if (capture) {
      VK_CHECK(vkWaitForFences(device, 1, &f.fence, VK_TRUE, UINT64_MAX));
      vmaInvalidateAllocation(allocator, readback.alloc, 0, VK_WHOLE_SIZE);
      geom::ImageRGBA8 img;
      img.width = static_cast<int>(extent.width);
      img.height = static_cast<int>(extent.height);
      img.pixels.resize(static_cast<size_t>(img.width) * img.height * 4);
      std::memcpy(img.pixels.data(), readback.mapped, img.pixels.size());
      const bool bgra = swap_format == VK_FORMAT_B8G8R8A8_SRGB || swap_format == VK_FORMAT_B8G8R8A8_UNORM;
      for (size_t i = 0; i < img.pixels.size(); i += 4) {
        if (bgra) std::swap(img.pixels[i], img.pixels[i + 2]);
        img.pixels[i + 3] = 255;
      }
      if (geom::write_png(screenshot_path, img)) log::info("render", "đã lưu screenshot {}", screenshot_path.string());
      else log::error("render", "không ghi được screenshot {}", screenshot_path.string());
      destroy(readback);
      screenshot_requested = false;
    }

    frame_index = (frame_index + 1) % kFramesInFlight;
    stats.cpu_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return true;
  }

  void init() {
    create_instance();
    pick_gpu();
    create_device();
    const VkExtent2D sext{settings.shadow_map_size, settings.shadow_map_size};
    shadow = create_image(sext, kDepthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                          VK_IMAGE_ASPECT_DEPTH_BIT);
    // Shadow map ở layout SHADER_READ trước frame đầu để descriptor hợp lệ.
    immediate([&](VkCommandBuffer cmd) {
      image_barrier(cmd, shadow.image, range(VK_IMAGE_ASPECT_DEPTH_BIT), VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, 0,
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    });
    create_samplers_and_layouts();
    create_swapchain();
    if (!swapchain) throw std::runtime_error("không tạo được swapchain (cửa sổ kích thước 0?)");
    create_pipelines();
    // Texture mặc định: 0 = trắng, 1 = normal phẳng.
    upload_texture(geom::make_solid(4, 0xFFFFFFFFu), true);
    upload_texture(geom::make_solid(4, 0x8080FFFFu), false);
    refresh_texture_descriptors();
  }

  ~Impl() {
    if (device) vkDeviceWaitIdle(device);
    if (allocator) collect_garbage(true);
    for (auto h : mesh_list) {
      if (GpuMesh* m = meshes.get(h)) {
        destroy(m->vb);
        destroy(m->ib);
        destroy(m->sb);
      }
    }
    for (auto& t : textures) destroy(t);
    if (device) {
      for (auto p : {mesh_pipeline, shadow_pipeline, tonemap_pipeline, skinned_pipeline, shadow_skinned_pipeline,
                     overlay_pipeline})
        if (p) vkDestroyPipeline(device, p, nullptr);
      if (mesh_layout) vkDestroyPipelineLayout(device, mesh_layout, nullptr);
      if (tonemap_layout) vkDestroyPipelineLayout(device, tonemap_layout, nullptr);
      if (overlay_layout) vkDestroyPipelineLayout(device, overlay_layout, nullptr);
      if (desc_pool) vkDestroyDescriptorPool(device, desc_pool, nullptr);
      if (scene_dsl) vkDestroyDescriptorSetLayout(device, scene_dsl, nullptr);
      if (tonemap_dsl) vkDestroyDescriptorSetLayout(device, tonemap_dsl, nullptr);
      for (auto s : {tex_sampler, shadow_sampler, hdr_sampler})
        if (s) vkDestroySampler(device, s, nullptr);
      destroy_swapchain_views();
      destroy(shadow);
      if (swapchain) vkDestroySwapchainKHR(device, swapchain, nullptr);
      for (auto& f : frames) {
        destroy(f.ubo);
        destroy(f.joints);
        destroy(f.overlay);
        if (f.fence) vkDestroyFence(device, f.fence, nullptr);
        if (f.acquired) vkDestroySemaphore(device, f.acquired, nullptr);
      }
      if (query_pool) vkDestroyQueryPool(device, query_pool, nullptr);
      if (cmd_pool) vkDestroyCommandPool(device, cmd_pool, nullptr);
      if (allocator) vmaDestroyAllocator(allocator);
      vkDestroyDevice(device, nullptr);
    }
    if (surface) SDL_Vulkan_DestroySurface(instance, surface, nullptr);
    if (messenger) {
      auto fn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
          vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
      if (fn) fn(instance, messenger, nullptr);
    }
    if (instance) vkDestroyInstance(instance, nullptr);
  }
};

// ======================================================================== public API

Renderer::Renderer(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Renderer::~Renderer() = default;

std::unique_ptr<Renderer> Renderer::create(SDL_Window* window, const RendererSettings& settings, std::string& error) {
  auto impl = std::make_unique<Impl>();
  impl->settings = settings;
  impl->window = window;
  try {
    impl->init();
  } catch (const std::exception& e) {
    error = e.what();
    return nullptr;  // ~Impl dọn phần đã tạo
  }
  return std::unique_ptr<Renderer>(new Renderer(std::move(impl)));
}

MeshHandle Renderer::upload_mesh(const geom::MeshData& mesh, std::string_view name) {
  auto& d = *impl_;
  if (auto err = geom::validate_mesh(mesh); !err.empty()) {
    log::error("render", "mesh '{}' không hợp lệ: {}", name, err);
    return {};
  }
  if (auto existing = d.meshes.find(name); d.meshes.valid(existing)) return existing;
  GpuMesh gm;
  const VkDeviceSize vb = mesh.vertices.size() * sizeof(geom::Vertex);
  const VkDeviceSize ib = mesh.indices.size() * sizeof(uint32_t);
  gm.vb = d.create_buffer(vb, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, false);
  gm.ib = d.create_buffer(ib, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, false);
  gm.index_count = static_cast<uint32_t>(mesh.indices.size());
  gm.bounds = mesh.bounds.valid() ? mesh.bounds : geom::Aabb{};
  if (!gm.bounds.valid()) {
    geom::MeshData tmp = mesh;
    tmp.recompute_bounds();
    gm.bounds = tmp.bounds;
  }
  Buffer staging = d.create_buffer(vb + ib, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
  std::memcpy(staging.mapped, mesh.vertices.data(), vb);
  std::memcpy(static_cast<char*>(staging.mapped) + vb, mesh.indices.data(), ib);
  vmaFlushAllocation(d.allocator, staging.alloc, 0, VK_WHOLE_SIZE);
  d.immediate([&](VkCommandBuffer cmd) {
    VkBufferCopy c1{0, 0, vb}, c2{vb, 0, ib};
    vkCmdCopyBuffer(cmd, staging.buffer, gm.vb.buffer, 1, &c1);
    vkCmdCopyBuffer(cmd, staging.buffer, gm.ib.buffer, 1, &c2);
  });
  d.destroy(staging);
  auto h = d.meshes.add(name, gm);
  d.mesh_list.push_back(h);
  return h;
}

MeshHandle Renderer::upload_skinned_mesh(const geom::MeshData& mesh, std::span<const std::array<uint16_t, 4>> joints,
                                         std::span<const glm::vec4> weights, std::string_view name) {
  auto& d = *impl_;
  if (joints.size() != mesh.vertices.size() || weights.size() != mesh.vertices.size()) {
    log::error("render", "mesh skinned '{}': số joints/weights không khớp số vertex", name);
    return {};
  }
  MeshHandle h = upload_mesh(mesh, name);
  GpuMesh* gm = d.meshes.get(h);
  if (!gm || gm->skinned) return h;
  std::vector<SkinVertex> sv(mesh.vertices.size());
  for (size_t i = 0; i < sv.size(); ++i) {
    for (int k = 0; k < 4; ++k) {
      sv[i].joints[k] = joints[i][static_cast<size_t>(k)];
      sv[i].weights[k] = weights[i][k];
    }
  }
  const VkDeviceSize bytes = sv.size() * sizeof(SkinVertex);
  gm->sb = d.create_buffer(bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, false);
  Buffer staging = d.create_buffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
  std::memcpy(staging.mapped, sv.data(), bytes);
  vmaFlushAllocation(d.allocator, staging.alloc, 0, VK_WHOLE_SIZE);
  d.immediate([&](VkCommandBuffer cmd) {
    VkBufferCopy c{0, 0, bytes};
    vkCmdCopyBuffer(cmd, staging.buffer, gm->sb.buffer, 1, &c);
  });
  d.destroy(staging);
  gm->skinned = true;
  // Animation có thể vươn ra ngoài bounds bind pose: nới 35% để frustum culling không cắt nhầm.
  const glm::vec3 c = gm->bounds.center(), e = gm->bounds.extent() * 1.35f;
  gm->bounds.min = c - e;
  gm->bounds.max = c + e;
  return h;
}

uint32_t Renderer::upload_texture(const geom::ImageRGBA8& image, bool srgb, std::string_view name) {
  auto& d = *impl_;
  if (d.textures.size() >= kMaxTextures) {
    log::error("render", "hết slot texture ({}), '{}' dùng texture trắng", kMaxTextures, name);
    return kTexWhite;
  }
  try {
    const uint32_t idx = d.upload_texture(image, srgb);
    d.refresh_texture_descriptors();
    return idx;
  } catch (const std::exception& e) {
    log::error("render", "upload texture '{}' lỗi: {}", name, e.what());
    return kTexWhite;
  }
}

const geom::Aabb* Renderer::mesh_bounds(MeshHandle h) const {
  const GpuMesh* m = impl_->meshes.valid(h) ? const_cast<Impl&>(*impl_).meshes.get(h) : nullptr;
  return m ? &m->bounds : nullptr;
}

void Renderer::release_mesh(MeshHandle h) {
  auto& d = *impl_;
  GpuMesh* m = d.meshes.get(h);
  if (!m) return;
  d.pending_deletes.push_back({*m, d.frame_counter + kFramesInFlight + 1});
  d.meshes.release(h);
  std::erase(d.mesh_list, h);
}

size_t Renderer::mesh_count() const { return impl_->meshes.size(); }

bool Renderer::draw_frame(const SceneParams& scene, std::span<const DrawItem> items, const Overlay* overlay) {
  return impl_->draw(scene, items, overlay);
}
void Renderer::notify_resized() { impl_->swap_dirty = true; }
void Renderer::request_screenshot(const std::filesystem::path& path) {
  impl_->screenshot_path = path;
  impl_->screenshot_requested = true;
}
bool Renderer::screenshot_pending() const { return impl_->screenshot_requested; }
const RenderStats& Renderer::stats() const { return impl_->stats; }
std::string Renderer::device_name() const { return impl_->props.deviceName; }
std::string Renderer::api_version() const {
  const uint32_t v = impl_->props.apiVersion;
  return std::format("{}.{}.{}", VK_API_VERSION_MAJOR(v), VK_API_VERSION_MINOR(v), VK_API_VERSION_PATCH(v));
}
void Renderer::wait_idle() {
  if (impl_->device) vkDeviceWaitIdle(impl_->device);
}

}  // namespace pb::render
