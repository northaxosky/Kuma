#include <kuma/renderer.h>
#include <vulkan/vulkan.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <limits>
#include <vector>

namespace kuma {

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
    bool create_command_pool();
    bool create_command_buffers();
    bool create_sync_objects();

    void destroy_swapchain();
    bool recreate_swapchain();

    VkSurfaceFormatKHR choose_surface_format() const;
    VkPresentModeKHR choose_present_mode() const;
    VkExtent2D choose_extent() const;

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
    if (!create_swapchain())       return false;
    if (!create_render_pass())     return false;
    if (!create_framebuffers())    return false;
    if (!create_command_pool())    return false;
    if (!create_command_buffers()) return false;
    if (!create_sync_objects())    return false;

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
    for (auto sem : render_finished_semaphores_) {
        vkDestroySemaphore(device_, sem, nullptr);
    }

    vkDestroyCommandPool(device_, command_pool_, nullptr);
    destroy_swapchain();
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
