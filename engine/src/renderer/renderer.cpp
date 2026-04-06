#include <kuma/renderer.h>
#include <vulkan/vulkan.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <limits>
#include <vector>
#include <fstream>
#include <array>

// stb_image: single-header image loading library.
// STB_IMAGE_IMPLEMENTATION generates the function bodies — must be in exactly one .cpp.
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace kuma {

// ── File I/O ────────────────────────────────────────────────────

static std::vector<char> read_binary_file(const char* path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        std::printf("[Kuma] Failed to open file: %s\n", path);
        return {};
    }

    auto file_size = file.tellg();
    std::vector<char> buffer(static_cast<size_t>(file_size));

    file.seekg(0);
    file.read(buffer.data(), file_size);

    return buffer;
}

// ── Vertex Data ─────────────────────────────────────────────────
// Now uses UV coordinates instead of color for texture mapping.
//   layout(location = 0) in vec2 in_position  →  position (offset 0)
//   layout(location = 1) in vec2 in_uv        →  uv       (offset 8)

struct Vertex {
    float position[2];   // x, y  — clip space coordinates (-1 to +1)
    float uv[2];         // u, v  — texture coordinates (0 to 1)
};

// ── Debug Messenger Callback ────────────────────────────────────
// Vulkan validation layers call this when they detect an error.

static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT /*type*/,
    const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
    void* /*user_data*/)
{
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        std::printf("[Vulkan] %s\n", callback_data->pMessage);
    }
    return VK_FALSE;
}

// ── Implementation ──────────────────────────────────────────────
// All Vulkan state lives here, invisible to game code.

class RendererImpl {
public:
    bool init(const RendererConfig& config);
    void shutdown();
    bool begin_frame();
    void end_frame();
    void on_resize(int32_t width, int32_t height);

private:
    bool create_instance();
    bool create_debug_messenger();
    bool create_surface();
    bool pick_physical_device();
    bool create_logical_device();
    bool create_swapchain();
    bool create_render_pass();
    bool create_framebuffers();
    bool create_graphics_pipeline();
    bool create_vertex_buffer();
    bool create_index_buffer();
    bool create_texture();
    bool create_descriptor_sets();
    bool create_command_pool();
    bool create_command_buffers();
    bool create_sync_objects();

    void copy_buffer_to_image(VkBuffer buffer, VkImage image,
        uint32_t width, uint32_t height);
    void transition_image_layout(VkImage image, VkImageLayout old_layout,
        VkImageLayout new_layout);
    VkCommandBuffer begin_single_command() const;
    void end_single_command(VkCommandBuffer cmd) const;

    void destroy_swapchain();
    bool recreate_swapchain();

    VkShaderModule create_shader_module(const std::vector<char>& code) const;
    VkSurfaceFormatKHR choose_surface_format() const;
    VkPresentModeKHR choose_present_mode() const;
    VkExtent2D choose_extent() const;
    uint32_t find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties) const;

    // Config
    SDL_Window* window_ = nullptr;
    int32_t width_ = 0;
    int32_t height_ = 0;
    bool validation_enabled_ = true;

    // Core Vulkan objects
    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug_messenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphics_queue_ = VK_NULL_HANDLE;
    VkQueue present_queue_ = VK_NULL_HANDLE;
    uint32_t queue_family_index_ = 0;

    // Swapchain
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapchain_format_ = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchain_extent_ = {0, 0};
    std::vector<VkImage> swapchain_images_;
    std::vector<VkImageView> swapchain_image_views_;

    // Render pass and framebuffers
    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers_;

    // Graphics pipeline
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline graphics_pipeline_ = VK_NULL_HANDLE;

    // Vertex buffer
    VkBuffer vertex_buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory vertex_buffer_memory_ = VK_NULL_HANDLE;

    // Index buffer
    VkBuffer index_buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory index_buffer_memory_ = VK_NULL_HANDLE;
    uint32_t index_count_ = 0;

    // Texture
    VkImage texture_image_ = VK_NULL_HANDLE;
    VkDeviceMemory texture_image_memory_ = VK_NULL_HANDLE;
    VkImageView texture_image_view_ = VK_NULL_HANDLE;
    VkSampler texture_sampler_ = VK_NULL_HANDLE;

    // Descriptors — how we bind the texture to the shader
    VkDescriptorSetLayout descriptor_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptor_sets_;  // one per frame-in-flight

    // Commands
    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> command_buffers_;

    // Synchronization
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
    std::vector<VkSemaphore> image_available_semaphores_;  // per frame-in-flight
    std::vector<VkSemaphore> render_finished_semaphores_;  // per swapchain image
    std::vector<VkFence> in_flight_fences_;                // per frame-in-flight
    uint32_t current_frame_ = 0;
    uint32_t current_image_index_ = 0;

    bool framebuffer_resized_ = false;
};

// ── Renderer (public thin wrapper) ──────────────────────────────

Renderer::Renderer() = default;

Renderer::~Renderer() {
    shutdown();
}

bool Renderer::init(const RendererConfig& config) {
    impl_ = new RendererImpl();
    if (!impl_->init(config)) {
        delete impl_;
        impl_ = nullptr;
        return false;
    }
    return true;
}

void Renderer::shutdown() {
    if (impl_) {
        impl_->shutdown();
        delete impl_;
        impl_ = nullptr;
    }
}

bool Renderer::begin_frame() {
    return impl_->begin_frame();
}

void Renderer::end_frame() {
    impl_->end_frame();
}

void Renderer::on_resize(int32_t width, int32_t height) {
    impl_->on_resize(width, height);
}

// ── RendererImpl ────────────────────────────────────────────────

