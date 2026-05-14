#pragma once

#include <kuma/material.h>

#include <cstdint>

namespace kuma {

// Forward declarations — game code doesn't need Vulkan headers
struct Texture;
struct Mesh;
class Renderer;

class ResourceManager {
public:
    ResourceManager();
    ~ResourceManager();

    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;

    // Called by the engine during init/shutdown (not by game code).
    // Renderer reference is stored for material descriptor allocation
    // and for the GpuContext (device, queue, command pool).
    bool init(Renderer& renderer);
    void shutdown();

    // Load a texture from a `.ktex` binary file produced by kuma-bake.
    // Returns nullptr on failure (file missing, bad magic, version
    // mismatch, unsupported format, truncated payload). Caches by
    // (path, usage) - the same .ktex loaded as Color and as Data
    // produces two distinct GPU textures with different VkFormat
    // (sRGB vs UNORM) so a normal map and a diffuse share-by-accident
    // never produces a wrongly-shaded surface.
    const Texture* load_texture_binary(const char* path,
                                       TextureUsage usage = TextureUsage::Color);

    // Load a mesh from a `.kmesh` binary file produced by kuma-bake.
    // Returns nullptr on failure. Caches by path.
    const Mesh* load_mesh_binary(const char* path);

    // Load a material from a `.kmaterial` binary file produced by
    // kuma-bake. Returns nullptr on failure. Caches by path.
    //
    // Loads referenced textures with the correct usage tag (Color
    // for diffuse and emissive, Data for normal/MR/occlusion) and
    // allocates a descriptor set from the renderer's material pool.
    // Unspecified texture slots get the renderer's defaults at draw
    // time (white diffuse, neutral normal, etc).
    const Material* load_material_binary(const char* path);

private:
    class Impl;
    Impl* impl_ = nullptr;
};

}  // namespace kuma
