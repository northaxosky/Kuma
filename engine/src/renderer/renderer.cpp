// ── Renderer Core ───────────────────────────────────────────────
// Public Renderer wrapper, init/shutdown orchestration, and the
// per-frame rendering loop. The thin entry point into the renderer.

#include <kuma/math.h>

#include "renderer_impl.h"

namespace kuma {

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

void Renderer::wait_idle() {
    vkDeviceWaitIdle(impl_->gpu_context().device);
}

void Renderer::on_resize(int32_t width, int32_t height) {
    impl_->on_resize(width, height);
}

void Renderer::set_mesh(const void* mesh) {
    impl_->set_mesh(static_cast<const Mesh*>(mesh));
}

void Renderer::set_pipeline(uint32_t index) {
    impl_->set_pipeline(index);
}

void Renderer::set_texture(const void* texture) {
    impl_->set_texture(static_cast<const Texture*>(texture));
}

void Renderer::set_material(const Material* material) {
    impl_->set_material(material);
}

void* Renderer::create_material_descriptor_set(const void* texture_slots[5]) {
    const Texture* slots[5] = {
        static_cast<const Texture*>(texture_slots[0]),
        static_cast<const Texture*>(texture_slots[1]),
        static_cast<const Texture*>(texture_slots[2]),
        static_cast<const Texture*>(texture_slots[3]),
        static_cast<const Texture*>(texture_slots[4]),
    };
    return impl_->allocate_material_descriptor_set(slots);
}

void Renderer::free_material_descriptor_set(void* descriptor_set) {
    impl_->free_material_descriptor_set(static_cast<VkDescriptorSet>(descriptor_set));
}

void Renderer::set_view_projection(const Mat4& view_projection) {
    impl_->set_view_projection(view_projection);
}

void Renderer::set_model_matrix(const Mat4& model) {
    impl_->set_model_matrix(model);
}

void Renderer::draw() {
    impl_->draw();
}

void* Renderer::gpu_context() {
    static GpuContext ctx = impl_->gpu_context();
    return &ctx;
}

void* Renderer::imgui_init_context() {
    static debug::InitContext ctx = impl_->imgui_init_context();
    return &ctx;
}

// ── RendererImpl Init/Shutdown ──────────────────────────────────

bool RendererImpl::init(const RendererConfig& config) {
    window_ = config.window;
    width_ = config.width;
    height_ = config.height;
    validation_enabled_ = config.enable_validation;
    requested_present_mode_ = config.present_mode;

    if (!create_instance())
        return false;
    if (!create_debug_messenger())
        return false;
    if (!create_surface())
        return false;
    if (!pick_physical_device())
        return false;
    if (!create_logical_device())
        return false;
    if (!create_swapchain())
        return false;
    if (!create_render_pass())
        return false;
    if (!create_graphics_pipelines())
        return false;
    if (!create_framebuffers())
        return false;
    if (!create_command_pool())
        return false;
    if (!create_command_buffers())
        return false;
    if (!create_sync_objects())
        return false;
    if (!create_default_textures())
        return false;
    if (!create_material_descriptor_pool())
        return false;

    kuma::log::info("Vulkan renderer initialized");
    return true;
}

void RendererImpl::shutdown() {
    if (device_ == VK_NULL_HANDLE)
        return;

    vkDeviceWaitIdle(device_);

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroySemaphore(device_, image_available_semaphores_[i], nullptr);
        vkDestroyFence(device_, in_flight_fences_[i], nullptr);
    }

    vkDestroyCommandPool(device_, command_pool_, nullptr);

    destroy_material_resources();

    // Mesh and texture are owned by ResourceManager — not destroyed here.

    destroy_swapchain();
    vkDestroyPipeline(device_, debug_normal_pipeline_, nullptr);
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
    kuma::log::info("Vulkan renderer shut down");
}

// ── Per-Frame Rendering ─────────────────────────────────────────

bool RendererImpl::begin_frame() {
    frame_recording_ = false;

    vkWaitForFences(device_, 1, &in_flight_fences_[current_frame_], VK_TRUE,
                    std::numeric_limits<uint64_t>::max());

    VkResult result = vkAcquireNextImageKHR(
        device_, swapchain_, std::numeric_limits<uint64_t>::max(),
        image_available_semaphores_[current_frame_], VK_NULL_HANDLE, &current_image_index_);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreate_swapchain();
        return false;
    }

    vkResetFences(device_, 1, &in_flight_fences_[current_frame_]);

    VkCommandBuffer cmd = command_buffers_[current_frame_];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &begin_info);

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

    // Per-frame state that doesn't change between draws: viewport,
    // scissor, descriptor sets. Per-mesh state (vertex/index binds)
    // and per-draw state (pipeline) live in draw() so the game can
    // alternate set_mesh() / set_pipeline() / draw() within a frame.

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(swapchain_extent_.width);
    viewport.height = static_cast<float>(swapchain_extent_.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapchain_extent_;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Descriptor sets are bound per-draw now (one set per material),
    // so begin_frame intentionally does NOT bind one. draw() picks
    // the active material set when there's geometry to render.

    frame_recording_ = true;
    return true;
}