bool RendererImpl::init(const RendererConfig& config) {
    window_ = config.window;
    width_ = config.width;
    height_ = config.height;
    validation_enabled_ = config.enable_validation;

    if (!create_instance())        return false;
    if (!create_debug_messenger()) return false;
    if (!create_surface())         return false;
    if (!pick_physical_device())   return false;
    if (!create_logical_device())  return false;
    if (!create_swapchain())          return false;
    if (!create_render_pass())        return false;
    if (!create_graphics_pipeline())  return false;
    if (!create_framebuffers())       return false;
    if (!create_vertex_buffer())      return false;
    if (!create_index_buffer())       return false;
    if (!create_command_pool())       return false;
    if (!create_texture())            return false;
    if (!create_descriptor_sets())    return false;
    if (!create_command_buffers())    return false;
    if (!create_sync_objects())       return false;

    std::printf("[Kuma] Vulkan renderer initialized\n");
    return true;
}

void RendererImpl::shutdown() {
    if (device_ == VK_NULL_HANDLE) return;

    vkDeviceWaitIdle(device_);

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroySemaphore(device_, image_available_semaphores_[i], nullptr);
        vkDestroyFence(device_, in_flight_fences_[i], nullptr);
    }
    // render_finished_semaphores_ are destroyed inside destroy_swapchain()

    vkDestroyCommandPool(device_, command_pool_, nullptr);

    // Destroy descriptor pool (this also frees all descriptor sets from it)
    if (descriptor_pool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr);
    }

    // Destroy texture resources
    if (texture_sampler_ != VK_NULL_HANDLE)
        vkDestroySampler(device_, texture_sampler_, nullptr);
    if (texture_image_view_ != VK_NULL_HANDLE)
        vkDestroyImageView(device_, texture_image_view_, nullptr);
    if (texture_image_ != VK_NULL_HANDLE) {
        vkDestroyImage(device_, texture_image_, nullptr);
        vkFreeMemory(device_, texture_image_memory_, nullptr);
    }

    // Destroy buffers
    if (index_buffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, index_buffer_, nullptr);
        vkFreeMemory(device_, index_buffer_memory_, nullptr);
    }
    if (vertex_buffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, vertex_buffer_, nullptr);
        vkFreeMemory(device_, vertex_buffer_memory_, nullptr);
    }

    destroy_swapchain();
    vkDestroyPipeline(device_, graphics_pipeline_, nullptr);
    vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
    vkDestroyDescriptorSetLayout(device_, descriptor_set_layout_, nullptr);
    vkDestroyRenderPass(device_, render_pass_, nullptr);
    vkDestroyDevice(device_, nullptr);
    vkDestroySurfaceKHR(instance_, surface_, nullptr);

    if (debug_messenger_ != VK_NULL_HANDLE) {
        auto destroy_fn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroy_fn) {
            destroy_fn(instance_, debug_messenger_, nullptr);
        }
    }

    vkDestroyInstance(instance_, nullptr);
    device_ = VK_NULL_HANDLE;
    std::printf("[Kuma] Vulkan renderer shut down\n");
}

// ── Instance ────────────────────────────────────────────────────
// The VkInstance is your application's connection to the Vulkan
// library. It stores which API version, layers, and extensions.

bool RendererImpl::create_instance() {
    VkApplicationInfo app_info{};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "Kuma Engine";
    app_info.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    app_info.pEngineName = "Kuma";
    app_info.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    app_info.apiVersion = VK_API_VERSION_1_3;

    // SDL tells us which Vulkan extensions the platform needs for
    // presenting to a window (e.g., VK_KHR_win32_surface on Windows)
    uint32_t sdl_ext_count = 0;
    const char* const* sdl_extensions = SDL_Vulkan_GetInstanceExtensions(&sdl_ext_count);

    std::vector<const char*> extensions(sdl_extensions, sdl_extensions + sdl_ext_count);

    const char* validation_layer = "VK_LAYER_KHRONOS_validation";
    std::vector<const char*> layers;

    if (validation_enabled_) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        layers.push_back(validation_layer);
    }

    VkInstanceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;
    create_info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    create_info.ppEnabledExtensionNames = extensions.data();
    create_info.enabledLayerCount = static_cast<uint32_t>(layers.size());
    create_info.ppEnabledLayerNames = layers.data();

    VkResult result = vkCreateInstance(&create_info, nullptr, &instance_);
    if (result != VK_SUCCESS) {
        std::printf("[Kuma] Failed to create Vulkan instance (error %d)\n", result);
        return false;
    }

    return true;
}

// ── Debug Messenger ─────────────────────────────────────────────
// Hooks into validation layers for error/warning messages.

bool RendererImpl::create_debug_messenger() {
    if (!validation_enabled_) return true;

    VkDebugUtilsMessengerCreateInfoEXT create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    create_info.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    create_info.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    create_info.pfnUserCallback = debug_callback;

    auto create_fn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));

    if (!create_fn) {
        std::printf("[Kuma] Debug messenger extension not available\n");
        return true;
    }

    VkResult result = create_fn(instance_, &create_info, nullptr, &debug_messenger_);
    if (result != VK_SUCCESS) {
        std::printf("[Kuma] Failed to create debug messenger\n");
        return false;
    }

    return true;
}

// ── Surface ─────────────────────────────────────────────────────
// The bridge between Vulkan and the OS window. SDL handles the
// platform-specific part.

bool RendererImpl::create_surface() {
    if (!SDL_Vulkan_CreateSurface(window_, instance_, nullptr, &surface_)) {
        std::printf("[Kuma] Failed to create Vulkan surface: %s\n", SDL_GetError());
        return false;
    }
    return true;
}

// ── Physical Device ─────────────────────────────────────────────
// Pick which GPU to use. Prefer discrete GPU with graphics + present.

