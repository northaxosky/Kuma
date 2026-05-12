#pragma once

// Private implementation header for the Debug Overlay module.
// Reachable from engine.cpp / renderer.cpp via the engine/src
// include path, NOT from public game code (which uses debug.h).
//
// Holds typed Vulkan + SDL function signatures so the public
// debug.h can stay free of those headers.

#include <kuma/debug.h>

#include <SDL3/SDL_events.h>
#include <vulkan/vulkan.h>

struct SDL_Window;

namespace kuma::debug {

// Bundle of Vulkan + SDL handles that ImGui needs to initialize.
// Populated by Renderer::imgui_init_context() and passed straight
// into kuma::debug::init() during engine startup.
struct InitContext {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    uint32_t queue_family = 0;
    VkQueue queue = VK_NULL_HANDLE;
    VkRenderPass render_pass = VK_NULL_HANDLE;
    uint32_t image_count = 0;     // swapchain image count - sizes ImGui's frame-in-flight bookkeeping
    uint32_t min_image_count = 2;
    SDL_Window* sdl_window = nullptr;
};

bool init(const InitContext& ctx);
void shutdown();

// Phase 1 (INPUT) hook: forwards each SDL event into ImGui so its
// internal mouse/keyboard state stays in sync with the OS.
void process_event(const SDL_Event& event);

// Called once per frame between Phase 2 (TIME) and Phase 3 (UPDATE).
// Updates internal stats (FPS smoothing, frame-time ring buffer)
// from kuma::time::delta(), checks the F3 hotkey, then opens the
// ImGui frame so user code can call ImGui::Begin/Text/End during
// UPDATE.
void new_frame();

// Called by RendererImpl::end_frame INSIDE the active render pass,
// after game draws but before vkCmdEndRenderPass. Calls
// ImGui::Render() and ImGui_ImplVulkan_RenderDrawData(cmd, ...).
// No-op if init() failed or hasn't run.
void render(VkCommandBuffer cmd);

}  // namespace kuma::debug
