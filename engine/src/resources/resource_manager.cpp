// ── Resource Manager ────────────────────────────────────────────
// Loads, caches, and manages GPU resources (textures, meshes).
// Owns a GPU context (device, queue, etc.) to upload data to the GPU.

#include <kuma/asset_format.h>
#include <kuma/log.h>
#include <kuma/resource_manager.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>

#define STB_IMAGE_IMPLEMENTATION
#include "../renderer/stb_image.h"

#define TINYOBJLOADER_DISABLE_FAST_FLOAT
#define TINYOBJLOADER_IMPLEMENTATION
#include "../renderer/renderer_impl.h"
#include "tiny_obj_loader.h"

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

        for (auto& [path, mesh] : mesh_cache_) {
            if (mesh.vertex_buffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(gpu_.device, mesh.vertex_buffer, nullptr);
                vkFreeMemory(gpu_.device, mesh.vertex_memory, nullptr);
            }
            if (mesh.index_buffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(gpu_.device, mesh.index_buffer, nullptr);
                vkFreeMemory(gpu_.device, mesh.index_memory, nullptr);
            }
        }
        mesh_cache_.clear();
    }

    const Texture* load_texture(const char* path);
    const Texture* load_texture_binary(const char* path);
    const Mesh* load_mesh(const char* path);
    const Mesh* load_mesh_binary(const char* path);

    GpuContext gpu_;

    std::unordered_map<std::string, Texture> texture_cache_;
    std::unordered_map<std::string, Mesh> mesh_cache_;

private:
    uint32_t find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties) const;
    VkCommandBuffer begin_single_command() const;
    void end_single_command(VkCommandBuffer cmd) const;
    void transition_image_layout(VkImage image, VkImageLayout old_layout, VkImageLayout new_layout);
    void copy_buffer_to_image(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);

    // Allocates host-visible+coherent memory and uploads the given
    // bytes into a buffer of the requested usage. Used by both the
    // mesh and (eventually) other binary loaders. Caller owns the
    // returned VkBuffer + VkDeviceMemory and must free them on shutdown.
    bool upload_buffer(const void* data, VkDeviceSize size, VkBufferUsageFlags usage,
                       VkBuffer& out_buffer, VkDeviceMemory& out_memory);

    // Allocates a sampled 2D image, copies pixel bytes through a
    // staging buffer, transitions to SHADER_READ_ONLY_OPTIMAL, and
    // creates the image view + sampler. Used by both load_texture
    // (PNG/JPG path) and load_texture_binary (.ktex path).
    bool upload_texture(const void* pixels, uint32_t width, uint32_t height,
                        VkFormat format, Texture& out_texture);
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

const Texture* ResourceManager::load_texture_binary(const char* path) {
    return impl_->load_texture_binary(path);
}

const Mesh* ResourceManager::load_mesh(const char* path) {
    return impl_->load_mesh(path);
}

const Mesh* ResourceManager::load_mesh_binary(const char* path) {
    return impl_->load_mesh_binary(path);
}

// ── GPU Helpers ─────────────────────────────────────────────────
// These are the same functions from resources.cpp, but taking explicit
// parameters instead of reading RendererImpl member variables.

