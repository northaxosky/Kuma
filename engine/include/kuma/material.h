#pragma once

#include <kuma/math.h>

#include <cstdint>

namespace kuma {

// ── TextureUsage ────────────────────────────────────────────────
// Tells the renderer how a texture's pixel data should be
// interpreted by the GPU when sampled.
//
// Color textures (diffuse, emissive) hold values authored in sRGB
// space - they are the colors a human picked in a paint program.
// They MUST be uploaded as an sRGB-tagged format so the GPU converts
// them to linear space when sampled, otherwise lighting math
// produces wrong results and gradients look gamma-crushed.
//
// Data textures (normal, metallic-roughness, occlusion) are not
// colors - they encode vectors or scalars and must be sampled
// linearly. Uploading them with an sRGB format double-corrects them
// and produces visibly wrong shading once lighting is on.
enum class TextureUsage {
    Color,  // sRGB - diffuse, emissive
    Data,   // UNORM/linear - normal, metallic-roughness, occlusion
};

// ── Material ────────────────────────────────────────────────────
// Runtime representation of a baked .kmaterial. The descriptor set
// is allocated and owned by the renderer (lives in its material
// pool) but referenced through Material so game code can pass one
// pointer around instead of a void* + factor blob.
//
// Most factor fields are unused by the current diffuse-only shader.
// They exist on the runtime struct so a future lit shader can read
// them without changing the load path or the .kmaterial format.
//
// This is a pure-CPU runtime struct - it is NOT directly copied to
// the GPU, so it uses the engine's Vec3/Vec4 types instead of the
// raw float arrays the on-disk KMaterialHeader has to stay locked
// to.
struct Material {
    void* descriptor_set = nullptr;        // opaque VkDescriptorSet

    uint32_t flags      = 0;               // see kMaterialFlag* in asset_format.h
    uint32_t alpha_mode = 0;               // see kAlphaMode* in asset_format.h

    Vec4 base_color         = {1.0f, 1.0f, 1.0f, 1.0f};
    float alpha_cutoff      = 0.5f;
    float metallic_factor   = 0.0f;
    float roughness_factor  = 1.0f;
    float normal_scale      = 1.0f;
    float occlusion_strength = 1.0f;
    Vec3 emissive_factor    = {0.0f, 0.0f, 0.0f};
};

}  // namespace kuma
