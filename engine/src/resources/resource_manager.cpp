// ── Resource Manager ────────────────────────────────────────────
// Loads, caches, and manages GPU resources (textures, meshes).
// Owns a GPU context (device, queue, etc.) to upload data to the GPU.

#include <kuma/resource_manager.h>
#include <vulkan/vulkan.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>
#include <array>

#define STB_IMAGE_IMPLEMENTATION
#include "../renderer/stb_image.h"
#include "../renderer/renderer_impl.h"

namespace kuma {

// ── Implementation ──────────────────────────────────────────────

class ResourceManager::Impl {
public:
    bool init(GpuContext gpu) {
        gpu_ = gpu;
        return true;
    }

    void shutdown() {
        for (auto& [path, texture] : texture_cache_) {
            if (texture.sampler != VK_NULL_HANDLE)
                vkDestroySampler(gpu_.device, texture.sampler, nullptr);
            if (texture.view != VK_NULL_HANDLE)
                vkDestroyImageView(gpu_.device, texture.view, nullptr);
            if (texture.image != VK_NULL_HANDLE) {
                vkDestroyImage(gpu_.device, texture.image, nullptr);
                vkFreeMemory(gpu_.device, texture.memory, nullptr);
            }
        }
        texture_cache_.clear();
    }

    const Texture* load_texture(const char* path);

    GpuContext gpu_;

    // Cache: path → loaded resource
    std::unordered_map<std::string, Texture> texture_cache_;

private:
    uint32_t find_memory_type(uint32_t type_filter,
        VkMemoryPropertyFlags properties) const;
    VkCommandBuffer begin_single_command() const;
    void end_single_command(VkCommandBuffer cmd) const;
    void transition_image_layout(VkImage image,
        VkImageLayout old_layout, VkImageLayout new_layout);
    void copy_buffer_to_image(VkBuffer buffer, VkImage image,
        uint32_t width, uint32_t height);
};

// ── Public wrapper ──────────────────────────────────────────────

ResourceManager::ResourceManager() = default;

ResourceManager::~ResourceManager() {
    shutdown();
}

bool ResourceManager::init(void* gpu_context) {
    impl_ = new Impl();
    auto* ctx = static_cast<GpuContext*>(gpu_context);
    return impl_->init(*ctx);
}

void ResourceManager::shutdown() {
    if (impl_) {
        impl_->shutdown();
        delete impl_;
        impl_ = nullptr;
    }
}

const Texture* ResourceManager::load_texture(const char* path) {
    return impl_->load_texture(path);
}

const Mesh* ResourceManager::load_mesh(const char* path) {
    // Step 5 — will be implemented when we add OBJ loading
    (void)path;
    return nullptr;
}

// ── GPU Helpers ─────────────────────────────────────────────────
// These are the same functions from resources.cpp, but taking explicit
// parameters instead of reading RendererImpl member variables.

uint32_t ResourceManager::Impl::find_memory_type(
    uint32_t type_filter, VkMemoryPropertyFlags properties) const
{
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(gpu_.physical_device, &mem_props);

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

VkCommandBuffer ResourceManager::Impl::begin_single_command() const
{
    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = gpu_.command_pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(gpu_.device, &alloc_info, &cmd);

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin_info);

    return cmd;
}

void ResourceManager::Impl::end_single_command(VkCommandBuffer cmd) const
{
    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &cmd;

    vkQueueSubmit(gpu_.graphics_queue, 1, &submit_info, VK_NULL_HANDLE);
    vkQueueWaitIdle(gpu_.graphics_queue);

    vkFreeCommandBuffers(gpu_.device, gpu_.command_pool, 1, &cmd);
}

void ResourceManager::Impl::transition_image_layout(VkImage image, VkImageLayout old_layout, VkImageLayout new_layout)
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

void ResourceManager::Impl::copy_buffer_to_image(
    VkBuffer buffer, VkImage image, uint32_t width, uint32_t height)
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

// ── Texture Loading ─────────────────────────────────────────────

const Texture* ResourceManager::Impl::load_texture(const char* path)
{
    // Check cache first
    auto it = texture_cache_.find(path);
    if (it != texture_cache_.end()) {
        return &it->second;
    }

    // Load image from disk
    int tex_width = 0;
    int tex_height = 0;
    int tex_channels = 0;
    stbi_uc* pixels = stbi_load(path, &tex_width, &tex_height, &tex_channels, STBI_rgb_alpha);

    if (!pixels) {
        std::printf("[Kuma] Failed to load texture: %s (%s)\n", path, stbi_failure_reason());
        return nullptr;
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

    vkCreateBuffer(gpu_.device, &staging_info, nullptr, &staging_buffer);

    VkMemoryRequirements mem_reqs;
    vkGetBufferMemoryRequirements(gpu_.device, staging_buffer, &mem_reqs);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = find_memory_type(mem_reqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    vkAllocateMemory(gpu_.device, &alloc_info, nullptr, &staging_memory);
    vkBindBufferMemory(gpu_.device, staging_buffer, staging_memory, 0);

    void* data;
    vkMapMemory(gpu_.device, staging_memory, 0, image_size, 0, &data);
    std::memcpy(data, pixels, static_cast<size_t>(image_size));
    vkUnmapMemory(gpu_.device, staging_memory);

    stbi_image_free(pixels);

    // GPU image
    Texture texture{};

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

    if (vkCreateImage(gpu_.device, &img_info, nullptr, &texture.image) != VK_SUCCESS) {
        std::printf("[Kuma] Failed to create texture image\n");
        vkDestroyBuffer(gpu_.device, staging_buffer, nullptr);
        vkFreeMemory(gpu_.device, staging_memory, nullptr);
        return nullptr;
    }

    vkGetImageMemoryRequirements(gpu_.device, texture.image, &mem_reqs);

    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = find_memory_type(mem_reqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    vkAllocateMemory(gpu_.device, &alloc_info, nullptr, &texture.memory);
    vkBindImageMemory(gpu_.device, texture.image, texture.memory, 0);

    // Transfer
    transition_image_layout(texture.image,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    copy_buffer_to_image(staging_buffer, texture.image,
        static_cast<uint32_t>(tex_width), static_cast<uint32_t>(tex_height));

    transition_image_layout(texture.image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    vkDestroyBuffer(gpu_.device, staging_buffer, nullptr);
    vkFreeMemory(gpu_.device, staging_memory, nullptr);

    // Image view
    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = texture.image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = VK_FORMAT_R8G8B8A8_SRGB;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;

    if (vkCreateImageView(gpu_.device, &view_info, nullptr, &texture.view) != VK_SUCCESS) {
        std::printf("[Kuma] Failed to create texture image view\n");
        return nullptr;
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

    if (vkCreateSampler(gpu_.device, &sampler_info, nullptr, &texture.sampler) != VK_SUCCESS) {
        std::printf("[Kuma] Failed to create texture sampler\n");
        return nullptr;
    }

    texture.width = static_cast<uint32_t>(tex_width);
    texture.height = static_cast<uint32_t>(tex_height);

    std::printf("[Kuma] Texture loaded: %s (%ux%u)\n", path, texture.width, texture.height);

    // Store in cache and return pointer to the cached copy
    auto [inserted, _] = texture_cache_.emplace(path, texture);
    return &inserted->second;
}

} // namespace kuma
