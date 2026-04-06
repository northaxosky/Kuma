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

    // Load a texture from an image file (PNG, JPG, etc.)
    // Returns nullptr on failure. Caches by path — loading the same
    // file twice returns the same texture.
    const Texture* load_texture(const char* path);

    // Load a mesh from a model file (OBJ, etc.)
    // Returns nullptr on failure. Caches by path.
    const Mesh* load_mesh(const char* path);

private:
    class Impl;
    Impl* impl_ = nullptr;
};

} // namespace kuma