bool RendererImpl::pick_physical_device() {
    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(instance_, &device_count, nullptr);

    if (device_count == 0) {
        std::printf("[Kuma] No Vulkan-capable GPUs found\n");
        return false;
    }

    std::vector<VkPhysicalDevice> devices(device_count);
    vkEnumeratePhysicalDevices(instance_, &device_count, devices.data());

    for (const auto& device : devices) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(device, &props);

        uint32_t family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &family_count, nullptr);
        std::vector<VkQueueFamilyProperties> families(family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &family_count, families.data());

        for (uint32_t i = 0; i < family_count; i++) {
            VkBool32 present_support = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface_, &present_support);

            if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present_support) {
                physical_device_ = device;
                queue_family_index_ = i;

                std::printf("[Kuma] Selected GPU: %s\n", props.deviceName);
                return true;
            }
        }
    }

    std::printf("[Kuma] No suitable GPU found\n");
    return false;
}

// ── Logical Device + Queues ─────────────────────────────────────
// The logical device is your interface to the GPU. Queues are how
// you submit work to it.

bool RendererImpl::create_logical_device() {
    float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info{};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = queue_family_index_;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &queue_priority;

    const char* device_extensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

    VkPhysicalDeviceFeatures features{};

    VkDeviceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    create_info.queueCreateInfoCount = 1;
    create_info.pQueueCreateInfos = &queue_info;
    create_info.enabledExtensionCount = 1;
    create_info.ppEnabledExtensionNames = device_extensions;
    create_info.pEnabledFeatures = &features;

    VkResult result = vkCreateDevice(physical_device_, &create_info, nullptr, &device_);
    if (result != VK_SUCCESS) {
        std::printf("[Kuma] Failed to create logical device (error %d)\n", result);
        return false;
    }

    vkGetDeviceQueue(device_, queue_family_index_, 0, &graphics_queue_);
    present_queue_ = graphics_queue_;

    return true;
}

// ── Swapchain ───────────────────────────────────────────────────
// Ring buffer of images. While display shows image A, we render to B.

bool RendererImpl::create_swapchain() {
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device_, surface_, &capabilities);

    VkSurfaceFormatKHR format = choose_surface_format();
    VkPresentModeKHR present_mode = choose_present_mode();
    VkExtent2D extent = choose_extent();

    uint32_t image_count = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount) {
        image_count = capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    create_info.surface = surface_;
    create_info.minImageCount = image_count;
    create_info.imageFormat = format.format;
    create_info.imageColorSpace = format.colorSpace;
    create_info.imageExtent = extent;
    create_info.imageArrayLayers = 1;
    create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    create_info.preTransform = capabilities.currentTransform;
    create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    create_info.presentMode = present_mode;
    create_info.clipped = VK_TRUE;
    create_info.oldSwapchain = VK_NULL_HANDLE;

    VkResult result = vkCreateSwapchainKHR(device_, &create_info, nullptr, &swapchain_);
    if (result != VK_SUCCESS) {
        std::printf("[Kuma] Failed to create swapchain (error %d)\n", result);
        return false;
    }

    swapchain_format_ = format.format;
    swapchain_extent_ = extent;

    vkGetSwapchainImagesKHR(device_, swapchain_, &image_count, nullptr);
    swapchain_images_.resize(image_count);
    vkGetSwapchainImagesKHR(device_, swapchain_, &image_count, swapchain_images_.data());

    swapchain_image_views_.resize(image_count);
    for (uint32_t i = 0; i < image_count; i++) {
        VkImageViewCreateInfo view_info{};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = swapchain_images_[i];
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = swapchain_format_;
        view_info.components = {
            VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY
        };
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.baseMipLevel = 0;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount = 1;

        result = vkCreateImageView(device_, &view_info, nullptr, &swapchain_image_views_[i]);
        if (result != VK_SUCCESS) {
            std::printf("[Kuma] Failed to create image view %u\n", i);
            return false;
        }
    }

    std::printf("[Kuma] Swapchain created: %ux%u (%u images)\n",
        extent.width, extent.height, image_count);
    return true;
}

