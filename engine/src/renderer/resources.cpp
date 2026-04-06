// ── GPU Resources ───────────────────────────────────────────────
// Buffers, textures, descriptors, command pool/buffers, sync objects,
// and GPU memory helpers.

#include "renderer_impl.h"

// stb_image: single-header image loading library.
// STB_IMAGE_IMPLEMENTATION generates the function bodies — must be in exactly one .cpp.
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace kuma {

// ── Vertex Buffer ──────────────────────────────────────────────

bool RendererImpl::create_vertex_buffer() {
    // Correct for aspect ratio so the quad appears square on screen.
    // Without this, a 1:1 quad in clip space stretches to 16:9 on a widescreen window.
    float aspect = static_cast<float>(width_) / static_cast<float>(height_);
    float half_w = 0.5f / aspect;   // shrink X to compensate
    float half_h = 0.5f;

    const std::array<Vertex, 4> vertices = {{
        {{-half_w, -half_h}, {0.0f, 0.0f}},   // top-left
        {{ half_w, -half_h}, {1.0f, 0.0f}},   // top-right
        {{-half_w,  half_h}, {0.0f, 1.0f}},   // bottom-left
        {{ half_w,  half_h}, {1.0f, 1.0f}},   // bottom-right
    }};

    VkDeviceSize buffer_size = sizeof(Vertex) * vertices.size();

    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = buffer_size;
    buffer_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult result = vkCreateBuffer(device_, &buffer_info, nullptr, &mesh_.vertex_buffer);
    if (result != VK_SUCCESS) {
        std::printf("[Kuma] Failed to create vertex buffer\n");
        return false;
    }

    VkMemoryRequirements mem_reqs;
    vkGetBufferMemoryRequirements(device_, mesh_.vertex_buffer, &mem_reqs);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = find_memory_type(mem_reqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    result = vkAllocateMemory(device_, &alloc_info, nullptr, &mesh_.vertex_memory);
    if (result != VK_SUCCESS) {
        std::printf("[Kuma] Failed to allocate vertex buffer memory\n");
        return false;
    }

    vkBindBufferMemory(device_, mesh_.vertex_buffer, mesh_.vertex_memory, 0);

    void* data = nullptr;
    vkMapMemory(device_, mesh_.vertex_memory, 0, buffer_size, 0, &data);
    std::memcpy(data, vertices.data(), buffer_size);
    vkUnmapMemory(device_, mesh_.vertex_memory);

    std::printf("[Kuma] Vertex buffer created (%zu bytes, %zu vertices)\n",
        static_cast<size_t>(buffer_size), vertices.size());
    return true;
}

// ── Index Buffer ───────────────────────────────────────────────

bool RendererImpl::create_index_buffer() {
    const std::array<uint16_t, 6> indices = {{
        0, 1, 2,
        2, 1, 3
    }};

    mesh_.index_count = static_cast<uint32_t>(indices.size());
    VkDeviceSize buffer_size = sizeof(uint16_t) * indices.size();

    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = buffer_size;
    buffer_info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult result = vkCreateBuffer(device_, &buffer_info, nullptr, &mesh_.index_buffer);
    if (result != VK_SUCCESS) {
        std::printf("[Kuma] Failed to create index buffer\n");
        return false;
    }

    VkMemoryRequirements mem_reqs;
    vkGetBufferMemoryRequirements(device_, mesh_.index_buffer, &mem_reqs);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = find_memory_type(mem_reqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    result = vkAllocateMemory(device_, &alloc_info, nullptr, &mesh_.index_memory);
    if (result != VK_SUCCESS) {
        std::printf("[Kuma] Failed to allocate index buffer memory\n");
        return false;
    }

    vkBindBufferMemory(device_, mesh_.index_buffer, mesh_.index_memory, 0);

    void* data = nullptr;
    vkMapMemory(device_, mesh_.index_memory, 0, buffer_size, 0, &data);
    std::memcpy(data, indices.data(), buffer_size);
    vkUnmapMemory(device_, mesh_.index_memory);

    return true;
}

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

// ── Texture ────────────────────────────────────────────────────

bool RendererImpl::create_texture() {
    // Load image from disk using stb_image.
    // The last argument (4) forces RGBA output regardless of the file's format.
    int tex_width = 0;
    int tex_height = 0;
    int tex_channels = 0;
    stbi_uc* pixels = stbi_load("assets/textures/VaultBoyNV.png",
        &tex_width, &tex_height, &tex_channels, STBI_rgb_alpha);

    if (!pixels) {
        std::printf("[Kuma] Failed to load texture: %s\n", stbi_failure_reason());
        return false;
    }

    VkDeviceSize image_size = static_cast<VkDeviceSize>(tex_width) * tex_height * 4;

    // Staging buffer
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

    void* data;
    vkMapMemory(device_, staging_memory, 0, image_size, 0, &data);
    std::memcpy(data, pixels, static_cast<size_t>(image_size));
    vkUnmapMemory(device_, staging_memory);

    // Pixel data has been copied to the staging buffer — free the CPU copy
    stbi_image_free(pixels);

    // GPU image
    VkImageCreateInfo img_info{};
    img_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    img_info.imageType = VK_IMAGE_TYPE_2D;
    img_info.extent = {static_cast<uint32_t>(tex_width), static_cast<uint32_t>(tex_height), 1};
    img_info.mipLevels = 1;
    img_info.arrayLayers = 1;
    img_info.format = VK_FORMAT_R8G8B8A8_SRGB;
    img_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    img_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    img_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    img_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    img_info.samples = VK_SAMPLE_COUNT_1_BIT;

    if (vkCreateImage(device_, &img_info, nullptr, &texture_.image) != VK_SUCCESS) {
        std::printf("[Kuma] Failed to create texture image\n");
        vkDestroyBuffer(device_, staging_buffer, nullptr);
        vkFreeMemory(device_, staging_memory, nullptr);
        return false;
    }

    vkGetImageMemoryRequirements(device_, texture_.image, &mem_reqs);

    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = find_memory_type(mem_reqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    vkAllocateMemory(device_, &alloc_info, nullptr, &texture_.memory);
    vkBindImageMemory(device_, texture_.image, texture_.memory, 0);

    // Transfer
    transition_image_layout(texture_.image,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    copy_buffer_to_image(staging_buffer, texture_.image,
        static_cast<uint32_t>(tex_width), static_cast<uint32_t>(tex_height));

    transition_image_layout(texture_.image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    vkDestroyBuffer(device_, staging_buffer, nullptr);
    vkFreeMemory(device_, staging_memory, nullptr);

    // Image view
    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = texture_.image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = VK_FORMAT_R8G8B8A8_SRGB;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device_, &view_info, nullptr, &texture_.view) != VK_SUCCESS) {
        std::printf("[Kuma] Failed to create texture image view\n");
        return false;
    }

    // Sampler
    VkSamplerCreateInfo sampler_info{};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = VK_FILTER_NEAREST;
    sampler_info.minFilter = VK_FILTER_NEAREST;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.anisotropyEnable = VK_FALSE;
    sampler_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    sampler_info.unnormalizedCoordinates = VK_FALSE;
    sampler_info.compareEnable = VK_FALSE;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

    if (vkCreateSampler(device_, &sampler_info, nullptr, &texture_.sampler) != VK_SUCCESS) {
        std::printf("[Kuma] Failed to create texture sampler\n");
        return false;
    }

    texture_.width = static_cast<uint32_t>(tex_width);
    texture_.height = static_cast<uint32_t>(tex_height);

    std::printf("[Kuma] Texture loaded (%ux%u)\n", texture_.width, texture_.height);
    return true;
}

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
        image_info.imageView = texture_.view;
        image_info.sampler = texture_.sampler;

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
