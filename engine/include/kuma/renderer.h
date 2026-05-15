#pragma once

#include <kuma/material.h>
#include <kuma/math.h>

#include <cstdint>

struct SDL_Window;

namespace kuma {

// ── Present mode ────────────────────────────────────────────────
// How the GPU paces frames at the swapchain. The right choice is
// genre-dependent — there is no universal best.
enum class PresentMode {
    // FIFO — vsync on. Capped to monitor refresh rate. Low power,
    // no tearing, predictable dt. Default for single-player and
    // most non-competitive games.
    Vsync,

    // MAILBOX — uncapped render rate, monitor-paced display. Newest
    // frame always shown, older queued frames discarded. Lowest
    // input latency, highest power draw. Falls back to Vsync if the
    // GPU/driver does not support mailbox.
    Mailbox,

    // IMMEDIATE — uncapped, no synchronization. May tear. Mostly
    // useful for benchmarking. Falls back to Vsync if unsupported.
    Immediate,
};

struct RendererConfig {
    SDL_Window* window = nullptr;
    int32_t width = 1920;
    int32_t height = 1080;
    bool enable_validation = true;
    PresentMode present_mode = PresentMode::Vsync;
};

// Forward declaration — the actual implementation is hidden in the .cpp
// Game code never touches Vulkan types directly
class RendererImpl;

class Renderer {
public:
    Renderer();
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool init(const RendererConfig& config);
    void shutdown();

    bool begin_frame();
    void end_frame();

    // Wait for all GPU work to complete. Call before destroying resources.
    void wait_idle();

    void on_resize(int32_t width, int32_t height);

    // Set the active texture and mesh (loaded by ResourceManager).
    // set_texture is a compatibility shim that wraps the texture in
    // a single-slot material; new code should call set_material with
    // a Material loaded from ResourceManager::load_material_binary.
    void set_texture(const void* texture);
    void set_mesh(const void* mesh);

    // Bind a material's texture set for subsequent draw() calls.
    // Pass nullptr to fall back to the renderer's defaults (a draw
    // with no material set bound is silently skipped). The Material
    // must outlive the draw call - the renderer holds a borrowed
    // pointer until set_material is called again.
    void set_material(const Material* material);

    // Allocate a descriptor set for a material from the renderer's
    // material pool. textures is an array of 5 const Texture*
    // (cast to const void*) in slot order: diffuse, normal,
    // metallic-roughness, occlusion, emissive. Pass nullptr in any
    // slot to use the renderer's default 1x1 texture for that slot.
    //
    // Returns an opaque handle (a VkDescriptorSet) that the caller
    // stores in Material::descriptor_set. Returns nullptr if the
    // pool is exhausted (logs a warning).
    //
    // ResourceManager calls this from load_material_binary; game
    // code should not call it directly.
    void* create_material_descriptor_set(const void* texture_slots[5]);

    // Free a material descriptor set previously returned by
    // create_material_descriptor_set. Must be called with the GPU
    // idle (call wait_idle first if unsure).
    //
    // ResourceManager calls this from shutdown / cache eviction;
    // game code should not call it directly.
    void free_material_descriptor_set(void* descriptor_set);

    // Pick which graphics pipeline subsequent draw() calls use.
    // 0 = textured (default), 1 = debug-normal visualizer (renders
    // the vertex normal as RGB; useful for checking mesh data
    // without a real material). Out-of-range values fall back to 0.
    void set_pipeline(uint32_t index);

    // Set the active camera matrix for subsequent frames. Call once
    // per frame after camera movement, before kuma::end_frame().
    void set_view_projection(const Mat4& view_projection);

    // Set the model matrix for the next draw. Defaults to identity if
    // never called. Re-call between draws to render different transforms.
    void set_model_matrix(const Mat4& model);

    // Records ONE draw call using the current state (mesh, texture,
    // model matrix). Call between begin_frame() and end_frame(), once
    // per object you want rendered. Safe to call zero times - the
    // frame still clears and presents.
    void draw();

    // Internal — returns opaque GPU context for resource loading.
    // Game code should not call this.
    void* gpu_context();

    // Internal - returns opaque debug-overlay init context for the
    // engine to forward into kuma::debug::init(). Game code should
    // not call this.
    void* imgui_init_context();

    // ── Particle rendering (engine-internal) ────────────────────
    // The particles:: module hands per-frame instance data to the
    // renderer through these calls. Not for game code.

    // Per-particle data the renderer expects in its instance ring
    // buffer. Layout MUST match the binding 1 declaration in the
    // particle vertex shader byte-for-byte. position is world space,
    // size is the particle's edge length in world units, color is
    // RGBA with alpha driving the blend.
    struct ParticleInstance {
        float position[3];
        float size;
        float color[4];
    };

    // Append one emitter's worth of per-particle render state into
    // the current frame's instance ring buffer. instances points at
    // a tightly-packed array of ParticleInstance structures; count
    // is the number of entries. Returns the byte offset into the
    // ring buffer where the upload landed (later passed to
    // draw_particles), or kInvalidParticleUpload if the ring buffer
    // would overflow.
    static constexpr uint32_t kInvalidParticleUpload = 0xFFFF'FFFFu;
    uint32_t upload_particle_instances(const void* instances, uint32_t count);

    // Issue an instanced draw for a previously-uploaded chunk of
    // particle instances. Caller is responsible for binding the
    // material descriptor set (via set_material) before calling.
    // upload_offset must be a value previously returned by
    // upload_particle_instances; count is the number of instances
    // to draw (<= the number originally uploaded).
    void draw_particles(uint32_t upload_offset, uint32_t count,
                        const Mat4& view, const Mat4& view_projection);

private:
    RendererImpl* impl_ = nullptr;
};

}  // namespace kuma
