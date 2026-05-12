#pragma once

// ── Kuma asset binary format ────────────────────────────────────
// On-disk layout for .kmesh and .ktex files produced by kuma-bake.
// MUST stay byte-for-byte identical to tools/kuma-bake/src/format.rs.
// Any change in either place requires bumping the corresponding
// version constant and updating the other side in lockstep.
//
// The static_asserts below catch size drift at C++ compile time;
// field reordering must be caught by careful review.

#include <cstdint>

namespace kuma::asset_format {

// ── Magic codes (4 ASCII bytes, little-endian u32 in code) ──────
// Hex dump of a valid .kmesh starts with `4B 4D 53 48` ('KMSH').
inline constexpr uint32_t kMagicKMesh = 0x48534D4B;  // 'KMSH'
inline constexpr uint32_t kMagicKTex  = 0x5845544B;  // 'KTEX'

inline constexpr uint32_t kKMeshVersion = 1;
inline constexpr uint32_t kKTexVersion  = 1;

// ── Texture pixel formats ───────────────────────────────────────
inline constexpr uint32_t kFormatRGBA8 = 1;

// ── Vertex (matches engine's Vulkan vertex input) ───────────────
struct Vertex {
    float pos[3];
    float uv[2];
    float normal[3];
};
static_assert(sizeof(Vertex) == 32,
              "Vertex layout must match tools/kuma-bake/src/format.rs");

// ── KMesh header ────────────────────────────────────────────────
struct KMeshHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t vertex_count;
    uint32_t index_count;
    uint32_t vertex_offset;
    uint32_t index_offset;
    uint32_t reserved[2];
};
static_assert(sizeof(KMeshHeader) == 32,
              "KMeshHeader layout must match tools/kuma-bake/src/format.rs");

// ── KTex header ─────────────────────────────────────────────────
struct KTexHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t pixel_offset;
    uint32_t reserved[2];
};
static_assert(sizeof(KTexHeader) == 32,
              "KTexHeader layout must match tools/kuma-bake/src/format.rs");

}  // namespace kuma::asset_format