void RendererImpl::destroy_swapchain() {
    for (auto fb : framebuffers_) {
        vkDestroyFramebuffer(device_, fb, nullptr);
    }
    framebuffers_.clear();

    for (auto view : swapchain_image_views_) {
        vkDestroyImageView(device_, view, nullptr);
    }
    swapchain_image_views_.clear();
    swapchain_images_.clear();

    for (auto sem : render_finished_semaphores_) {
        vkDestroySemaphore(device_, sem, nullptr);
    }
    render_finished_semaphores_.clear();

    if (swapchain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
}

bool RendererImpl::recreate_swapchain() {
    vkDeviceWaitIdle(device_);
    destroy_swapchain();

    if (!create_swapchain())    return false;
    if (!create_framebuffers()) return false;

    // Recreate render-finished semaphores for new swapchain image count
    uint32_t image_count = static_cast<uint32_t>(swapchain_images_.size());
    render_finished_semaphores_.resize(image_count);
    VkSemaphoreCreateInfo sem_info{};
    sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (uint32_t i = 0; i < image_count; i++) {
        vkCreateSemaphore(device_, &sem_info, nullptr, &render_finished_semaphores_[i]);
    }

    return true;
}

// ── Render Pass ─────────────────────────────────────────────────
// Describes WHAT we render to and HOW (load/store operations).

bool RendererImpl::create_render_pass() {
    VkAttachmentDescription color_attachment{};
    color_attachment.format = swapchain_format_;
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference color_ref{};
    color_ref.attachment = 0;
    color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_ref;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    create_info.attachmentCount = 1;
    create_info.pAttachments = &color_attachment;
    create_info.subpassCount = 1;
    create_info.pSubpasses = &subpass;
    create_info.dependencyCount = 1;
    create_info.pDependencies = &dependency;

    VkResult result = vkCreateRenderPass(device_, &create_info, nullptr, &render_pass_);
    if (result != VK_SUCCESS) {
        std::printf("[Kuma] Failed to create render pass\n");
        return false;
    }

    return true;
}

// ── Graphics Pipeline ──────────────────────────────────────────
// The pipeline bundles everything needed to go from vertices to pixels:
// shaders, vertex format, rasterization, blending, viewport, etc.
// Once created, it's immutable — changing any setting means a new pipeline.

bool RendererImpl::create_graphics_pipeline() {
    // ── 1. Load compiled shaders ────────────────────────────────
    auto vert_code = read_binary_file("shaders/quad.vert.spv");
    auto frag_code = read_binary_file("shaders/quad.frag.spv");

    if (vert_code.empty() || frag_code.empty()) {
        std::printf("[Kuma] Failed to load shader files\n");
        return false;
    }

    VkShaderModule vert_module = create_shader_module(vert_code);
    VkShaderModule frag_module = create_shader_module(frag_code);

    if (vert_module == VK_NULL_HANDLE || frag_module == VK_NULL_HANDLE) {
        return false;
    }

    // ── 2. Shader stages ────────────────────────────────────────
    // Tell the pipeline which shaders to use and at which stage.

    VkPipelineShaderStageCreateInfo vert_stage{};
    vert_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vert_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vert_stage.module = vert_module;
    vert_stage.pName = "main";   // entry point function name in the shader

    VkPipelineShaderStageCreateInfo frag_stage{};
    frag_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    frag_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    frag_stage.module = frag_module;
    frag_stage.pName = "main";

    std::array<VkPipelineShaderStageCreateInfo, 2> shader_stages = {
        vert_stage, frag_stage
    };

    // ── 3. Vertex input ─────────────────────────────────────────
    // Describes the layout of vertex data: how big is each vertex (stride),
    // and where each attribute lives within that vertex (offset + format).

    VkVertexInputBindingDescription binding_desc{};
    binding_desc.binding = 0;                         // binding index (we only have one)
    binding_desc.stride = sizeof(Vertex);             // 20 bytes per vertex
    binding_desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;  // advance per vertex (not per instance)

    std::array<VkVertexInputAttributeDescription, 2> attr_descs{};

    // location 0 → position (vec2 = 2 floats)
    attr_descs[0].binding = 0;
    attr_descs[0].location = 0;
    attr_descs[0].format = VK_FORMAT_R32G32_SFLOAT;         // vec2
    attr_descs[0].offset = offsetof(Vertex, position);      // 0 bytes in

    // location 1 → uv (vec2 = 2 floats)
    attr_descs[1].binding = 0;
    attr_descs[1].location = 1;
    attr_descs[1].format = VK_FORMAT_R32G32_SFLOAT;          // vec2
    attr_descs[1].offset = offsetof(Vertex, uv);             // 8 bytes in

    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount = 1;
    vertex_input.pVertexBindingDescriptions = &binding_desc;
    vertex_input.vertexAttributeDescriptionCount = static_cast<uint32_t>(attr_descs.size());
    vertex_input.pVertexAttributeDescriptions = attr_descs.data();

    // ── 4. Input assembly ───────────────────────────────────────
    // How to interpret the vertices: as triangles, lines, points, etc.

    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly.primitiveRestartEnable = VK_FALSE;

    // ── 5. Dynamic state ────────────────────────────────────────
    // Viewport and scissor will be set per-frame (not baked into pipeline).
    // This means we don't need to recreate the pipeline on window resize.

    std::array<VkDynamicState, 2> dynamic_states = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
    dynamic_state.pDynamicStates = dynamic_states.data();

    // We still declare that we have 1 viewport and 1 scissor, but their
    // actual values come from vkCmdSetViewport/vkCmdSetScissor at draw time.
    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;

    // ── 6. Rasterizer ───────────────────────────────────────────
    // Turns triangles into fragments (candidate pixels).

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;         // don't clamp depth (discard instead)
    rasterizer.rasterizerDiscardEnable = VK_FALSE;   // actually rasterize (VK_TRUE = skip)
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;   // fill triangles (LINE = wireframe)
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;     // cull back faces
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;  // clockwise = front
    rasterizer.depthBiasEnable = VK_FALSE;

    // ── 7. Multisampling ────────────────────────────────────────
    // Anti-aliasing. Disabled for now (1 sample per pixel).

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // ── 8. Color blending ───────────────────────────────────────
    // How to combine the fragment shader's output with what's already
    // in the framebuffer. We just overwrite (no transparency).

    VkPipelineColorBlendAttachmentState blend_attachment{};
    blend_attachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blend_attachment.blendEnable = VK_FALSE;   // no blending — just write the color

    VkPipelineColorBlendStateCreateInfo color_blending{};
    color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.logicOpEnable = VK_FALSE;
    color_blending.attachmentCount = 1;
    color_blending.pAttachments = &blend_attachment;

    // ── 9. Descriptor set layout + Pipeline layout ────────────────
    // The descriptor set layout tells Vulkan what kind of resources
    // the shader expects. We have one: a combined image sampler (texture).

    VkDescriptorSetLayoutBinding sampler_binding{};
    sampler_binding.binding = 0;                                    // binding 0 in the shader
    sampler_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sampler_binding.descriptorCount = 1;                            // one texture
    sampler_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;      // used in fragment shader

    VkDescriptorSetLayoutCreateInfo layout_binding_info{};
    layout_binding_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_binding_info.bindingCount = 1;
    layout_binding_info.pBindings = &sampler_binding;

    if (vkCreateDescriptorSetLayout(device_, &layout_binding_info, nullptr,
            &descriptor_set_layout_) != VK_SUCCESS) {
        std::printf("[Kuma] Failed to create descriptor set layout\n");
        vkDestroyShaderModule(device_, vert_module, nullptr);
        vkDestroyShaderModule(device_, frag_module, nullptr);
        return false;
    }

    // Pipeline layout now references our descriptor set layout.
    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount = 1;
    layout_info.pSetLayouts = &descriptor_set_layout_;

    VkResult result = vkCreatePipelineLayout(device_, &layout_info, nullptr, &pipeline_layout_);
    if (result != VK_SUCCESS) {
        std::printf("[Kuma] Failed to create pipeline layout\n");
        vkDestroyShaderModule(device_, vert_module, nullptr);
        vkDestroyShaderModule(device_, frag_module, nullptr);
        return false;
    }

    // ── 10. Create the pipeline ─────────────────────────────────
    // Finally, bundle everything into one immutable pipeline object.

    VkGraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = static_cast<uint32_t>(shader_stages.size());
    pipeline_info.pStages = shader_stages.data();
    pipeline_info.pVertexInputState = &vertex_input;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState = &multisampling;
    pipeline_info.pDepthStencilState = nullptr;       // no depth testing yet
    pipeline_info.pColorBlendState = &color_blending;
    pipeline_info.pDynamicState = &dynamic_state;
    pipeline_info.layout = pipeline_layout_;
    pipeline_info.renderPass = render_pass_;
    pipeline_info.subpass = 0;                        // index of our subpass

    result = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE,
        1, &pipeline_info, nullptr, &graphics_pipeline_);

    // Shader modules are no longer needed — the pipeline has its own copy
    vkDestroyShaderModule(device_, vert_module, nullptr);
    vkDestroyShaderModule(device_, frag_module, nullptr);

    if (result != VK_SUCCESS) {
        std::printf("[Kuma] Failed to create graphics pipeline (error %d)\n", result);
        return false;
    }

    std::printf("[Kuma] Graphics pipeline created\n");
    return true;
}