uint32_t ResourceManager::Impl::find_memory_type(uint32_t type_filter,
                                                 VkMemoryPropertyFlags properties) const {
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(gpu_.physical_device, &mem_props);

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

VkCommandBuffer ResourceManager::Impl::begin_single_command() const {
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

void ResourceManager::Impl::end_single_command(VkCommandBuffer cmd) const {
    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &cmd;

    vkQueueSubmit(gpu_.graphics_queue, 1, &submit_info, VK_NULL_HANDLE);
    vkQueueWaitIdle(gpu_.graphics_queue);

    vkFreeCommandBuffers(gpu_.device, gpu_.command_pool, 1, &cmd);
}

void ResourceManager::Impl::transition_image_layout(VkImage image, VkImageLayout old_layout,
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

void ResourceManager::Impl::copy_buffer_to_image(VkBuffer buffer, VkImage image, uint32_t width,
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

// ── Texture Loading ─────────────────────────────────────────────

const Texture* ResourceManager::Impl::load_texture(const char* path) {
    auto it = texture_cache_.find(path);
    if (it != texture_cache_.end()) {
        return &it->second;
    }

    int tex_width = 0;
    int tex_height = 0;
    int tex_channels = 0;
    stbi_uc* pixels = stbi_load(path, &tex_width, &tex_height, &tex_channels, STBI_rgb_alpha);
    if (!pixels) {
        kuma::log::error("Failed to load texture: %s (%s)", path, stbi_failure_reason());
        return nullptr;
    }

    Texture texture{};
    bool ok = upload_texture(pixels, static_cast<uint32_t>(tex_width),
                             static_cast<uint32_t>(tex_height),
                             VK_FORMAT_R8G8B8A8_SRGB, texture);
    stbi_image_free(pixels);
    if (!ok) {
        return nullptr;
    }

    kuma::log::info("Texture loaded: %s (%ux%u)", path, texture.width, texture.height);
    auto [inserted, _] = texture_cache_.emplace(path, texture);
    return &inserted->second;
}

// ── Mesh Loading ────────────────────────────────────────────────

const Mesh* ResourceManager::Impl::load_mesh(const char* path) {
    // Check cache first
    auto it = mesh_cache_.find(path);
    if (it != mesh_cache_.end()) {
        return &it->second;
    }

    // Parse the OBJ file using tinyobjloader
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn;
    std::string err;

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path)) {
        kuma::log::error("Failed to load mesh: %s (%s)", path, err.c_str());
        return nullptr;
    }

    if (!warn.empty()) {
        kuma::log::warn("OBJ warning: %s", warn.c_str());
    }

    // Build vertex and index arrays from the parsed data.
    // OBJ stores positions and UVs in separate arrays, then references
    // them by index in each face. We need to combine them into our
    // interleaved Vertex format.
    std::vector<Vertex> vertices;
    std::vector<uint16_t> indices;

    for (const auto& shape : shapes) {
        for (const auto& index : shape.mesh.indices) {
            Vertex vertex{};

            vertex.pos[0] = attrib.vertices[3 * index.vertex_index + 0];
            vertex.pos[1] = attrib.vertices[3 * index.vertex_index + 1];
            vertex.pos[2] = attrib.vertices[3 * index.vertex_index + 2];

            if (index.texcoord_index >= 0) {
                vertex.uv[0] = attrib.texcoords[2 * index.texcoord_index + 0];
                // OBJ has V=0 at bottom, Vulkan has V=0 at top - flip it.
                vertex.uv[1] = 1.0f - attrib.texcoords[2 * index.texcoord_index + 1];
            }

            // Normal: fill in if present, otherwise default to +Y.
            if (index.normal_index >= 0) {
                vertex.normal[0] = attrib.normals[3 * index.normal_index + 0];
                vertex.normal[1] = attrib.normals[3 * index.normal_index + 1];
                vertex.normal[2] = attrib.normals[3 * index.normal_index + 2];
            } else {
                vertex.normal[1] = 1.0f;
            }

            indices.push_back(static_cast<uint16_t>(vertices.size()));
            vertices.push_back(vertex);
        }
    }

    // Upload vertex + index data to GPU.
    Mesh mesh{};
    mesh.index_count = static_cast<uint32_t>(indices.size());

    if (!upload_buffer(vertices.data(), sizeof(Vertex) * vertices.size(),
                       VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                       mesh.vertex_buffer, mesh.vertex_memory)) {
        return nullptr;
    }
    if (!upload_buffer(indices.data(), sizeof(uint16_t) * indices.size(),
                       VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                       mesh.index_buffer, mesh.index_memory)) {
        return nullptr;
    }

    kuma::log::info("Mesh loaded: %s (%zu vertices, %u indices)", path, vertices.size(),
                    mesh.index_count);

    auto [inserted, _] = mesh_cache_.emplace(path, mesh);
    return &inserted->second;
}

// ── Binary mesh loader (.kmesh produced by kuma-bake) ───────────

bool ResourceManager::Impl::upload_buffer(const void* data, VkDeviceSize size,
                                          VkBufferUsageFlags usage, VkBuffer& out_buffer,
                                          VkDeviceMemory& out_memory) {
    VkBufferCreateInfo bi{};
    bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size        = size;
    bi.usage       = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(gpu_.device, &bi, nullptr, &out_buffer) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements mem_reqs{};
    vkGetBufferMemoryRequirements(gpu_.device, out_buffer, &mem_reqs);

    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = mem_reqs.size;
    ai.memoryTypeIndex = find_memory_type(
        mem_reqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(gpu_.device, &ai, nullptr, &out_memory) != VK_SUCCESS) {
        vkDestroyBuffer(gpu_.device, out_buffer, nullptr);
        out_buffer = VK_NULL_HANDLE;
        return false;
    }
    vkBindBufferMemory(gpu_.device, out_buffer, out_memory, 0);

    void* mapped = nullptr;
    vkMapMemory(gpu_.device, out_memory, 0, size, 0, &mapped);
    std::memcpy(mapped, data, size);
    vkUnmapMemory(gpu_.device, out_memory);
    return true;
}

const Mesh* ResourceManager::Impl::load_mesh_binary(const char* path) {
    auto it = mesh_cache_.find(path);
    if (it != mesh_cache_.end()) {
        return &it->second;
    }

    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        kuma::log::error("kmesh open failed: %s", path);
        return nullptr;
    }
    const std::streamsize size = f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<char> bytes(static_cast<size_t>(size));
    f.read(bytes.data(), size);

    if (bytes.size() < sizeof(asset_format::KMeshHeader)) {
        kuma::log::error("kmesh too small: %s", path);
        return nullptr;
    }

    asset_format::KMeshHeader hdr{};
    std::memcpy(&hdr, bytes.data(), sizeof(hdr));

    if (hdr.magic != asset_format::kMagicKMesh) {
        kuma::log::error("kmesh bad magic 0x%08x: %s", hdr.magic, path);
        return nullptr;
    }
    if (hdr.version != asset_format::kKMeshVersion) {
        kuma::log::error("kmesh version mismatch (file %u, engine %u): %s", hdr.version,
                         asset_format::kKMeshVersion, path);
        return nullptr;
    }

    const VkDeviceSize vertex_bytes = hdr.vertex_count * sizeof(Vertex);
    const VkDeviceSize index_bytes  = hdr.index_count * sizeof(uint16_t);
    if (hdr.vertex_offset + vertex_bytes > bytes.size() ||
        hdr.index_offset + index_bytes > bytes.size()) {
        kuma::log::error("kmesh truncated: %s", path);
        return nullptr;
    }

    Mesh mesh{};
    mesh.index_count = hdr.index_count;
    if (!upload_buffer(bytes.data() + hdr.vertex_offset, vertex_bytes,
                       VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                       mesh.vertex_buffer, mesh.vertex_memory)) {
        return nullptr;
    }
    if (!upload_buffer(bytes.data() + hdr.index_offset, index_bytes,
                       VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                       mesh.index_buffer, mesh.index_memory)) {
        return nullptr;
    }

    kuma::log::info("Mesh loaded: %s (%u vertices, %u indices, binary)", path,
                    hdr.vertex_count, hdr.index_count);

    auto [inserted, _] = mesh_cache_.emplace(path, mesh);
    return &inserted->second;
}

// ── Shared texture upload (used by both PNG/JPG and .ktex paths) ─

bool ResourceManager::Impl::upload_texture(const void* pixels, uint32_t width, uint32_t height,
                                           VkFormat format, Texture& out_texture) {
    const VkDeviceSize image_size = static_cast<VkDeviceSize>(width) * height * 4;

    // Staging buffer (host-visible, copied via cmd buffer to a GPU-local image).
    VkBuffer staging_buffer = VK_NULL_HANDLE;
    VkDeviceMemory staging_memory = VK_NULL_HANDLE;
    if (!upload_buffer(pixels, image_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                       staging_buffer, staging_memory)) {
        return false;
    }

    // Destination image (DEVICE_LOCAL, optimal tiling).
    VkImageCreateInfo img_info{};
    img_info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    img_info.imageType     = VK_IMAGE_TYPE_2D;
    img_info.extent        = {width, height, 1};
    img_info.mipLevels     = 1;
    img_info.arrayLayers   = 1;
    img_info.format        = format;
    img_info.tiling        = VK_IMAGE_TILING_OPTIMAL;
    img_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    img_info.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    img_info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    img_info.samples       = VK_SAMPLE_COUNT_1_BIT;
    if (vkCreateImage(gpu_.device, &img_info, nullptr, &out_texture.image) != VK_SUCCESS) {
        kuma::log::error("Failed to create texture image");
        vkDestroyBuffer(gpu_.device, staging_buffer, nullptr);
        vkFreeMemory(gpu_.device, staging_memory, nullptr);
        return false;
    }

    VkMemoryRequirements mem_reqs{};
    vkGetImageMemoryRequirements(gpu_.device, out_texture.image, &mem_reqs);

    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = mem_reqs.size;
    ai.memoryTypeIndex = find_memory_type(mem_reqs.memoryTypeBits,
                                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(gpu_.device, &ai, nullptr, &out_texture.memory);
    vkBindImageMemory(gpu_.device, out_texture.image, out_texture.memory, 0);

    transition_image_layout(out_texture.image, VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    copy_buffer_to_image(staging_buffer, out_texture.image, width, height);
    transition_image_layout(out_texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    vkDestroyBuffer(gpu_.device, staging_buffer, nullptr);
    vkFreeMemory(gpu_.device, staging_memory, nullptr);

    // Image view.
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
    if (vkCreateImageView(gpu_.device, &view_info, nullptr, &out_texture.view) != VK_SUCCESS) {
        kuma::log::error("Failed to create texture image view");
        return false;
    }

    // Sampler.
    VkSamplerCreateInfo sampler_info{};
    sampler_info.sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter               = VK_FILTER_NEAREST;
    sampler_info.minFilter               = VK_FILTER_NEAREST;
    sampler_info.addressModeU            = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.addressModeV            = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.addressModeW            = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.anisotropyEnable        = VK_FALSE;
    sampler_info.borderColor             = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    sampler_info.unnormalizedCoordinates = VK_FALSE;
    sampler_info.compareEnable           = VK_FALSE;
    sampler_info.mipmapMode              = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    if (vkCreateSampler(gpu_.device, &sampler_info, nullptr, &out_texture.sampler) != VK_SUCCESS) {
        kuma::log::error("Failed to create texture sampler");
        return false;
    }

    out_texture.width  = width;
    out_texture.height = height;
    return true;
}

// ── Binary texture loader (.ktex produced by kuma-bake) ─────────

const Texture* ResourceManager::Impl::load_texture_binary(const char* path) {
    auto it = texture_cache_.find(path);
    if (it != texture_cache_.end()) {
        return &it->second;
    }

    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        kuma::log::error("ktex open failed: %s", path);
        return nullptr;
    }
    const std::streamsize size = f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<char> bytes(static_cast<size_t>(size));
    f.read(bytes.data(), size);

    if (bytes.size() < sizeof(asset_format::KTexHeader)) {
        kuma::log::error("ktex too small: %s", path);
        return nullptr;
    }

    asset_format::KTexHeader hdr{};
    std::memcpy(&hdr, bytes.data(), sizeof(hdr));

    if (hdr.magic != asset_format::kMagicKTex) {
        kuma::log::error("ktex bad magic 0x%08x: %s", hdr.magic, path);
        return nullptr;
    }
    if (hdr.version != asset_format::kKTexVersion) {
        kuma::log::error("ktex version mismatch (file %u, engine %u): %s", hdr.version,
                         asset_format::kKTexVersion, path);
        return nullptr;
    }
    if (hdr.format != asset_format::kFormatRGBA8) {
        kuma::log::error("ktex unsupported format %u: %s", hdr.format, path);
        return nullptr;
    }

    const VkDeviceSize pixel_bytes = static_cast<VkDeviceSize>(hdr.width) * hdr.height * 4;
    if (hdr.pixel_offset + pixel_bytes > bytes.size()) {
        kuma::log::error("ktex truncated: %s", path);
        return nullptr;
    }

    Texture texture{};
    if (!upload_texture(bytes.data() + hdr.pixel_offset, hdr.width, hdr.height,
                        VK_FORMAT_R8G8B8A8_SRGB, texture)) {
        return nullptr;
    }

    kuma::log::info("Texture loaded: %s (%ux%u, binary)", path, hdr.width, hdr.height);
    auto [inserted, _] = texture_cache_.emplace(path, texture);
    return &inserted->second;
}

}  // namespace kuma
