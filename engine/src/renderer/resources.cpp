// ── GPU Resources ───────────────────────────────────────────────
// Buffers, textures, descriptors, command pool/buffers, sync objects,
// and GPU memory helpers.

#include "renderer_impl.h"

namespace kuma {

// ── Vertex/Index Buffers (now loaded by ResourceManager) ───────

// ── Single-use Command Helpers ─────────────────────────────────

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
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
               new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
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
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
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

// ── Texture (now loaded by ResourceManager) ────────────────────

// ── Descriptor Sets ────────────────────────────────────────────

bool RendererImpl::create_descriptor_sets() {
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

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorImageInfo image_info{};
        image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        image_info.imageView = texture_->view;
        image_info.sampler = texture_->sampler;

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

// ── Memory Helpers ──────────────────────────────────────────────

uint32_t RendererImpl::find_memory_type(uint32_t type_filter,
    VkMemoryPropertyFlags properties) const
{
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(physical_device_, &mem_props);

    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
        bool type_suitable = (type_filter & (1 << i)) != 0;
        bool has_properties = (mem_props.memoryTypes[i].propertyFlags & properties) == properties;

        if (type_suitable && has_properties) {
            return i;
        }
    }

    std::printf("[Kuma] Failed to find suitable memory type\n");
    return 0;
}

} // namespace kuma