// ── Vertex Buffer ──────────────────────────────────────────────
// Upload quad vertex data (position + UV) to GPU-accessible memory.

bool RendererImpl::create_vertex_buffer() {
    // A quad: 4 corners with position and UV coordinates.
    //
    //  v0 (-0.5,-0.5)──────v1 (0.5,-0.5)
    //   │  uv(0,0)           uv(1,0)  │
    //   │                              │
    //   │                              │
    //  v2 (-0.5, 0.5)──────v3 (0.5, 0.5)
    //      uv(0,1)           uv(1,1)

    const std::array<Vertex, 4> vertices = {{
        {{-0.5f, -0.5f}, {0.0f, 0.0f}},   // top-left
        {{ 0.5f, -0.5f}, {1.0f, 0.0f}},   // top-right
        {{-0.5f,  0.5f}, {0.0f, 1.0f}},   // bottom-left
        {{ 0.5f,  0.5f}, {1.0f, 1.0f}},   // bottom-right
    }};

    VkDeviceSize buffer_size = sizeof(Vertex) * vertices.size();

    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = buffer_size;
    buffer_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult result = vkCreateBuffer(device_, &buffer_info, nullptr, &vertex_buffer_);
    if (result != VK_SUCCESS) {
        std::printf("[Kuma] Failed to create vertex buffer\n");
        return false;
    }

    VkMemoryRequirements mem_reqs;
    vkGetBufferMemoryRequirements(device_, vertex_buffer_, &mem_reqs);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = find_memory_type(mem_reqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    result = vkAllocateMemory(device_, &alloc_info, nullptr, &vertex_buffer_memory_);
    if (result != VK_SUCCESS) {
        std::printf("[Kuma] Failed to allocate vertex buffer memory\n");
        return false;
    }

    vkBindBufferMemory(device_, vertex_buffer_, vertex_buffer_memory_, 0);

    void* data = nullptr;
    vkMapMemory(device_, vertex_buffer_memory_, 0, buffer_size, 0, &data);
    std::memcpy(data, vertices.data(), buffer_size);
    vkUnmapMemory(device_, vertex_buffer_memory_);

    std::printf("[Kuma] Vertex buffer created (%zu bytes, %zu vertices)\n",
        static_cast<size_t>(buffer_size), vertices.size());
    return true;
}

// ── Index Buffer ───────────────────────────────────────────────
// Indices tell the GPU which vertices form each triangle.
// A quad = 2 triangles = 6 indices, but only 4 unique vertices.

bool RendererImpl::create_index_buffer() {
    const std::array<uint16_t, 6> indices = {{
        0, 1, 2,    // first triangle  (top-left, top-right, bottom-left)
        2, 1, 3     // second triangle (bottom-left, top-right, bottom-right)
    }};

    index_count_ = static_cast<uint32_t>(indices.size());
    VkDeviceSize buffer_size = sizeof(uint16_t) * indices.size();

    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = buffer_size;
    buffer_info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult result = vkCreateBuffer(device_, &buffer_info, nullptr, &index_buffer_);
    if (result != VK_SUCCESS) {
        std::printf("[Kuma] Failed to create index buffer\n");
        return false;
    }

    VkMemoryRequirements mem_reqs;
    vkGetBufferMemoryRequirements(device_, index_buffer_, &mem_reqs);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = find_memory_type(mem_reqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    result = vkAllocateMemory(device_, &alloc_info, nullptr, &index_buffer_memory_);
    if (result != VK_SUCCESS) {
        std::printf("[Kuma] Failed to allocate index buffer memory\n");
        return false;
    }

    vkBindBufferMemory(device_, index_buffer_, index_buffer_memory_, 0);

    void* data = nullptr;
    vkMapMemory(device_, index_buffer_memory_, 0, buffer_size, 0, &data);
    std::memcpy(data, indices.data(), buffer_size);
    vkUnmapMemory(device_, index_buffer_memory_);

    return true;
}

// ── Single-use Command Helpers ─────────────────────────────────
// Some operations (like copying data to GPU images) need a one-shot
// command buffer. These helpers create one, let you record, then submit
// and wait for completion.

VkCommandBuffer RendererImpl::begin_single_command() const {
    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = command_pool_;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device_, &alloc_info, &cmd);

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin_info);

    return cmd;
}

