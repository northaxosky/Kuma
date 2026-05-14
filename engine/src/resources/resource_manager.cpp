// ── Resource Manager ────────────────────────────────────────────
// Loads, caches, and manages GPU resources (textures, meshes,
// materials). Owns a GPU context (device, queue, etc.) to upload
// data to the GPU and a Renderer reference for material descriptor
// allocation.
//
// All loaders consume the engine's binary asset format produced by
// kuma-bake (.kmesh, .ktex, .kmaterial). Source-format parsing
// (.obj, .png, .gltf) is the bake tool's job and never happens at
// runtime.

#include <kuma/asset_format.h>
#include <kuma/log.h>
#include <kuma/renderer.h>
#include <kuma/resource_manager.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>

#include "../renderer/renderer_impl.h"

namespace kuma {

// ── Implementation ──────────────────────────────────────────────

class ResourceManager::Impl {
public:
    bool init(Renderer& renderer) {
        renderer_ = &renderer;
        auto* ctx = static_cast<GpuContext*>(renderer.gpu_context());
        gpu_ = *ctx;
        return true;
    }

    void shutdown() {
        // Materials destruction MUST happen before texture
        // destruction: each material's descriptor set references
        // texture image views, and vkFreeDescriptorSets needs those
        // views still alive to cleanly remove them from the pool's
        // bookkeeping. After this loop the pool is empty and texture
        // VkImageViews can be destroyed without validation noise.
        for (auto& [path, material] : material_cache_) {
            if (material.descriptor_set != nullptr) {
                renderer_->free_material_descriptor_set(material.descriptor_set);
                material.descriptor_set = nullptr;
            }
        }
        material_cache_.clear();

        for (auto& [key, texture] : texture_cache_) {
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

    const Texture*  load_texture_binary(const char* path, TextureUsage usage);
    const Mesh*     load_mesh_binary(const char* path);
    const Material* load_material_binary(const char* path);

    Renderer*   renderer_ = nullptr;
    GpuContext  gpu_;

    // Texture cache key includes usage so the same .ktex requested
    // as Color and Data resolves to two GPU textures with different
    // VkFormat (sRGB vs UNORM). Key format: "<path>|<usage_int>".
    std::unordered_map<std::string, Texture>  texture_cache_;
    std::unordered_map<std::string, Mesh>     mesh_cache_;
    std::unordered_map<std::string, Material> material_cache_;

private:
    uint32_t find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties) const;
    VkCommandBuffer begin_single_command() const;
    void end_single_command(VkCommandBuffer cmd) const;
    void transition_image_layout(VkImage image, VkImageLayout old_layout, VkImageLayout new_layout);
    void copy_buffer_to_image(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);

    // Allocates host-visible+coherent memory and uploads the given
    // bytes into a buffer of the requested usage. Caller owns the
    // returned VkBuffer + VkDeviceMemory and must free them on shutdown.
    bool upload_buffer(const void* data, VkDeviceSize size, VkBufferUsageFlags usage,
                       VkBuffer& out_buffer, VkDeviceMemory& out_memory);

    // Allocates a sampled 2D image, copies pixel bytes through a
    // staging buffer, transitions to SHADER_READ_ONLY_OPTIMAL, and
    // creates the image view + sampler.
    bool upload_texture(const void* pixels, uint32_t width, uint32_t height,
                        VkFormat format, Texture& out_texture);
};

// ── Public wrapper ──────────────────────────────────────────────

ResourceManager::ResourceManager() = default;

ResourceManager::~ResourceManager() {
    shutdown();
}

bool ResourceManager::init(Renderer& renderer) {
    impl_ = new Impl();
    return impl_->init(renderer);
}

void ResourceManager::shutdown() {
    if (impl_) {
        impl_->shutdown();
        delete impl_;
        impl_ = nullptr;
    }
}

const Texture* ResourceManager::load_texture_binary(const char* path, TextureUsage usage) {
    return impl_->load_texture_binary(path, usage);
}

const Mesh* ResourceManager::load_mesh_binary(const char* path) {
    return impl_->load_mesh_binary(path);
}

const Material* ResourceManager::load_material_binary(const char* path) {
    return impl_->load_material_binary(path);
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

    asset_format::KMeshHeader hdr{};
    const asset_format::ParseResult pr =
        asset_format::parse_kmesh_header(bytes.data(), bytes.size(), hdr);
    if (pr != asset_format::ParseResult::Ok) {
        kuma::log::error("kmesh parse failed (%d): %s", static_cast<int>(pr), path);
        return nullptr;
    }

    const VkDeviceSize vertex_bytes = static_cast<VkDeviceSize>(hdr.vertex_count) * sizeof(Vertex);
    const VkDeviceSize index_bytes  = static_cast<VkDeviceSize>(hdr.index_count) * sizeof(uint16_t);

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

const Texture* ResourceManager::Impl::load_texture_binary(const char* path, TextureUsage usage) {
    // Cache key combines path with usage so the same .ktex requested
    // as Color and Data resolves to two distinct GPU textures with
    // different VkFormat (sRGB vs UNORM) - critical for normal maps,
    // which must be sampled linearly even if the same source file
    // also happens to be used as a diffuse somewhere.
    std::string key;
    key.reserve(std::strlen(path) + 2);
    key.append(path);
    key.push_back('|');
    key.push_back(usage == TextureUsage::Color ? 'c' : 'd');

    auto it = texture_cache_.find(key);
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

    asset_format::KTexHeader hdr{};
    const asset_format::ParseResult pr =
        asset_format::parse_ktex_header(bytes.data(), bytes.size(), hdr);
    if (pr != asset_format::ParseResult::Ok) {
        kuma::log::error("ktex parse failed (%d): %s", static_cast<int>(pr), path);
        return nullptr;
    }

    const VkFormat vk_format = (usage == TextureUsage::Color)
        ? VK_FORMAT_R8G8B8A8_SRGB
        : VK_FORMAT_R8G8B8A8_UNORM;

    Texture texture{};
    if (!upload_texture(bytes.data() + hdr.pixel_offset, hdr.width, hdr.height,
                        vk_format, texture)) {
        return nullptr;
    }

    kuma::log::info("Texture loaded: %s (%ux%u, %s)",
                    path, hdr.width, hdr.height,
                    usage == TextureUsage::Color ? "sRGB" : "linear");
    auto [inserted, _] = texture_cache_.emplace(std::move(key), texture);
    return &inserted->second;
}

// ── Binary material loader (.kmaterial produced by kuma-bake) ───
//
// The header references texture paths via offsets into a string
// table that immediately follows the header in the file. Paths are
// stored relative to the .kmaterial's parent directory so a scene's
// material folder can be moved without rewriting paths.
//
// Each texture is loaded with the correct usage tag (Color for
// diffuse and emissive, Data for the rest). Missing slots stay
// nullptr; the renderer substitutes a default 1x1 texture at draw
// time so the shader can sample every binding unconditionally.

const Material* ResourceManager::Impl::load_material_binary(const char* path) {
    auto it = material_cache_.find(path);
    if (it != material_cache_.end()) {
        return &it->second;
    }

    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        kuma::log::error("kmaterial open failed: %s", path);
        return nullptr;
    }
    const std::streamsize size = f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<char> bytes(static_cast<size_t>(size));
    f.read(bytes.data(), size);

    asset_format::KMaterialHeader hdr{};
    const asset_format::ParseResult pr =
        asset_format::parse_kmaterial_header(bytes.data(), bytes.size(), hdr);
    if (pr != asset_format::ParseResult::Ok) {
        kuma::log::error("kmaterial parse failed (%d): %s", static_cast<int>(pr), path);
        return nullptr;
    }

    // String table sits immediately after the header. The parser
    // already validated every (offset, length) pair against
    // string_table_size, so reads here are in-bounds by construction.
    const char* string_table = bytes.data() + sizeof(asset_format::KMaterialHeader);
    const std::filesystem::path material_dir =
        std::filesystem::path(path).parent_path();

    auto resolve_texture = [&](uint32_t offset, uint32_t length,
                               TextureUsage usage) -> const Texture* {
        if (length == 0) return nullptr;
        std::string rel(string_table + offset, length);
        std::filesystem::path resolved = material_dir / rel;
        return load_texture_binary(resolved.string().c_str(), usage);
    };

    const Texture* slots[5] = {
        resolve_texture(hdr.diffuse_path_offset,            hdr.diffuse_path_length,
                        TextureUsage::Color),
        resolve_texture(hdr.normal_path_offset,             hdr.normal_path_length,
                        TextureUsage::Data),
        resolve_texture(hdr.metallic_roughness_path_offset, hdr.metallic_roughness_path_length,
                        TextureUsage::Data),
        resolve_texture(hdr.occlusion_path_offset,          hdr.occlusion_path_length,
                        TextureUsage::Data),
        resolve_texture(hdr.emissive_path_offset,           hdr.emissive_path_length,
                        TextureUsage::Color),
    };

    const void* slot_ptrs[5] = {slots[0], slots[1], slots[2], slots[3], slots[4]};
    void* descriptor_set = renderer_->create_material_descriptor_set(slot_ptrs);
    if (descriptor_set == nullptr) {
        kuma::log::error("kmaterial descriptor set allocation failed: %s", path);
        return nullptr;
    }

    Material mat{};
    mat.descriptor_set     = descriptor_set;
    mat.flags              = hdr.flags;
    mat.alpha_mode         = hdr.alpha_mode;
    std::memcpy(mat.base_color,      hdr.base_color,      sizeof(mat.base_color));
    mat.alpha_cutoff       = hdr.alpha_cutoff;
    mat.metallic_factor    = hdr.metallic_factor;
    mat.roughness_factor   = hdr.roughness_factor;
    mat.normal_scale       = hdr.normal_scale;
    mat.occlusion_strength = hdr.occlusion_strength;
    std::memcpy(mat.emissive_factor, hdr.emissive_factor, sizeof(mat.emissive_factor));

    kuma::log::info("Material loaded: %s", path);
    auto [inserted, _] = material_cache_.emplace(path, mat);
    return &inserted->second;
}

}  // namespace kuma
