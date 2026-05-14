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

void RendererImpl::transition_image_layout(VkImage image, VkImageLayout old_layout,
                                           VkImageLayout new_layout) {
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
        kuma::log::error("Unsupported layout transition");
        end_single_command(cmd);
        return;
    }

    vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    end_single_command(cmd);
}

// ── Buffer-to-Image Copy ───────────────────────────────────────

void RendererImpl::copy_buffer_to_image(VkBuffer buffer, VkImage image, uint32_t width,
                                        uint32_t height) {
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

    vkCmdCopyBufferToImage(cmd, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    end_single_command(cmd);
}

// ── Texture (now loaded by ResourceManager) ────────────────────

// ── Default Textures ───────────────────────────────────────────
// 1x1 RGBA fallbacks bound when a material doesn't supply a slot.
// Default values match the glTF 2.0 spec so a "default everything"
// material reproduces the spec's neutral PBR behavior:
//   diffuse  = white            -> base color factor passes through
//   normal   = (0.5, 0.5, 1.0)  -> tangent-space "no perturbation"
//   MR       = (0, 1, 0, 0)     -> roughness=1, metallic=0 packed in GB
//   occlusion= white            -> no AO darkening
//   emissive = black            -> emissive factor passes through
//
// Stored as RGBA8_UNORM. The lit shader will eventually multiply by
// the material factors; the current diffuse-only shader just samples
// the diffuse texture directly so the white default cleanly produces
// "untextured material renders white".

bool RendererImpl::upload_pixels_to_texture(const void* pixels, uint32_t width, uint32_t height,
                                            VkFormat format, Texture& out_texture) {
    const VkDeviceSize image_size = static_cast<VkDeviceSize>(width) * height * 4;

    // Staging buffer in host-visible memory.
    VkBuffer staging_buffer = VK_NULL_HANDLE;
    VkDeviceMemory staging_memory = VK_NULL_HANDLE;
    VkBufferCreateInfo buf_info{};
    buf_info.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buf_info.size        = image_size;
    buf_info.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    buf_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device_, &buf_info, nullptr, &staging_buffer) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements buf_reqs{};
    vkGetBufferMemoryRequirements(device_, staging_buffer, &buf_reqs);
    VkMemoryAllocateInfo buf_alloc{};
    buf_alloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    buf_alloc.allocationSize  = buf_reqs.size;
    buf_alloc.memoryTypeIndex = find_memory_type(
        buf_reqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(device_, &buf_alloc, nullptr, &staging_memory) != VK_SUCCESS) {
        vkDestroyBuffer(device_, staging_buffer, nullptr);
        return false;
    }
    vkBindBufferMemory(device_, staging_buffer, staging_memory, 0);

    void* mapped = nullptr;
    vkMapMemory(device_, staging_memory, 0, image_size, 0, &mapped);
    std::memcpy(mapped, pixels, static_cast<size_t>(image_size));
    vkUnmapMemory(device_, staging_memory);

    // Device-local image.
    VkImageCreateInfo img_info{};
    img_info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    img_info.imageType     = VK_IMAGE_TYPE_2D;
    img_info.extent.width  = width;
    img_info.extent.height = height;
    img_info.extent.depth  = 1;
    img_info.mipLevels     = 1;
    img_info.arrayLayers   = 1;
    img_info.format        = format;
    img_info.tiling        = VK_IMAGE_TILING_OPTIMAL;
    img_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    img_info.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    img_info.samples       = VK_SAMPLE_COUNT_1_BIT;
    img_info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateImage(device_, &img_info, nullptr, &out_texture.image) != VK_SUCCESS) {
        vkDestroyBuffer(device_, staging_buffer, nullptr);
        vkFreeMemory(device_, staging_memory, nullptr);
        return false;
    }

    VkMemoryRequirements img_reqs{};
    vkGetImageMemoryRequirements(device_, out_texture.image, &img_reqs);
    VkMemoryAllocateInfo img_alloc{};
    img_alloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    img_alloc.allocationSize  = img_reqs.size;
    img_alloc.memoryTypeIndex =
        find_memory_type(img_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(device_, &img_alloc, nullptr, &out_texture.memory) != VK_SUCCESS) {
        vkDestroyImage(device_, out_texture.image, nullptr);
        vkDestroyBuffer(device_, staging_buffer, nullptr);
        vkFreeMemory(device_, staging_memory, nullptr);
        return false;
    }
    vkBindImageMemory(device_, out_texture.image, out_texture.memory, 0);

    transition_image_layout(out_texture.image, VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    copy_buffer_to_image(staging_buffer, out_texture.image, width, height);
    transition_image_layout(out_texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    vkDestroyBuffer(device_, staging_buffer, nullptr);
    vkFreeMemory(device_, staging_memory, nullptr);

    VkImageViewCreateInfo view_info{};
    view_info.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image                           = out_texture.image;
    view_info.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format                          = format;
    view_info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel   = 0;
    view_info.subresourceRange.levelCount     = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount     = 1;
    if (vkCreateImageView(device_, &view_info, nullptr, &out_texture.view) != VK_SUCCESS) {
        return false;
    }

    VkSamplerCreateInfo sampler_info{};
    sampler_info.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter    = VK_FILTER_NEAREST;  // 1x1 defaults; no filtering needed
    sampler_info.minFilter    = VK_FILTER_NEAREST;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.borderColor  = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    sampler_info.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    if (vkCreateSampler(device_, &sampler_info, nullptr, &out_texture.sampler) != VK_SUCCESS) {
        return false;
    }

    out_texture.width  = width;
    out_texture.height = height;
    return true;
}

bool RendererImpl::create_default_textures() {
    struct Default { VkFormat format; uint8_t pixel[4]; const char* name; };
    const Default defaults[5] = {
        {VK_FORMAT_R8G8B8A8_SRGB,  {255, 255, 255, 255}, "diffuse"},
        {VK_FORMAT_R8G8B8A8_UNORM, {128, 128, 255, 255}, "normal"},
        {VK_FORMAT_R8G8B8A8_UNORM, {  0, 255,   0, 255}, "metallic-roughness"},
        {VK_FORMAT_R8G8B8A8_UNORM, {255, 255, 255, 255}, "occlusion"},
        {VK_FORMAT_R8G8B8A8_SRGB,  {  0,   0,   0, 255}, "emissive"},
    };
    for (uint32_t i = 0; i < default_textures_.size(); ++i) {
        if (!upload_pixels_to_texture(defaults[i].pixel, 1, 1, defaults[i].format,
                                      default_textures_[i])) {
            kuma::log::error("Failed to create default %s texture", defaults[i].name);
            return false;
        }
    }
    kuma::log::info("Default material textures created");
    return true;
}

// ── Material Descriptor Pool ───────────────────────────────────
// One descriptor set per loaded material, all immutable after
// allocation. Pool capacity is the max materials a scene can hold;
// 256 covers Sponza (~25 materials) with plenty of headroom for
// stress-test scenes. When scenes start needing more, this becomes
// the place to grow the pool or switch to bindless (followups.md).

bool RendererImpl::create_material_descriptor_pool() {
    constexpr uint32_t kMaxMaterials = 256;
    constexpr uint32_t kBindingsPerMaterial = 5;

    VkDescriptorPoolSize pool_size{};
    pool_size.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_size.descriptorCount = kMaxMaterials * kBindingsPerMaterial;

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes    = &pool_size;
    pool_info.maxSets       = kMaxMaterials;

    if (vkCreateDescriptorPool(device_, &pool_info, nullptr, &material_pool_) != VK_SUCCESS) {
        kuma::log::error("Failed to create material descriptor pool");
        return false;
    }
    return true;
}

VkDescriptorSet RendererImpl::allocate_material_descriptor_set(const Texture* slots[5]) {
    VkDescriptorSetAllocateInfo alloc_info{};
    alloc_info.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool     = material_pool_;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts        = &descriptor_set_layout_;

    VkDescriptorSet set = VK_NULL_HANDLE;
    VkResult result = vkAllocateDescriptorSets(device_, &alloc_info, &set);
    if (result != VK_SUCCESS) {
        kuma::log::error("Material descriptor pool exhausted (allocated %u sets)",
                         materials_allocated_);
        return VK_NULL_HANDLE;
    }
    ++materials_allocated_;

    std::array<VkDescriptorImageInfo, 5> image_infos{};
    std::array<VkWriteDescriptorSet, 5>  writes{};
    for (uint32_t i = 0; i < 5; ++i) {
        const Texture* tex = (slots[i] != nullptr) ? slots[i] : &default_textures_[i];
        image_infos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        image_infos[i].imageView   = tex->view;
        image_infos[i].sampler     = tex->sampler;

        writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet          = set;
        writes[i].dstBinding      = i;
        writes[i].dstArrayElement = 0;
        writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].descriptorCount = 1;
        writes[i].pImageInfo      = &image_infos[i];
    }
    vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(),
                           0, nullptr);
    return set;
}