void RendererImpl::draw() {
    if (!frame_recording_) return;  // begin_frame failed (e.g. swapchain rebuild) - safe no-op
    if (!mesh_) {
        // No mesh bound - silently skip. Calling draw() before any
        // set_mesh() is benign (sandbox can call draw() in a loop
        // that gates on having geometry to render).
        return;
    }

    VkCommandBuffer cmd = command_buffers_[current_frame_];

    // Pick which pipeline this draw uses. set_pipeline() validates
    // the index; this just maps it to the corresponding VkPipeline.
    VkPipeline pipeline =
        active_pipeline_index_ == 1 ? debug_normal_pipeline_ : graphics_pipeline_;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    // Bind the active material's descriptor set (5 sampler bindings).
    // The debug-normal pipeline ignores the textures but still needs
    // a valid set bound because the layout declares the bindings.
    if (active_material_set_ != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_, 0, 1,
                                &active_material_set_, 0, nullptr);
    }

    // Per-mesh state. Cheap on Vulkan; the driver fast-paths
    // re-binding the same buffer pointers between draws.
    VkBuffer buffers[] = {mesh_->vertex_buffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, buffers, offsets);
    vkCmdBindIndexBuffer(cmd, mesh_->index_buffer, 0, VK_INDEX_TYPE_UINT16);

    // projection * view * model: model transforms first, then view, then project
    Mat4 mvp = view_projection_ * model_;
    vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Mat4),
                       mvp.ptr());

    vkCmdDrawIndexed(cmd, mesh_->index_count, 1, 0, 0, 0);
}

void RendererImpl::end_frame() {
    if (!frame_recording_) return;  // begin_frame failed - nothing to submit
    frame_recording_ = false;

    VkCommandBuffer cmd = command_buffers_[current_frame_];

    // Debug overlay rendering must happen INSIDE the active render
    // pass, AFTER game draws but BEFORE we close the pass. ImGui
    // emits its own vkCmdDraw calls into our command buffer.
    debug::render(cmd);

    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);

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

GpuContext RendererImpl::gpu_context() const {
    return GpuContext{device_, physical_device_, command_pool_, graphics_queue_};
}

debug::InitContext RendererImpl::imgui_init_context() const {
    debug::InitContext ctx{};
    ctx.instance         = instance_;
    ctx.physical_device  = physical_device_;
    ctx.device           = device_;
    ctx.queue_family     = queue_family_index_;
    ctx.queue            = graphics_queue_;
    ctx.render_pass      = render_pass_;
    ctx.image_count      = static_cast<uint32_t>(swapchain_images_.size());
    ctx.min_image_count  = 2;
    ctx.sdl_window       = window_;
    return ctx;
}

void RendererImpl::set_mesh(const Mesh* mesh) {
    mesh_ = mesh;
}

void RendererImpl::set_texture(const Texture* texture) {
    texture_ = texture;

    // Free any previously-allocated boot descriptor set so repeated
    // set_texture calls don't leak pool capacity, and so we never
    // hand back a stale set if a Texture* gets reused after the
    // upstream cache evicts and re-inserts the same address (a real
    // hazard once scene unload or hot-reload lands).
    if (boot_texture_set_ != VK_NULL_HANDLE) {
        if (active_material_set_ == boot_texture_set_) {
            active_material_set_ = VK_NULL_HANDLE;
        }
        vkFreeDescriptorSets(device_, material_pool_, 1, &boot_texture_set_);
        boot_texture_set_ = VK_NULL_HANDLE;
        if (materials_allocated_ > 0) --materials_allocated_;
    }

    if (texture == nullptr) {
        active_material_set_ = VK_NULL_HANDLE;
        return;
    }

    // Wrap the texture in a one-off material descriptor set, using
    // the renderer's defaults for the other four slots.
    const Texture* slots[5] = {texture, nullptr, nullptr, nullptr, nullptr};
    boot_texture_set_ = allocate_material_descriptor_set(slots);
    active_material_set_ = boot_texture_set_;
}

void RendererImpl::set_material(const Material* material) {
    if (material == nullptr || material->descriptor_set == nullptr) {
        active_material_set_ = VK_NULL_HANDLE;
        return;
    }
    active_material_set_ = static_cast<VkDescriptorSet>(material->descriptor_set);
}

void RendererImpl::free_material_descriptor_set(VkDescriptorSet set) {
    if (set == VK_NULL_HANDLE) return;
    if (active_material_set_ == set) {
        active_material_set_ = VK_NULL_HANDLE;
    }
    vkFreeDescriptorSets(device_, material_pool_, 1, &set);
    if (materials_allocated_ > 0) --materials_allocated_;
}

void RendererImpl::set_view_projection(const Mat4& view_projection) {
    view_projection_ = view_projection;
    has_view_projection_ = true;
}

void RendererImpl::set_model_matrix(const Mat4& model) {
    model_ = model;
}

void RendererImpl::set_pipeline(uint32_t index) {
    // 0 = textured (default), 1 = debug-normal viz. Out-of-range
    // values clamp to 0 instead of asserting - draw() always has a
    // sensible fallback.
    active_pipeline_index_ = (index <= 1) ? index : 0;
}

}  // namespace kuma
