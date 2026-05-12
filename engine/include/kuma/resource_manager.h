#pragma once

#include <cstdint>

namespace kuma {

// Forward declarations — game code doesn't need Vulkan headers
struct Texture;
struct Mesh;

class ResourceManager {
public:
    ResourceManager();
    ~ResourceManager();

    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;

    // Called by the engine during init/shutdown (not by game code).
    // gpu_context is an opaque pointer from Renderer::gpu_context().
    bool init(void* gpu_context);
    void shutdown();

    // Load a texture from a `.ktex` binary file produced by kuma-bake.
    // Returns nullptr on failure (file missing, bad magic, version
    // mismatch, unsupported format, truncated payload). Caches by
    // path - loading the same file twice returns the same texture.
    const Texture* load_texture_binary(const char* path);

    // Load a mesh from a `.kmesh` binary file produced by kuma-bake.
    // Returns nullptr on failure. Caches by path.
    const Mesh* load_mesh_binary(const char* path);

private:
    class Impl;
    Impl* impl_ = nullptr;
};

}  // namespace kuma
