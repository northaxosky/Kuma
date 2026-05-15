#pragma once

// ── Renderer Implementation Header ─────────────────────────────
// Shared by all renderer .cpp files. Defines the RendererImpl class
// and common types. Game code never sees this — only renderer.h.

#include <kuma/log.h>
#include <kuma/renderer.h>

#include "core/debug_internal.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <limits>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>

namespace kuma {

// ── Vertex Data ─────────────────────────────────────────────────
//   layout(location = 0) in vec2 in_position  →  position (offset 0)
//   layout(location = 1) in vec2 in_uv        →  uv       (offset 8)

// ── Vertex ──────────────────────────────────────────────────────
// Per-vertex layout consumed by the Vulkan pipeline. MUST match
// kuma::asset_format::Vertex byte for byte; the bake tool writes
// vertex arrays in this exact layout straight to .kmesh files.
struct Vertex {
    float pos[3];      // x, y, z (model space)
    float uv[2];       // u, v texture coordinates (V flipped at bake time)
    float normal[3];   // unit normal (currently unused by the shader,
                       //  reserved for lighting work)
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

// ── Particle vertex data ────────────────────────────────────────
// Particle rendering uses two vertex bindings:
//
//   binding 0 = ParticleQuadVertex, per-vertex
//     The four corners of a unit quad. Same 4 vertices for every
//     particle ever drawn. The vertex shader extracts camera right
//     and up from the view matrix and projects the corner into
//     world space, so the actual quad position lives entirely in
//     instance data.
//
//   binding 1 = Renderer::ParticleInstance, per-instance
//     Defined publicly in renderer.h since particles:: needs to
//     build arrays of it. Layout MUST match the binding 1
//     declaration in particle.vert byte-for-byte.
struct ParticleQuadVertex {
    float corner[2];   // (-0.5, -0.5), (0.5, -0.5), (-0.5, 0.5), (0.5, 0.5)
};

static_assert(sizeof(Renderer::ParticleInstance) == 32,
              "Renderer::ParticleInstance is part of the GPU vertex layout - size changes need a shader update");

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

    // Build the bundle of Vulkan handles ImGui needs for init.
    // Cached on first call (assumed stable for the renderer's lifetime).
    debug::InitContext imgui_init_context() const;

    // Set resources loaded by the resource manager
    void set_texture(const Texture* texture);
    void set_mesh(const Mesh* mesh);
    void set_material(const Material* material);

    // Material descriptor allocator. Public so the renderer.cpp
    // wrapper can forward to it; called by ResourceManager via the
    // public Renderer API. slots is in fixed order (diffuse, normal,
    // metallic-roughness, occlusion, emissive); nullptr -> default
    // for that slot.
    VkDescriptorSet allocate_material_descriptor_set(const Texture* slots[5]);
    void free_material_descriptor_set(VkDescriptorSet set);

    // Pick which graphics pipeline subsequent draw() calls use.
    // 0 = textured (default), 1 = debug-normal visualizer.
    void set_pipeline(uint32_t index);

    void set_view_projection(const Mat4& view_projection);
    void set_model_matrix(const Mat4& model);
    void draw();

    // Particle path - see Renderer:: docs.
    uint32_t upload_particle_instances(const void* instances, uint32_t count);
    void     draw_particles(uint32_t upload_offset, uint32_t count,
                            const Mat4& view, const Mat4& view_projection);

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
    bool create_depth_resources();
    void destroy_depth_resources();
    VkSurfaceFormatKHR choose_surface_format() const;
    VkPresentModeKHR choose_present_mode() const;
    VkExtent2D choose_extent() const;

    // ── pipeline.cpp ────────────────────────────────────────────
    bool create_graphics_pipelines();
    VkPipeline build_pipeline(const char* vert_spv, const char* frag_spv,
                              const struct PipelineCreateOptions& opts);
    VkShaderModule create_shader_module(const std::vector<char>& code) const;

    // ── resources.cpp ───────────────────────────────────────────
    bool create_default_textures();
    bool create_material_descriptor_pool();
    void destroy_material_resources();
    bool create_particle_quad();
    bool create_particle_instance_buffers();
    void destroy_particle_resources();
    bool create_command_pool();
    bool create_command_buffers();
    bool create_sync_objects();
    bool upload_pixels_to_texture(const void* pixels, uint32_t width, uint32_t height,
                                  VkFormat format, Texture& out_texture);
    void copy_buffer_to_image(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
    void transition_image_layout(VkImage image, VkImageLayout old_layout, VkImageLayout new_layout);
    VkCommandBuffer begin_single_command() const;
    void end_single_command(VkCommandBuffer cmd) const;
    uint32_t find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties) const;