void RendererImpl::end_single_command(VkCommandBuffer cmd) const {
    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &cmd;

    vkQueueSubmit(graphics_queue_, 1, &submit_info, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphics_queue_);

    vkFreeCommandBuffers(device_, command_pool_, 1, &cmd);
}

// ── Image Layout Transitions ───────────────────────────────────
// GPU images must be in the right "layout" for each operation.
// This function inserts a pipeline barrier to transition between layouts.

void RendererImpl::transition_image_layout(VkImage image,
    VkImageLayout old_layout, VkImageLayout new_layout)
{
    VkCommandBuffer cmd = begin_single_command();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags src_stage;
    VkPipelineStageFlags dst_stage;

    if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED &&
        new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        // Transition for copying data into the image
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
               new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        // Transition for reading in a shader (after copy is done)
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        std::printf("[Kuma] Unsupported layout transition\n");
        end_single_command(cmd);
        return;
    }

    vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0,
        0, nullptr, 0, nullptr, 1, &barrier);

    end_single_command(cmd);
}

// ── Buffer-to-Image Copy ───────────────────────────────────────

void RendererImpl::copy_buffer_to_image(VkBuffer buffer, VkImage image,
    uint32_t width, uint32_t height)
{
    VkCommandBuffer cmd = begin_single_command();

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;    // tightly packed
    region.bufferImageHeight = 0;  // tightly packed
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};

    vkCmdCopyBufferToImage(cmd, buffer, image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    end_single_command(cmd);
}

// ── Texture ────────────────────────────────────────────────────
// Creates a checkerboard texture procedurally: no external file needed.
// This demonstrates the full texture pipeline:
//   pixels → staging buffer → GPU image → image view → sampler

bool RendererImpl::create_texture() {
    // Generate a checkerboard pattern (8x8 tiles on a 64x64 image)
    constexpr uint32_t TEX_WIDTH = 64;
    constexpr uint32_t TEX_HEIGHT = 64;
    constexpr uint32_t TILE_SIZE = 8;

    std::array<uint8_t, TEX_WIDTH * TEX_HEIGHT * 4> pixels;
    for (uint32_t y = 0; y < TEX_HEIGHT; y++) {
        for (uint32_t x = 0; x < TEX_WIDTH; x++) {
            uint32_t i = (y * TEX_WIDTH + x) * 4;
            bool white = ((x / TILE_SIZE) + (y / TILE_SIZE)) % 2 == 0;
            pixels[i + 0] = white ? 255 : 50;   // R
            pixels[i + 1] = white ? 255 : 50;   // G
            pixels[i + 2] = white ? 255 : 50;   // B
            pixels[i + 3] = 255;                 // A (fully opaque)
        }
    }

    VkDeviceSize image_size = TEX_WIDTH * TEX_HEIGHT * 4;

    // ── Staging buffer (CPU-visible temp storage) ───────────────
    VkBuffer staging_buffer;
    VkDeviceMemory staging_memory;

    VkBufferCreateInfo staging_info{};
    staging_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    staging_info.size = image_size;
    staging_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    staging_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    vkCreateBuffer(device_, &staging_info, nullptr, &staging_buffer);

    VkMemoryRequirements mem_reqs;
    vkGetBufferMemoryRequirements(device_, staging_buffer, &mem_reqs);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = find_memory_type(mem_reqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    vkAllocateMemory(device_, &alloc_info, nullptr, &staging_memory);
    vkBindBufferMemory(device_, staging_buffer, staging_memory, 0);

    // Copy pixel data into the staging buffer
    void* data;
    vkMapMemory(device_, staging_memory, 0, image_size, 0, &data);
    std::memcpy(data, pixels.data(), image_size);
    vkUnmapMemory(device_, staging_memory);

    // ── GPU image (DEVICE_LOCAL — fast VRAM) ────────────────────
    VkImageCreateInfo img_info{};
    img_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    img_info.imageType = VK_IMAGE_TYPE_2D;
    img_info.extent = {TEX_WIDTH, TEX_HEIGHT, 1};
    img_info.mipLevels = 1;
    img_info.arrayLayers = 1;
    img_info.format = VK_FORMAT_R8G8B8A8_SRGB;
    img_info.tiling = VK_IMAGE_TILING_OPTIMAL;       // GPU-optimized layout
    img_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    img_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    img_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    img_info.samples = VK_SAMPLE_COUNT_1_BIT;

    if (vkCreateImage(device_, &img_info, nullptr, &texture_image_) != VK_SUCCESS) {
        std::printf("[Kuma] Failed to create texture image\n");
        vkDestroyBuffer(device_, staging_buffer, nullptr);
        vkFreeMemory(device_, staging_memory, nullptr);
        return false;
    }

    vkGetImageMemoryRequirements(device_, texture_image_, &mem_reqs);

    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = find_memory_type(mem_reqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    vkAllocateMemory(device_, &alloc_info, nullptr, &texture_image_memory_);
    vkBindImageMemory(device_, texture_image_, texture_image_memory_, 0);

    // ── Transfer: staging buffer → GPU image ────────────────────
    transition_image_layout(texture_image_,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    copy_buffer_to_image(staging_buffer, texture_image_, TEX_WIDTH, TEX_HEIGHT);

    transition_image_layout(texture_image_,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // Staging buffer is no longer needed
    vkDestroyBuffer(device_, staging_buffer, nullptr);
    vkFreeMemory(device_, staging_memory, nullptr);

    // ── Image view ──────────────────────────────────────────────
    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = texture_image_;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = VK_FORMAT_R8G8B8A8_SRGB;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device_, &view_info, nullptr, &texture_image_view_) != VK_SUCCESS) {
        std::printf("[Kuma] Failed to create texture image view\n");
        return false;
    }

    // ── Sampler (filtering + wrapping rules) ────────────────────
    VkSamplerCreateInfo sampler_info{};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = VK_FILTER_NEAREST;     // when texture is magnified: sharp pixels
    sampler_info.minFilter = VK_FILTER_NEAREST;     // when texture is minified: sharp pixels
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;  // tile horizontally
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;  // tile vertically
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.anisotropyEnable = VK_FALSE;
    sampler_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    sampler_info.unnormalizedCoordinates = VK_FALSE;   // use 0.0–1.0 UV range
    sampler_info.compareEnable = VK_FALSE;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

    if (vkCreateSampler(device_, &sampler_info, nullptr, &texture_sampler_) != VK_SUCCESS) {
        std::printf("[Kuma] Failed to create texture sampler\n");
        return false;
    }

    std::printf("[Kuma] Texture created (%ux%u checkerboard)\n", TEX_WIDTH, TEX_HEIGHT);
    return true;
}

// ── Descriptor Sets ────────────────────────────────────────────
// Descriptor sets bind actual resources (our texture) to shader bindings.
// We need one per frame-in-flight so the GPU can read from one while
// we update another.

bool RendererImpl::create_descriptor_sets() {
    // ── Pool ────────────────────────────────────────────────────
    // A pool allocates descriptor sets (like a command pool for command buffers).
    VkDescriptorPoolSize pool_size{};
    pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_size.descriptorCount = MAX_FRAMES_IN_FLIGHT;

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &pool_size;
    pool_info.maxSets = MAX_FRAMES_IN_FLIGHT;

    if (vkCreateDescriptorPool(device_, &pool_info, nullptr, &descriptor_pool_) != VK_SUCCESS) {
        std::printf("[Kuma] Failed to create descriptor pool\n");
        return false;
    }

    // ── Allocate sets ───────────────────────────────────────────
    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, descriptor_set_layout_);

    VkDescriptorSetAllocateInfo set_alloc_info{};
    set_alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    set_alloc_info.descriptorPool = descriptor_pool_;
    set_alloc_info.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
    set_alloc_info.pSetLayouts = layouts.data();

    descriptor_sets_.resize(MAX_FRAMES_IN_FLIGHT);
    if (vkAllocateDescriptorSets(device_, &set_alloc_info, descriptor_sets_.data()) != VK_SUCCESS) {
        std::printf("[Kuma] Failed to allocate descriptor sets\n");
        return false;
    }

    // ── Write (point each set to our texture + sampler) ─────────
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorImageInfo image_info{};
        image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        image_info.imageView = texture_image_view_;
        image_info.sampler = texture_sampler_;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = descriptor_sets_[i];
        write.dstBinding = 0;
        write.dstArrayElement = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &image_info;

        vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    }

    std::printf("[Kuma] Descriptor sets created\n");
    return true;
}

