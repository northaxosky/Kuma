#pragma once

// ── Renderer Implementation Header ─────────────────────────────
// Shared by all renderer .cpp files. Defines the RendererImpl class
// and common types. Game code never sees this — only renderer.h.

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

namespace kuma {

// ── Vertex Data ─────────────────────────────────────────────────
//   layout(location = 0) in vec2 in_position  →  position (offset 0)
//   layout(location = 1) in vec2 in_uv        →  uv       (offset 8)

struct Vertex {
    float position[2];   // x, y  — clip space coordinates (-1 to +1)
    float uv[2];         // u, v  — texture coordinates (0 to 1)
};

// ── Texture ─────────────────────────────────────────────────────
// Bundles all Vulkan objects that make up a usable texture.
// Created together, destroyed together — a single unit of GPU state.

struct Texture {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    uint32_t width = 0;
    uint32_t height = 0;
};

// ── Mesh ────────────────────────────────────────────────────────
// Bundles the GPU buffers that make up a renderable piece of geometry.
// Vertex buffer holds per-vertex data (position, UV, etc.).
// Index buffer holds which vertices form each triangle.

struct Mesh {
    VkBuffer vertex_buffer = VK_NULL_HANDLE;
    VkDeviceMemory vertex_memory = VK_NULL_HANDLE;
    VkBuffer index_buffer = VK_NULL_HANDLE;
    VkDeviceMemory index_memory = VK_NULL_HANDLE;
    uint32_t index_count = 0;
};

// ── GPU Context ─────────────────────────────────────────────────
// The minimal set of Vulkan handles needed to create GPU resources.
// Shared between the renderer and the resource manager.

struct GpuContext {
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkQueue graphics_queue = VK_NULL_HANDLE;
};

// ── File I/O ────────────────────────────────────────────────────

std::vector<char> read_binary_file(const char* path);

// ── RendererImpl ────────────────────────────────────────────────
// All Vulkan state lives here, invisible to game code.
// Method implementations are spread across multiple .cpp files:
//   renderer.cpp   — init, shutdown, frame loop
//   device.cpp     — instance, debug messenger, surface, GPU, logical device
//   swapchain.cpp  — swapchain, render pass, framebuffers, format helpers
//   pipeline.cpp   — graphics pipeline, shader modules
//   resources.cpp  — buffers, textures, descriptors, command helpers

class RendererImpl {
public:
    bool init(const RendererConfig& config);
    void shutdown();
    bool begin_frame();
    void end_frame();
    void on_resize(int32_t width, int32_t height);
    GpuContext gpu_context() const;

    // Set resources loaded by the resource manager
    void set_texture(const Texture* texture);
    void set_mesh(const Mesh* mesh);

private:
    // ── device.cpp ──────────────────────────────────────────────
    bool create_instance();
    bool create_debug_messenger();
    bool create_surface();
    bool pick_physical_device();
    bool create_logical_device();

    // ── swapchain.cpp ───────────────────────────────────────────
    bool create_swapchain();
    void destroy_swapchain();
    bool recreate_swapchain();
    bool create_render_pass();
    bool create_framebuffers();
    VkSurfaceFormatKHR choose_surface_format() const;
    VkPresentModeKHR choose_present_mode() const;
    VkExtent2D choose_extent() const;

    // ── pipeline.cpp ────────────────────────────────────────────
    bool create_graphics_pipeline();
    VkShaderModule create_shader_module(const std::vector<char>& code) const;

    // ── resources.cpp ───────────────────────────────────────────
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
    uint32_t find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties) const;

    // ── State ───────────────────────────────────────────────────

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

    // Resources (owned by ResourceManager, renderer borrows pointers)
    const Mesh* mesh_ = nullptr;
    const Texture* texture_ = nullptr;

    // Descriptors
    VkDescriptorSetLayout descriptor_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptor_sets_;

    // Commands
    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> command_buffers_;

    // Synchronization
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
    std::vector<VkSemaphore> image_available_semaphores_;
    std::vector<VkSemaphore> render_finished_semaphores_;
    std::vector<VkFence> in_flight_fences_;
    uint32_t current_frame_ = 0;
    uint32_t current_image_index_ = 0;

    bool framebuffer_resized_ = false;
};

} // namespace kuma