    // ── State ───────────────────────────────────────────────────

    // Config
    SDL_Window* window_ = nullptr;
    int32_t width_ = 0;
    int32_t height_ = 0;
    bool validation_enabled_ = true;
    PresentMode requested_present_mode_ = PresentMode::Vsync;

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

    // Depth attachment. Created alongside the swapchain at the same
    // resolution; the render pass clears it to 1.0 every frame and
    // discards the contents on store (depth is internal-only - never
    // sampled, never read back). Recreated when the swapchain is
    // recreated on resize.
    VkImage        depth_image_  = VK_NULL_HANDLE;
    VkDeviceMemory depth_memory_ = VK_NULL_HANDLE;
    VkImageView    depth_view_   = VK_NULL_HANDLE;
    VkFormat       depth_format_ = VK_FORMAT_D32_SFLOAT;

    // Graphics pipeline
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline graphics_pipeline_ = VK_NULL_HANDLE;          // textured (pipeline 0)
    VkPipeline debug_normal_pipeline_ = VK_NULL_HANDLE;      // debug normal viz (pipeline 1)
    VkPipeline transparent_pipeline_ = VK_NULL_HANDLE;       // particles (pipeline 2)
    uint32_t active_pipeline_index_ = 0;

    // Particle rendering scratch:
    //   quad vertex/index buffers - constant 4-vertex unit quad shared
    //     by every particle draw, allocated once at renderer init.
    //   instance ring buffer - per-frame-in-flight arena that emitter
    //     uploads append into. Reset at the start of each begin_frame
    //     once the corresponding fence is known complete, so any data
    //     a previous frame's draw is still reading stays untouched.
    VkBuffer       particle_quad_vertex_buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory particle_quad_vertex_memory_ = VK_NULL_HANDLE;
    VkBuffer       particle_quad_index_buffer_  = VK_NULL_HANDLE;
    VkDeviceMemory particle_quad_index_memory_  = VK_NULL_HANDLE;

    static constexpr uint32_t kParticleRingCapacity = 64 * 256;  // 64 emitters * 256 particles
    std::vector<VkBuffer>       particle_instance_buffers_;       // one per frame in flight
    std::vector<VkDeviceMemory> particle_instance_memory_;
    std::vector<void*>          particle_instance_mapped_;        // persistent host mapping
    uint32_t                    particle_instance_offset_ = 0;    // bytes used in current frame

    // Resources (owned by ResourceManager, renderer borrows pointers)
    const Mesh* mesh_ = nullptr;
    const Texture* texture_ = nullptr;

    // Camera matrix supplied by game code via set_view_projection.
    Mat4 view_projection_ = Mat4::identity();
    bool has_view_projection_ = false;

    // True between a successful begin_frame() and the matching
    // end_frame(). draw() and end_frame() check this so they no-op
    // safely when begin_frame failed (e.g. swapchain rebuild).
    bool frame_recording_ = false;

    // Model matrix for the next draw. Identity by default, so callers
    // that don't care about transforms still get sensible behavior.
    Mat4 model_ = Mat4::identity();

    // Descriptors. The pool sources one immutable descriptor set per
    // material at material-load time; sets are read-only for the GPU
    // and safe to reuse across frames in flight without per-frame
    // copies.
    VkDescriptorSetLayout descriptor_set_layout_  = VK_NULL_HANDLE;
    VkDescriptorPool      material_pool_          = VK_NULL_HANDLE;
    uint32_t              materials_allocated_    = 0;

    // Default 1x1 textures bound when a material doesn't supply a
    // particular slot. Indexed by binding (0=diffuse..4=emissive).
    // Owned by the renderer so they exist before any user material.
    std::array<Texture, 5> default_textures_{};

    // Single-slot helper for the legacy set_texture compatibility
    // shim. Each set_texture call frees this and allocates a new
    // descriptor set wrapping the requested texture as diffuse, so
    // repeated calls don't leak pool capacity. The shim exists for
    // the engine's boot-time texture binding; new code uses
    // set_material with a Material loaded from ResourceManager.
    VkDescriptorSet boot_texture_set_ = VK_NULL_HANDLE;

    // Currently-bound material descriptor set. draw() binds this; if
    // null, the draw is skipped.
    VkDescriptorSet active_material_set_ = VK_NULL_HANDLE;

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

}  // namespace kuma
