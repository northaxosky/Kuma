#pragma once

// ── Kuma asset binary format ────────────────────────────────────
// On-disk layout for .kmesh and .ktex files produced by kuma-bake.
// MUST stay byte-for-byte identical to tools/kuma-bake/src/format.rs.
// Any change in either place requires bumping the corresponding
// version constant and updating the other side in lockstep.
//
// The static_asserts below catch size drift at C++ compile time;
// field reordering must be caught by careful review.

#include <cstddef>
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

// ── Pure parsers (no Vulkan, no I/O) ────────────────────────────
// Validate the header + slice layout of an in-memory .kmesh /
// .ktex blob. Engine loaders call these before doing any GPU
// upload; integration tests drive them with hand-crafted byte
// buffers to exercise corruption cases (bad magic, bad version,
// truncated payload, offset overlap, integer overflow risk).
//
// On Ok, `out_header` is populated and the payload byte ranges
// `[header.vertex_offset, +vertex_count*sizeof(Vertex))` etc. are
// guaranteed in-bounds. On any other result, out_header contents
// are unspecified and callers must not deref the payload offsets.

enum class ParseResult {
    Ok,
    TooSmall,           // buffer < sizeof(header)
    BadMagic,           // first 4 bytes != expected magic
    VersionMismatch,    // version field != current version
    UnsupportedFormat,  // (KTex only) format field not in known set
    OffsetOutOfBounds,  // vertex/index/pixel slice extends past buffer
    OffsetOverlap,      // vertex slice overlaps with index slice
    PayloadOverflow,    // count * sizeof(elem) overflows or exceeds buffer
};

ParseResult parse_kmesh_header(const void* data, size_t size, KMeshHeader& out_header);
ParseResult parse_ktex_header(const void* data, size_t size, KTexHeader& out_header);

}  // namespace kuma::asset_format