void RendererImpl::destroy_material_resources() {
    // Pool destruction frees every set allocated from it; the legacy
    // texture-to-set cache just borrows raw VkDescriptorSet handles
    // so clearing the map is enough on the C++ side.
    texture_to_material_set_.clear();
    if (material_pool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, material_pool_, nullptr);
        material_pool_ = VK_NULL_HANDLE;
    }
    for (auto& tex : default_textures_) {
        if (tex.sampler != VK_NULL_HANDLE) vkDestroySampler(device_, tex.sampler, nullptr);
        if (tex.view    != VK_NULL_HANDLE) vkDestroyImageView(device_, tex.view, nullptr);
        if (tex.image   != VK_NULL_HANDLE) {
            vkDestroyImage(device_, tex.image, nullptr);
            vkFreeMemory(device_, tex.memory, nullptr);
        }
        tex = Texture{};
    }
    materials_allocated_ = 0;
}

// ── Command Pool + Buffers ──────────────────────────────────────

bool RendererImpl::create_command_pool() {
    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = queue_family_index_;

    VkResult result = vkCreateCommandPool(device_, &pool_info, nullptr, &command_pool_);
    if (result != VK_SUCCESS) {
        kuma::log::error("Failed to create command pool");
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
        kuma::log::error("Failed to allocate command buffers");
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
        if (vkCreateSemaphore(device_, &sem_info, nullptr, &image_available_semaphores_[i]) !=
                VK_SUCCESS ||
            vkCreateFence(device_, &fence_info, nullptr, &in_flight_fences_[i]) != VK_SUCCESS) {
            kuma::log::error("Failed to create sync objects");
            return false;
        }
    }

    for (uint32_t i = 0; i < image_count; i++) {
        if (vkCreateSemaphore(device_, &sem_info, nullptr, &render_finished_semaphores_[i]) !=
            VK_SUCCESS) {
            kuma::log::error("Failed to create render finished semaphore");
            return false;
        }
    }

    return true;
}

// ── Memory Helpers ──────────────────────────────────────────────

uint32_t RendererImpl::find_memory_type(uint32_t type_filter,
                                        VkMemoryPropertyFlags properties) const {
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(physical_device_, &mem_props);

    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
        bool type_suitable = (type_filter & (1 << i)) != 0;
        bool has_properties = (mem_props.memoryTypes[i].propertyFlags & properties) == properties;

        if (type_suitable && has_properties) {
            return i;
        }
    }

    kuma::log::error("Failed to find suitable memory type");
    return 0;
}

}  // namespace kuma
