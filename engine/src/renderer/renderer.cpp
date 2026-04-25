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

void Renderer::set_texture(const void* texture) {
    impl_->set_texture(static_cast<const Texture*>(texture));
}

void Renderer::set_view_projection(const Mat4& view_projection) {
    impl_->set_view_projection(view_projection);
}

void* Renderer::gpu_context() {
    static GpuContext ctx = impl_->gpu_context();
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
    if (!create_graphics_pipeline())
        return false;
    if (!create_framebuffers())
        return false;
    if (!create_command_pool())
        return false;
    if (!create_command_buffers())
        return false;
    if (!create_sync_objects())
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

    if (descriptor_pool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr);
    }

    // Mesh and texture are owned by ResourceManager — not destroyed here.

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
    kuma::log::info("Vulkan renderer shut down");
}

// ── Per-Frame Rendering ─────────────────────────────────────────

bool RendererImpl::begin_frame() {
    if (!has_view_projection_) {
        kuma::log::error(
            "Renderer missing view-projection matrix; call Renderer::set_view_projection() before "
            "kuma::end_frame()");
        return false;
    }

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

    // Draw commands
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline_);

    Mat4 model = Mat4::identity();

    // projection * view * model: model transforms first, then view, then project
    Mat4 mvp = view_projection_ * model;

    vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Mat4),
                       mvp.ptr());

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

    VkBuffer buffers[] = {mesh_->vertex_buffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, buffers, offsets);

    vkCmdBindIndexBuffer(cmd, mesh_->index_buffer, 0, VK_INDEX_TYPE_UINT16);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_, 0, 1,
                            &descriptor_sets_[current_frame_], 0, nullptr);

    vkCmdDrawIndexed(cmd, mesh_->index_count, 1, 0, 0, 0);

    return true;
}

void RendererImpl::end_frame() {
    VkCommandBuffer cmd = command_buffers_[current_frame_];

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

void RendererImpl::set_mesh(const Mesh* mesh) {
    mesh_ = mesh;
}

void RendererImpl::set_texture(const Texture* texture) {
    texture_ = texture;

    // Recreate descriptor sets to point at the new texture
    if (descriptor_pool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr);
        descriptor_pool_ = VK_NULL_HANDLE;
    }

    if (texture_ != nullptr) {
        create_descriptor_sets();
    }
}

void RendererImpl::set_view_projection(const Mat4& view_projection) {
    view_projection_ = view_projection;
    has_view_projection_ = true;
}

}  // namespace kuma