// ── Framebuffers ────────────────────────────────────────────────
// Each framebuffer binds a swapchain image view to the render pass.

bool RendererImpl::create_framebuffers() {
    framebuffers_.resize(swapchain_image_views_.size());

    for (size_t i = 0; i < swapchain_image_views_.size(); i++) {
        VkFramebufferCreateInfo fb_info{};
        fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb_info.renderPass = render_pass_;
        fb_info.attachmentCount = 1;
        fb_info.pAttachments = &swapchain_image_views_[i];
        fb_info.width = swapchain_extent_.width;
        fb_info.height = swapchain_extent_.height;
        fb_info.layers = 1;

        VkResult result = vkCreateFramebuffer(device_, &fb_info, nullptr, &framebuffers_[i]);
        if (result != VK_SUCCESS) {
            std::printf("[Kuma] Failed to create framebuffer %zu\n", i);
            return false;
        }
    }

    return true;
}

// ── Command Pool + Buffers ──────────────────────────────────────

bool RendererImpl::create_command_pool() {
    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = queue_family_index_;

    VkResult result = vkCreateCommandPool(device_, &pool_info, nullptr, &command_pool_);
    if (result != VK_SUCCESS) {
        std::printf("[Kuma] Failed to create command pool\n");
        return false;
    }
    return true;
}

bool RendererImpl::create_command_buffers() {
    command_buffers_.resize(MAX_FRAMES_IN_FLIGHT);

    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = command_pool_;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = MAX_FRAMES_IN_FLIGHT;

    VkResult result = vkAllocateCommandBuffers(device_, &alloc_info, command_buffers_.data());
    if (result != VK_SUCCESS) {
        std::printf("[Kuma] Failed to allocate command buffers\n");
        return false;
    }
    return true;
}

// ── Sync Objects ────────────────────────────────────────────────
// Semaphores: GPU-to-GPU sync. Fences: GPU-to-CPU sync.

bool RendererImpl::create_sync_objects() {
    uint32_t image_count = static_cast<uint32_t>(swapchain_images_.size());

    image_available_semaphores_.resize(MAX_FRAMES_IN_FLIGHT);
    render_finished_semaphores_.resize(image_count);
    in_flight_fences_.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo sem_info{};
    sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateSemaphore(device_, &sem_info, nullptr, &image_available_semaphores_[i]) != VK_SUCCESS ||
            vkCreateFence(device_, &fence_info, nullptr, &in_flight_fences_[i]) != VK_SUCCESS) {
            std::printf("[Kuma] Failed to create sync objects\n");
            return false;
        }
    }

    for (uint32_t i = 0; i < image_count; i++) {
        if (vkCreateSemaphore(device_, &sem_info, nullptr, &render_finished_semaphores_[i]) != VK_SUCCESS) {
            std::printf("[Kuma] Failed to create render finished semaphore\n");
            return false;
        }
    }

    return true;
}

// ── Per-Frame Rendering ─────────────────────────────────────────

