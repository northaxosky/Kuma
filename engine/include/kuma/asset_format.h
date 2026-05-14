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
inline constexpr uint32_t kMagicKMesh  = 0x48534D4B;  // 'KMSH'
inline constexpr uint32_t kMagicKTex   = 0x5845544B;  // 'KTEX'
inline constexpr uint32_t kMagicKSound = 0x444E534B;  // 'KSND'
inline constexpr uint32_t kMagicKScene = 0x4E43534B;  // 'KSCN'

inline constexpr uint32_t kKMeshVersion  = 1;
inline constexpr uint32_t kKTexVersion   = 1;
inline constexpr uint32_t kKSoundVersion = 1;
inline constexpr uint32_t kKSceneVersion = 1;

// ── Texture pixel formats ───────────────────────────────────────
inline constexpr uint32_t kFormatRGBA8 = 1;

// ── Audio payload formats ───────────────────────────────────────
// PcmF32 = the payload is raw IEEE-754 float32 samples, little
// endian, interleaved frame-major (mono = M M M, stereo = L R L R),
// frame_count = sample_count, payload_size = frame_count * channels * 4.
//
// Compressed formats store the original encoded bytes (passthrough).
// frame_count is 0 because the engine doesn't pre-compute it; it
// hands the bytes to the audio backend's decoder at load time.
inline constexpr uint32_t kAudioFormatPcmF32 = 0;
inline constexpr uint32_t kAudioFormatOgg    = 1;
inline constexpr uint32_t kAudioFormatMp3    = 2;
inline constexpr uint32_t kAudioFormatFlac   = 3;
inline constexpr uint32_t kAudioFormatMaxKnown = kAudioFormatFlac;

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

// ── KSound header ───────────────────────────────────────────────
// PCM payloads: little-endian f32, interleaved frame-major.
// Compressed payloads (OGG/MP3/FLAC): original encoded bytes,
// frame_count == 0 (the audio backend computes it lazily at load).
struct KSoundHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t format;         // see kAudioFormat* constants
    uint32_t sample_rate;    // hz
    uint32_t channels;       // 1 (mono) or 2 (stereo)
    uint32_t frame_count;    // frames per channel for PCM, 0 for compressed
    uint32_t payload_offset; // byte offset of first payload byte
    uint32_t payload_size;   // payload byte count
};
static_assert(sizeof(KSoundHeader) == 32,
              "KSoundHeader layout must match tools/kuma-bake/src/format.rs");

// ── KScene header ───────────────────────────────────────────────
// Multi-mesh scene asset. Layout:
//   header (32 bytes)
//   mesh table  (mesh_count entries, KSceneMeshEntry each)
//   node table  (node_count entries, KSceneNodeEntry each)
//   string table (variable length, packed utf-8 paths)
//
// All offsets are byte offsets from the start of the file. Mesh
// paths are stored in the string table and resolved at runtime
// relative to the .kscene's own directory; absolute paths and
// "../" traversal are rejected by the loader for safety.
struct KSceneHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t mesh_count;
    uint32_t node_count;
    uint32_t mesh_table_offset;
    uint32_t node_table_offset;
    uint32_t string_table_offset;
    uint32_t string_table_size;
};
static_assert(sizeof(KSceneHeader) == 32,
              "KSceneHeader layout must match tools/kuma-bake/src/format.rs");

// One per unique mesh referenced by the scene. Stores a path into
// the string table; the path is relative to the .kscene's directory.
struct KSceneMeshEntry {
    uint32_t path_offset;  // byte offset into string table (RELATIVE to its start)
    uint32_t path_length;  // byte length of the path
};
static_assert(sizeof(KSceneMeshEntry) == 8,
              "KSceneMeshEntry layout must match tools/kuma-bake/src/format.rs");

// One per renderable node. mesh_index == kSceneNoMesh marks a
// node with no geometry; the loader currently discards those at
// bake time, but the constant exists so a future format that
// keeps named-marker nodes can use it.
inline constexpr uint32_t kSceneNoMesh = 0xFFFFFFFFu;

// transform is a 4x4 column-major matrix already composed with
// the parent chain - the runtime applies it directly to the
// entity's Transform without walking any hierarchy.
struct KSceneNodeEntry {
    uint32_t mesh_index;
    uint32_t reserved;
    float    transform[16];
};
static_assert(sizeof(KSceneNodeEntry) == 72,
              "KSceneNodeEntry layout must match tools/kuma-bake/src/format.rs");

// ── Pure parsers (no I/O, no third-party libs) ──────────────────
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
    UnsupportedFormat,  // (KTex / KSound) format field not in known set
    OffsetOutOfBounds,  // payload slice extends past buffer
    OffsetOverlap,      // (KMesh) vertex slice overlaps with index slice
    PayloadOverflow,    // count * sizeof(elem) overflows or exceeds buffer
    BadSampleRate,      // (KSound) sample_rate == 0
    BadChannels,        // (KSound) channels != 1 and != 2
    BadFrameCount,      // (KSound) PCM frame_count == 0 OR compressed frame_count != 0
    PayloadSizeMismatch,// (KSound) PCM payload_size != frame_count * channels * 4
    BadMeshIndex,       // (KScene) node references a mesh_index >= mesh_count
};

ParseResult parse_kmesh_header(const void* data, size_t size, KMeshHeader& out_header);
ParseResult parse_ktex_header(const void* data, size_t size, KTexHeader& out_header);
ParseResult parse_ksound_header(const void* data, size_t size, KSoundHeader& out_header);
ParseResult parse_kscene_header(const void* data, size_t size, KSceneHeader& out_header);

}  // namespace kuma::asset_format