bool RendererImpl::begin_frame() {
    // Wait for the previous frame using this slot to finish
    vkWaitForFences(device_, 1, &in_flight_fences_[current_frame_],
        VK_TRUE, std::numeric_limits<uint64_t>::max());

    // Acquire the next swapchain image
    VkResult result = vkAcquireNextImageKHR(device_, swapchain_,
        std::numeric_limits<uint64_t>::max(),
        image_available_semaphores_[current_frame_],
        VK_NULL_HANDLE, &current_image_index_);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreate_swapchain();
        return false;
    }

    vkResetFences(device_, 1, &in_flight_fences_[current_frame_]);

    // Record commands
    VkCommandBuffer cmd = command_buffers_[current_frame_];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &begin_info);

    // Dark navy blue clear color
    VkClearValue clear_color = {{{0.05f, 0.05f, 0.12f, 1.0f}}};

    VkRenderPassBeginInfo rp_info{};
    rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp_info.renderPass = render_pass_;
    rp_info.framebuffer = framebuffers_[current_image_index_];
    rp_info.renderArea.offset = {0, 0};
    rp_info.renderArea.extent = swapchain_extent_;
    rp_info.clearValueCount = 1;
    rp_info.pClearValues = &clear_color;

    vkCmdBeginRenderPass(cmd, &rp_info, VK_SUBPASS_CONTENTS_INLINE);

    // ── Draw commands ───────────────────────────────────────────

    // Bind the graphics pipeline — "use these shaders and this configuration"
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline_);

    // Set viewport — maps clip space (-1 to +1) to pixel coordinates.
    // The minDepth/maxDepth range is the depth buffer range (0 to 1).
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(swapchain_extent_.width);
    viewport.height = static_cast<float>(swapchain_extent_.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    // Set scissor — clips pixels outside this rectangle.
    // We set it to the full swapchain extent (no clipping).
    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapchain_extent_;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Bind the vertex buffer — "here's the quad vertex data"
    VkBuffer buffers[] = {vertex_buffer_};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, buffers, offsets);

    // Bind the index buffer — "here's which vertices form each triangle"
    vkCmdBindIndexBuffer(cmd, index_buffer_, 0, VK_INDEX_TYPE_UINT16);

    // Bind the descriptor set — "here's the texture for the shader"
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipeline_layout_, 0, 1, &descriptor_sets_[current_frame_], 0, nullptr);

    // Draw! 6 indices (2 triangles), 1 instance.
    vkCmdDrawIndexed(cmd, index_count_, 1, 0, 0, 0);

    return true;
}

void RendererImpl::end_frame() {
    VkCommandBuffer cmd = command_buffers_[current_frame_];

    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);

    // Submit to GPU
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = &image_available_semaphores_[current_frame_];
    submit_info.pWaitDstStageMask = &wait_stage;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &cmd;
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &render_finished_semaphores_[current_image_index_];

    vkQueueSubmit(graphics_queue_, 1, &submit_info, in_flight_fences_[current_frame_]);

    // Present to screen
    VkPresentInfoKHR present_info{};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &render_finished_semaphores_[current_image_index_];
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &swapchain_;
    present_info.pImageIndices = &current_image_index_;

    VkResult result = vkQueuePresentKHR(present_queue_, &present_info);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebuffer_resized_) {
        framebuffer_resized_ = false;
        recreate_swapchain();
    }

    current_frame_ = (current_frame_ + 1) % MAX_FRAMES_IN_FLIGHT;
}

void RendererImpl::on_resize(int32_t width, int32_t height) {
    width_ = width;
    height_ = height;
    framebuffer_resized_ = true;
}

// ── Helpers ─────────────────────────────────────────────────────

// Creates a VkShaderModule from compiled SPIR-V bytecode.
// The module is a thin wrapper — Vulkan copies the data internally,
// so the source vector can be freed after this call.

VkShaderModule RendererImpl::create_shader_module(const std::vector<char>& code) const {
    VkShaderModuleCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create_info.codeSize = code.size();
    // SPIR-V expects uint32_t* but we have char*. reinterpret_cast is safe
    // here because SPIR-V data is always 4-byte aligned (the spec requires it).
    create_info.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shader_module = VK_NULL_HANDLE;
    VkResult result = vkCreateShaderModule(device_, &create_info, nullptr, &shader_module);
    if (result != VK_SUCCESS) {
        std::printf("[Kuma] Failed to create shader module\n");
        return VK_NULL_HANDLE;
    }

    return shader_module;
}

// Finds a GPU memory type that satisfies both the buffer's requirements
// and our desired properties (e.g., host-visible for CPU access).

uint32_t RendererImpl::find_memory_type(uint32_t type_filter,
    VkMemoryPropertyFlags properties) const
{
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(physical_device_, &mem_props);

    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
        // type_filter is a bitmask — bit i is set if memory type i is suitable
        bool type_suitable = (type_filter & (1 << i)) != 0;
        // Check that this memory type has ALL the properties we need
        bool has_properties = (mem_props.memoryTypes[i].propertyFlags & properties) == properties;

        if (type_suitable && has_properties) {
            return i;
        }
    }

    std::printf("[Kuma] Failed to find suitable memory type\n");
    return 0;
}

VkSurfaceFormatKHR RendererImpl::choose_surface_format() const {
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &count, formats.data());

    for (const auto& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }

    return formats[0];
}

VkPresentModeKHR RendererImpl::choose_present_mode() const {
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device_, surface_, &count, nullptr);
    std::vector<VkPresentModeKHR> modes(count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device_, surface_, &count, modes.data());

    for (const auto& mode : modes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return mode;
        }
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D RendererImpl::choose_extent() const {
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device_, surface_, &capabilities);

    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    }

    VkExtent2D extent = {
        static_cast<uint32_t>(width_),
        static_cast<uint32_t>(height_)
    };

    extent.width = std::clamp(extent.width,
        capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    extent.height = std::clamp(extent.height,
        capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

    return extent;
}

} // namespace kuma
